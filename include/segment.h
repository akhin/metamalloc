/*
    - A SEGMENT IS A COLLECTION OF LOGICAL PAGES. IT MAKES IT EASIER TO MANAGE MULTIPLE LOGICAL_PAGES :

                1. AS FOR ALLOCATIONS, WHEN MEMORY CONSUMPTION IS HIGH , SEQUENTIAL SEARCH FROM STARTING PAGE CAN BE VERY SLOW FOR. IT UTILISES "NEXT FIT" SO SEARCHES START FROM THE LOGICAL PAGE WHICH WAS THE LAST ALLOCATOR

                2. WE CAN GIVE COMPLETELY FREE PAGES BACK TO SYSTEM IF WE WILL NOT FALL UNDER "MINIMUM_LOGICAL_PAGES" THRESHOLD . OTHERWISE MEMORY CONSUMPTION OF ALLOCATOR WILL NEVER GO DOWN
                  ( WE LIMIT LOGICAL PAGES TO VM PAGE SIZES MORE EASILY SO THAT OBJECT DON'T END UP IN MULTIPLE PAGES )

    - INITIALLY HAS CONTIGIOUS LOGICAL PAGES LIKE SPANS/PAGE RUNS. HOWEVER THAT CAN CHANGE IF IT IS AN UNBOUNDED SEGMENT.

    - MINIMUM LOGICAL PAGE SIZE FOR LINUX IS 4KB/4096 ON LINUX AND 64KB/65536 ON WINDOWS. LOGICAL PAGE SIZE ALSO HAS TO BE A MULTIPLE OF VIRTUAL MEMORY PAGE ALLOCATION GRANULARITY.

    - IT WILL PLACE A LOGICAL PAGE HEADER TO INITIAL 64 BYTES OF EVERY LOGICAL PAGE.

    - METADATA USAGE : PAGE HEADERS IN METAMALLOC ARE 64 BYTES THEREFORE FOR METADATA, WE WILL USE 64 BYTES PER EACH LOGICAL PAGE.
      IF SELECTED PAGE SIZE 4096 BYTES , 64 BYTES IN 4096 IS 0.78%
*/
#ifndef __SEGMENT_H__
#define __SEGMENT_H__


#include <cstddef>
#include <cstdint>
#include <cassert>
#include <type_traits>

#include "compiler/unused.h"
#include "compiler/hints_branch_predictor.h"
#include "compiler/hints_hot_code.h"
#include "cpu/alignment_constants.h"
#include "os/virtual_memory.h"
#include "utilities/alignment_checks.h"
#include "utilities/size_utilities.h"
#include "utilities/modulo_utilities.h"
#include "utilities/multiple_utilities.h"
#include "utilities/lockable.h"
#include "deallocation_queue.h"
#include "arena_base.h"
#include "logical_page_header.h"

enum class PageRecyclingPolicy
{
    IMMEDIATE,    // IF LOGICAL PAGE COUNT > PAGE RECYCLING THRESHOLD VALUE, RECYCLING DONE AT THE END OF DEALLOCATIONS.
    DEFERRED    // USER HAS TO CALL "recycle_free_logical_pages"
};

enum class ConcurrencyPolicy
{
                        // BOUNDEDNESS                    DESCRIPTION

    THREAD_LOCAL,       // Bounded , can't grow           Partial locking needed. Deallocates just push ptrs to a spinlock based q and they quit, as they can come from multiple threads.
                        //                                Allocs will come from only one thread and they do actual deallocation by consuming dealloc q.
    CENTRAL,            // Unbounded, can grow            Segment level locking needed
    SINGLE_THREAD       // Unbounded, can grow            No locks
};

struct SegmentCreationParameters
{
    std::size_t m_logical_page_size= 0;
    std::size_t m_logical_page_count = 0;
    std::size_t m_page_recycling_threshold = 0;
    std::size_t m_deallocation_queue_initial_capacity = 65536;
    uint32_t m_size_class = 0;                             // 0 means that that segment will hold arbitrary size. Otherwise it will be for only 1 sizeclass.
    double m_grow_coefficient = 0.0;                     // 0 means that we will be growing by allocating only required amount
};

#ifdef ENABLE_STATS
#include <array>
static inline constexpr std::size_t MAX_GROW_STAT_COUNT = 32;

struct SegmentStats
{
    std::size_t m_size_class = 0;
    std::size_t m_latest_logical_page_count = 0;
    std::size_t m_recycle_count =0;
    std::size_t m_grow_size_count = 0;
    std::array<std::size_t, MAX_GROW_STAT_COUNT> m_grow_sizes {0};
};
#endif

#if defined(ENABLE_PERF_TRACES) || defined(ENABLE_REPORT_LEAKS) // VOLTRON_EXCLUDE
#include <cstdio>
#endif // VOLTRON_EXCLUDE

template <
            ConcurrencyPolicy concurrency_policy,
            typename LogicalPageType,
            typename ArenaType,
            PageRecyclingPolicy page_recycling_policy = PageRecyclingPolicy::IMMEDIATE,
            bool buffer_aligned_to_logical_page_size = false
        >
class Segment : public Lockable<LockPolicy::USERSPACE_LOCK>
{
    public:

        Segment()
        {
            static_assert(std::is_base_of<ArenaBase<ArenaType>, ArenaType>::value);
            m_logical_page_object_size = sizeof(LogicalPageType);
            assert( m_logical_page_object_size == sizeof(LogicalPageHeader) );
        }

        ~Segment()
        {
            destroy();
        }

        Segment(const Segment& other) = delete;
        Segment& operator= (const Segment& other) = delete;
        Segment(Segment&& other) = delete;
        Segment& operator=(Segment&& other) = delete;

        [[nodiscard]] bool create(char* external_buffer, ArenaType* arena_ptr, const SegmentCreationParameters& params)
        {
            if (params.m_size_class < 0 || params.m_logical_page_size <= 0 || MultipleUtilities::is_size_a_multiple_of_page_allocation_granularity(params.m_logical_page_size) == false
                || params.m_logical_page_count <= 0 || params.m_logical_page_size <= m_logical_page_object_size || !external_buffer || !arena_ptr)
            {
                return false;
            }

            if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL)
            {
                if (m_deallocation_queue.create(params.m_deallocation_queue_initial_capacity/sizeof(PointerPage), ArenaType::MetadataAllocator::allocate(params.m_deallocation_queue_initial_capacity)) == false)
                {
                    return false;
                }

                m_deallocation_queue_processing_lock.initialise();
            }

            m_arena = arena_ptr;
            m_logical_page_size = params.m_logical_page_size;
            m_max_object_size = m_logical_page_size - sizeof(LogicalPageHeader);
            m_size_class = params.m_size_class;
            m_page_recycling_threshold = params.m_page_recycling_threshold;
            m_grow_coefficient = params.m_grow_coefficient;

            if (grow(external_buffer, params.m_logical_page_count) == nullptr)
            {
                return false;
            }

            if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL)
            {
                // We are bounded , we want to know our buffer limit
                m_buffer_length = m_logical_page_size * params.m_logical_page_count;
            }

            return true;
        }

        // size=0 means that underlying logical page can hold only one size which is 'm_size_class' which is set by create
        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size = 0)
        {
            if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL)
            {
                // THREAD LOCAL
                if constexpr (LogicalPageType::supports_any_size()==false) // Underyling type should be used for same size class
                {
                    void* pointer = process_deallocation_queue<true>(); // While pointers in the deallocation queue are deallocated ,one of them will be returned to the allocation requestor

                    if (pointer)
                    {
                        return pointer;
                    }
                    else
                    {
                        return allocate_internal(size);
                    }
                }
                else
                {
                    process_deallocation_queue<false>();
                    return allocate_internal(size);
                }
            }
            else if constexpr (concurrency_policy == ConcurrencyPolicy::CENTRAL)
            {
                // CENTRAL , we are locking the entire segment
                this->enter_concurrent_context();
                auto ret = allocate_internal(size);
                this->leave_concurrent_context();
                return ret;
            }
            else
            {
                // SINGLE THREADED, NO LOCKS NEEDED
                return allocate_internal(size);
            }
        }

        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE)
        void deallocate(void* ptr)
        {
            if( m_head == nullptr ) { return;}

            if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL)
            {
                // THREAD LOCAL
                m_deallocation_queue.push(ptr); // Q is thread safe
            }
            else if constexpr(concurrency_policy == ConcurrencyPolicy::CENTRAL)
            {
                // CENTRAL , we are locking entire segment
                this->enter_concurrent_context();
                deallocate_internal(ptr);
                this->leave_concurrent_context();
            }
            else
            {
                // SINGLE THREADED , NO LOCKS NEEDED
                deallocate_internal(ptr);
            }
        }

        // MAY BE CALLED FROM HEAP DEALLOCATION METHODS. IN CASE OF THREAD LOCAL OR CENTRAL HEAP , THAT MEANS MULTIPLE THREADS
        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE)
        bool owns_pointer(void* ptr)
        {
            if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL || concurrency_policy == ConcurrencyPolicy::CENTRAL)
            {
                this->enter_concurrent_context();
            }

            if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL)
            {
                // BOUNDED BUFFER
                uint64_t address_in_question = reinterpret_cast<uint64_t>(ptr);

                if (address_in_question >= reinterpret_cast<uint64_t>(m_head) && address_in_question < (reinterpret_cast<uint64_t>(m_head) + m_buffer_length))
                {
                    if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL || concurrency_policy == ConcurrencyPolicy::CENTRAL)
                    {
                        this->leave_concurrent_context();
                    }
                    return true;
                }
            }
            else
            {
                // UNBOUNDED BUFFER , WE NEED A SEARCH
                std::size_t address = reinterpret_cast<std::size_t>(ptr);

                LogicalPageType* iter = m_head;

                while (iter != nullptr)
                {
                    std::size_t start = reinterpret_cast<std::size_t>(iter);
                    std::size_t end = start + m_logical_page_size;

                    if (address >= start && address < end)
                    {
                        if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL || concurrency_policy == ConcurrencyPolicy::CENTRAL)
                        {
                            this->leave_concurrent_context();
                        }
                        return true;
                    }

                    iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
                }

            }

            if constexpr (concurrency_policy == ConcurrencyPolicy::THREAD_LOCAL || concurrency_policy == ConcurrencyPolicy::CENTRAL)
            {
                this->leave_concurrent_context();
            }
            return false;
        }

        void recycle_free_logical_pages()
        {
            // Should be called only when using deferred recycling
            if constexpr (page_recycling_policy == PageRecyclingPolicy::IMMEDIATE)
            {
                assert(0 == 1);
            }

            this->enter_concurrent_context();
            ///////////////////////////////////////////////////////////////////
            auto num_logical_pages_to_recycle = m_logical_page_count - m_page_recycling_threshold;
            LogicalPageType* iter = m_head;
            LogicalPageType* iter_previous = nullptr;

            while (num_logical_pages_to_recycle)
            {
                if (iter->can_be_recycled())
                {
                    recycle_logical_page(iter); // This method will update iter_previous's next ptr
                    iter = reinterpret_cast<LogicalPageType*>(iter_previous->get_next_logical_page());
                    num_logical_pages_to_recycle--;
                }
                else
                {
                    iter_previous = iter;
                    iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
                }
            }

            ///////////////////////////////////////////////////////////////////
            this->leave_concurrent_context();
        }

        void transfer_logical_pages_from(Segment& from)
        {
            this->enter_concurrent_context();
            ///////////////////////////////////////////////////////////////////
            LogicalPageType* iter = from.m_head;

            while (iter)
            {
                LogicalPageType* iter_next = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());

                add_logical_page(iter); // Will also update iter's next ptr
                from.remove_logical_page(iter);

                iter = iter_next;
            }
            ///////////////////////////////////////////////////////////////////
            this->leave_concurrent_context();
        }

        std::size_t get_usable_size(void* ptr)
        {
            if constexpr (LogicalPageType::supports_any_size() == false)
            {
                return m_size_class;
            }
            else
            {
                LogicalPageType* iter = m_head;
                std::size_t address = reinterpret_cast<std::size_t>(ptr);

                while (iter != nullptr)
                {
                    std::size_t start = reinterpret_cast<std::size_t>(iter);
                    std::size_t end = start + m_logical_page_size;

                    if (address >= start && address < end)
                    {
                        return iter->get_usable_size(ptr);
                    }

                    iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
                }

                return 0;
            }
        }

        // Constant time logical page look up method for finding logical pages if their start addresses are aligned to logical page size
        static LogicalPageType* get_logical_page_from_address(void* ptr, std::size_t logical_page_size)
        {
            static_assert(buffer_aligned_to_logical_page_size == true);
            
            uint64_t orig_ptr = reinterpret_cast<uint64_t>(ptr);
            // Masking below is equivalent of -> orig_ptr - ModuloUtilities::modulo(orig_ptr, logical_page_size);
            uint64_t target_page_address = orig_ptr & ~(logical_page_size - 1);
            LogicalPageType* target_logical_page = reinterpret_cast<LogicalPageType*>(target_page_address);
            return target_logical_page;
        }

        // Constant time size_class look up method for finding logical pages if their start addresses are aligned to logical page size
        static uint32_t get_size_class_from_address(void* ptr, std::size_t logical_page_size)
        {
            static_assert(buffer_aligned_to_logical_page_size == true);
            
            LogicalPageType* target_logical_page = get_logical_page_from_address(ptr, logical_page_size);
            return target_logical_page->get_size_class();
        }

        void lock_pages()
        {
            this->enter_concurrent_context();
            ///////////////////////////////////////////////////////////////////
            LogicalPageType* iter = m_head;

            while (iter)
            {
                VirtualMemory::lock(reinterpret_cast<void*>(iter), m_logical_page_size);
                iter->mark_as_locked();
                iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
            }
            ///////////////////////////////////////////////////////////////////
            this->leave_concurrent_context();
        }

        void unlock_pages()
        {
            this->enter_concurrent_context();
            ///////////////////////////////////////////////////////////////////
            LogicalPageType* iter = m_head;

            while (iter)
            {
                VirtualMemory::unlock(reinterpret_cast<void*>(iter), m_logical_page_size);
                iter->mark_as_non_locked();
                iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
            }
            ///////////////////////////////////////////////////////////////////
            this->leave_concurrent_context();
        }

        #ifdef UNIT_TEST
        std::size_t get_logical_page_count() const { return m_logical_page_count; }
        #endif

        #ifdef ENABLE_STATS
        SegmentStats get_stats() { m_stats.m_latest_logical_page_count = m_logical_page_count; return m_stats; }
        #endif

    private:
        uint32_t m_size_class = 0;                    // if m_size_class is zero that means, underlying logical page can hold any size
        std::size_t m_logical_page_size = 0;            // It includes also m_logical_page_object_size
        std::size_t m_max_object_size = 0;
        std::size_t m_logical_page_object_size = 0;
        std::size_t m_logical_page_count = 0;
        std::size_t m_buffer_length = 0;  // Applies to only bounded segments
        LogicalPageType* m_head = nullptr;
        LogicalPageType* m_tail = nullptr;
        LogicalPageType* m_last_used = nullptr;
        std::size_t m_page_recycling_threshold = 0; // In auto page recycling mode, if a logical page is free after deallocation ,
                                                    // it will be given back to system if free l.page count is over that threshold
        double m_grow_coefficient = 1.0;            // Applies to unbounded segments
        DeallocationQueue<typename ArenaType::MetadataAllocator> m_deallocation_queue;
        ArenaType* m_arena = nullptr;
        UserspaceSpinlock<> m_deallocation_queue_processing_lock;

        #ifdef ENABLE_STATS
        SegmentStats m_stats;
        #endif

        // Returns first logical page ptr of the grow
        [[nodiscard]] LogicalPageType* grow(char* buffer, std::size_t logical_page_count)
        {
            LogicalPageType* first_new_logical_page = nullptr;
            LogicalPageType* previous_page = m_tail;
            LogicalPageType* iter_page = nullptr;

            auto create_new_logical_page = [&](char* logical_page_buffer) -> bool
            {
                iter_page = new(logical_page_buffer) LogicalPageType();

                bool success = iter_page->create(logical_page_buffer + m_logical_page_object_size, m_logical_page_size - m_logical_page_object_size, m_size_class);

                if (success == false)
                {
                    m_arena->release_to_system(buffer, m_logical_page_size);
                    return false;
                }

                iter_page->mark_as_used();

                m_logical_page_count++;

                return true;
            };
            /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // FIRST PAGE
            if (unlikely(create_new_logical_page(buffer) == false))
            {
                return nullptr;
            }

            first_new_logical_page = iter_page;

            if (m_head == nullptr)
            {
                // The very first page
                m_head = iter_page;
                m_tail = iter_page;
            }
            else
            {
                previous_page->set_next_logical_page(iter_page);
                iter_page->set_previous_logical_page(previous_page);
            }

            previous_page = iter_page;

            /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // REST OF THE PAGES
            for (std::size_t i = 1; i < logical_page_count; i++)
            {
                if (create_new_logical_page(buffer + (i * m_logical_page_size)) == false)
                {
                    return nullptr;
                }

                previous_page->set_next_logical_page(iter_page);
                iter_page->set_previous_logical_page(previous_page);
                previous_page = iter_page;
            }

            m_tail = iter_page;

            return first_new_logical_page;
        }

        void recycle_logical_page(LogicalPageType* affected)
        {
            remove_logical_page(affected);
            affected->~LogicalPageType();
            m_arena->release_to_system(affected, m_logical_page_size);
            #ifdef ENABLE_STATS
            m_stats.m_recycle_count++;
            #endif
        }

        void remove_logical_page(LogicalPageType* affected)
        {
            auto next = reinterpret_cast<LogicalPageType*>(affected->get_next_logical_page());
            auto previous = reinterpret_cast<LogicalPageType*>(affected->get_previous_logical_page());

            if (affected == m_last_used)
            {
                if (previous)
                {
                    m_last_used = previous;
                }
                else if (next)
                {
                    m_last_used = next;
                }
                else
                {
                    m_last_used = nullptr;
                }
            }

            if (previous == nullptr)
            {
                m_head = next;

                if (m_head == nullptr || m_head->get_next_logical_page() == 0)
                {
                    m_tail = m_head;
                }
            }
            else
            {
                previous->set_next_logical_page(next);

                if (m_tail == affected)
                {
                    m_tail = previous;
                }
            }

            if (next)
                next->set_previous_logical_page(previous);

            m_logical_page_count--;
        }

        void add_logical_page(LogicalPageType* logical_page)
        {
            if (m_tail)
            {
                m_tail->set_next_logical_page(logical_page);
                logical_page->set_previous_logical_page(m_tail);
            }
            else
            {
                m_head = logical_page;
                m_tail = logical_page;
            }

            logical_page->set_next_logical_page(nullptr);
            m_logical_page_count++;
        }

        void destroy()
        {
            if constexpr (concurrency_policy==ConcurrencyPolicy::THREAD_LOCAL)
            {
                process_deallocation_queue();
            }

            LogicalPageType* iter = m_head;
            LogicalPageType* next = nullptr;

            while (iter)
            {
                next = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
                /////////////////////////////////////////////////////////////////////////////
                #ifdef ENABLE_REPORT_LEAKS
                if (iter->get_used_size() != 0)
                {
                    FILE* leak_report = nullptr;
                    leak_report = fopen("leaks.txt", "a+"); // does not allocate memory
            
                    if (leak_report) 
                    {
                        if(m_size_class!=0)
                        {
                            fprintf(leak_report, "Potential memory leak : sizeclass=%zu count=%zu\n", static_cast<std::size_t>(m_size_class), static_cast<std::size_t>(iter->get_used_size() / m_size_class));
                        }
                        else
                        {
                            fprintf(leak_report, "Potential memory leak : total size=%zu \n", static_cast<std::size_t>(iter->get_used_size()));
                        }
                        
                        fclose(leak_report);
                    }
                    else
                    {
                        fprintf(stderr, "Failed to open leaks.txt for writing\n"); // does not allocate memory
                    }
                }
                #endif

                // There still maybe unloaded dynamic libraries (shared objects or DLLs) loaded by the host process
                // Even worse there may be unloaded but memory-leaking dynamic libraries
                // If we destroy a segment which has not been deallocated fully, we will crash, therefore this check is needed
                // If it is not possible to destroy then OS will reclaim virtual memory pages as soon as the host process dies.
                if (iter->get_used_size() == 0)
                {
                    // Invoking dtor of logical page
                    iter->~LogicalPageType();
                    // Release pages back to system if we are managing the arena
                    m_arena->release_to_system(iter, m_logical_page_size);
                }

                /////////////////////////////////////////////////////////////////////////////
                iter = next;
            }

            m_head = nullptr;
            m_tail = nullptr;
        }

        // DEALLOCATES ALL POINTERS IN THE DEALLOCATION QUEUE
        // IF THE CALLER IS ALLOCATOR INITIAL POINTER WON'T BE DEALLOCATED BUT INSTEAD RETURNED TO THE CALLER
        // TO SERVE ALLOCATIONS AS FAST AS POSSIBLE
        template <bool return_initial_pointer=false>
        void* process_deallocation_queue()
        {
            void* ret = nullptr;

            while (true)
            {
                auto pointer = m_deallocation_queue.pop();

                if (pointer == nullptr)
                {
                    break;
                }

                if constexpr (return_initial_pointer)
                {
                    if (likely(ret != nullptr))
                    {
                        deallocate_internal(pointer);
                    }
                    else
                    {
                        ret = pointer;
                    }
                }
                else
                {
                    deallocate_internal(pointer);
                }
            }

            return ret;
        }

        [[nodiscard]] void* allocate_internal(std::size_t size)
        {

            if (unlikely(size > m_max_object_size))
            {
                return nullptr;
            }

            ///////////////////////////////////////////////////////////////////
            // Next-fit like , we start searching from where we left if possible
            void* ret = nullptr;
            LogicalPageType* iter = m_last_used ? m_last_used : m_head;

            while (iter)
            {
                ret = iter->allocate(size);

                if (ret != nullptr)
                {
                    m_last_used = iter;

                    return ret;
                }

                iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
            }
            ///////////////////////////////////////////////////////////////////
            // If we started the search from a non-head node,  then we need one more iteration
            if (m_last_used)
            {
                iter = m_head;

                while (iter != m_last_used)
                {
                    ret = iter->allocate(size);

                    if (ret != nullptr)
                    {
                        m_last_used = iter;

                        return ret;
                    }

                    iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
                }
            }
            ///////////////////////////////////////////////////////////////////
            // If we reached here , it means that we need to allocate more memory
            if constexpr (concurrency_policy == ConcurrencyPolicy::CENTRAL || concurrency_policy == ConcurrencyPolicy::SINGLE_THREAD) // Only unbounded concurrecy policies can grow
            {
                std::size_t new_logical_page_count = 0;
                std::size_t minimum_new_logical_page_count = 0;
                calculate_quantities(size, new_logical_page_count, minimum_new_logical_page_count);

                char* new_buffer = nullptr;
                new_buffer = static_cast<char*>(m_arena->allocate(m_logical_page_size * new_logical_page_count));

                if (new_buffer == nullptr && new_logical_page_count > minimum_new_logical_page_count)  // Meeting grow_coefficient is not possible so lower the new_logical_page_count
                {
                    new_buffer = static_cast<char*>(m_arena->allocate(m_logical_page_size * minimum_new_logical_page_count));
                }

                if (!new_buffer)
                {
                    return nullptr;
                }

                auto first_new_logical_page = grow(new_buffer, new_logical_page_count);

                #ifdef ENABLE_PERF_TRACES // INSIDE ALLOCATION CALLSTACK SO CAN'T ALLOCATE MEMORY HENCE OUTPUT TO stderr
                fprintf(stderr, "segment grow size=%zu  sizeclass=%u\n", size, m_size_class);
                #endif

                #ifdef ENABLE_STATS
                if(m_stats.m_grow_size_count<MAX_GROW_STAT_COUNT)
                {
                    m_stats.m_grow_sizes[m_stats.m_grow_size_count] = new_logical_page_count;
                    m_stats.m_grow_size_count++;
                }
                #endif

                if (first_new_logical_page)
                {
                    ret = first_new_logical_page->allocate(size);

                    if (ret != nullptr)
                    {
                        m_last_used = first_new_logical_page;

                        return ret;
                    }
                }

            }

            // OUT OF MEMORY !
            return nullptr;
        }

        void calculate_quantities(const std::size_t size, std::size_t& desired_new_logical_page_count, std::size_t& minimum_new_logical_page_count)
        {
            if constexpr (LogicalPageType::supports_any_size()) // That means size will be arbitrary therefore the size may not suffice
            {
                minimum_new_logical_page_count = SizeUtilities::get_required_page_count_for_allocation(m_logical_page_size, m_logical_page_object_size, size, 1);
            }
            else
            {
                minimum_new_logical_page_count = SizeUtilities::get_required_page_count_for_allocation(m_logical_page_size, m_logical_page_object_size, m_size_class, size / m_size_class);
            }

            if ( likely(m_grow_coefficient > 0))
            {
                desired_new_logical_page_count = static_cast<std::size_t>(m_logical_page_count * m_grow_coefficient);

                if (desired_new_logical_page_count < minimum_new_logical_page_count)
                {
                    desired_new_logical_page_count = minimum_new_logical_page_count;
                }
            }
            else
            {
                desired_new_logical_page_count = minimum_new_logical_page_count;
            }
        }

        void deallocate_internal(void* ptr)
        {
            if constexpr (buffer_aligned_to_logical_page_size == true)
            {
                deallocate_from_aligned_logical_page(ptr);
            }
            else
            {
                deallocate_by_search(ptr);
            }
        }

        // WARNING : WHEN CALLING THIS ONE YOU HAVE TO MAKE SURE THAT ALL LOGICAL PAGES ARE
        // PLACED AT m_logical_page_size ALIGNED ADDRESSES
        void deallocate_from_aligned_logical_page(void* ptr)
        {
            auto affected = get_logical_page_from_address(ptr, m_logical_page_size);

            affected->deallocate(ptr);

            if (affected->get_used_size() == 0)
            {
                affected->mark_as_non_used();

                if constexpr (page_recycling_policy == PageRecyclingPolicy::IMMEDIATE)
                {
                    if (m_logical_page_count > m_page_recycling_threshold)
                    {
                        recycle_logical_page(affected);
                    }
                }
            }
        }

        // MAKES A LINEAR SEARCH
        void deallocate_by_search(void* ptr)
        {
            std::size_t address = reinterpret_cast<std::size_t>(ptr);
            LogicalPageType* iter = m_head;

            while (iter != nullptr)
            {
                std::size_t start = reinterpret_cast<std::size_t>(iter);
                std::size_t end = start + m_logical_page_size;

                if (address >= start && address < end)
                {
                    iter->deallocate(ptr);

                    if (iter->get_used_size() == 0)
                    {
                        iter->mark_as_non_used();

                        if constexpr (page_recycling_policy == PageRecyclingPolicy::IMMEDIATE)
                        {
                            if (m_logical_page_count > m_page_recycling_threshold)
                            {
                                recycle_logical_page(iter);
                            }
                        }
                    }

                    return;
                }
                iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
            }
        }
};

#endif