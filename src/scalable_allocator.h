/*
    - THE ALLOCATOR WILL HAVE A CENTRAL HEAP AND ALSO THREAD OR CPU LOCAL HEAPS.

    - ALLOCATIONS INITIALLY WILL BE FROM LOCAL ( EITHER THREAD LOCAL OR CPU LOCAL ) HEAPS. IF LOCAL HEAPS ARE EXHAUSTED , THEN CENTRAL HEAP WILL BE USED.

    - USES CONFIGURABLE METADATA ( DEFAULT 128KB ) TO STORE LOCAL HEAPS.

    - YOU HAVE TO MAKE SURE THAT METADATA SIZE WILL BE ABLE TO HANDLE NUMBER OF THREADS IN YOUR APPLICATION.

*/
#ifndef __SCALABLE_ALLOCATOR__H__
#define __SCALABLE_ALLOCATOR__H__

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <new> // std::get_new_handler
#include "compiler/builtin_functions.h"
#include "compiler/hints_hot_code.h"
#include "compiler/hints_branch_predictor.h"
#include "os/thread_local_storage.h"
#include "os/virtual_memory.h"
#include "utilities/multiple_utilities.h"
#include "utilities/lockable.h"
#include "arena_base.h"
#include "heap_base.h"

#ifdef ENABLE_DEFAULT_MALLOC // VOLTRON_EXCLUDE
#include "compiler/builtin_functions.h"
#endif // VOLTRON_EXCLUDE

#ifdef ENABLE_STATS // VOLTRON_EXCLUDE
#include "utilities/size_utilities.h"
#include <string_view>
#include <fstream>
#endif // VOLTRON_EXCLUDE

#ifdef ENABLE_PERF_TRACES // VOLTRON_EXCLUDE
#include <cstdio>
#endif // VOLTRON_EXCLUDE

template <
            typename CentralHeapType,
            typename LocalHeapType,
            typename ArenaType = Arena<>
         >
class ScalableAllocator : public Lockable<LockPolicy::USERSPACE_LOCK>
{
public:

    // THIS CLASS IS INTENDED TO BE USED DIRECTLY IN MALLOC REPLACEMENTS
    // SINCE THIS ONE IS A TEMPLATE CLASS , WE HAVE TO ENSURE A SINGLE ONLY STATIC VARIABLE INITIALISATION
    static ScalableAllocator& get_instance()
    {
        static ScalableAllocator instance;
        return instance;
    }

    [[nodiscard]] bool create(const typename CentralHeapType::HeapCreationParams& params_central, const typename LocalHeapType::HeapCreationParams& params_local, std::size_t arena_capacity, std::size_t arena_page_alignment = 65536, std::size_t metadata_buffer_size = 131072)
    {
        if (arena_capacity <= 0 || arena_page_alignment <= 0 || metadata_buffer_size <= 0 || !MultipleUtilities::is_size_a_multiple_of_page_allocation_granularity(arena_page_alignment) || !MultipleUtilities::is_size_a_multiple_of_page_allocation_granularity(metadata_buffer_size))
        {
            return false;
        }

        if (m_objects_arena.create(arena_capacity, arena_page_alignment) == false)
        {
            return false;
        }

        m_metadata_buffer_size = metadata_buffer_size;
        m_metadata_buffer = reinterpret_cast<char*>(ArenaType::MetadataAllocator::allocate(m_metadata_buffer_size));

        if (m_metadata_buffer == nullptr)
        {
            return false;
        }

        if (m_central_heap.create(params_central, &m_objects_arena) == false)
        {
            return false;
        }

        if (ThreadLocalStorage::get_instance().create(ScalableAllocator::thread_specific_destructor) == false)
        {
            return false;
        }

        m_local_heap_creation_params = params_local;

        if (!create_heaps())
        {
            return false;
        }

        m_initialised_successfully.store(true);

        return true;
    }

    void set_thread_local_heap_cache_count(std::size_t count)
    {
        m_cached_thread_local_heap_count = count;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
    void* allocate(const std::size_t size)
    {
        #ifndef ENABLE_DEFAULT_MALLOC
        void* ret{ nullptr };
        auto local_heap = get_thread_local_heap();

        if (local_heap != nullptr)
        {
            ret = local_heap->allocate(size);
        }

        if (ret == nullptr)
        {
            #ifdef ENABLE_STATS
            m_central_heap_hit_count++;
            #endif

            #ifdef ENABLE_PERF_TRACES // INSIDE ALLOCATION CALLSTACK SO CAN'T ALLOCATE MEMORY HENCE OUTPUT TO stderr
            m_central_heap_hit_count++;
            fprintf(stderr, "scalable allocator , central heap hit count=%zu\n", m_central_heap_hit_count);
            #endif

            //If the local one is exhausted , failover to the central one
            ret = m_central_heap.allocate(size);
        }

        return ret;
        #else
        return builtin_aligned_alloc(size, AlignmentConstants::MINIMUM_VECTORISATION_WIDTH);
        #endif
    }

    ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
    void* allocate_aligned(std::size_t size, std::size_t alignment)
    {
        #ifndef ENABLE_DEFAULT_MALLOC
        void* ret{ nullptr };
        auto local_heap = get_thread_local_heap();

        if (local_heap != nullptr)
        {
            ret = local_heap->allocate_aligned(size, alignment);
        }

        if (ret == nullptr)
        {
            #ifdef ENABLE_STATS
            m_central_heap_hit_count++;
            #endif

            #ifdef ENABLE_PERF_TRACES // INSIDE ALLOCATION CALLSTACK SO CAN'T ALLOCATE MEMORY HENCE OUTPUT TO stderr
            m_central_heap_hit_count++;
            fprintf(stderr, "scalable allocator , central heap hit count=%zu\n", m_central_heap_hit_count);
            #endif

            //If the local one is exhausted , failover to the central one
            ret = m_central_heap.allocate_aligned(size, alignment);
        }

        return ret;
        #else
        return builtin_aligned_alloc(size, alignment);
        #endif
    }

    ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE)
    void deallocate(void* ptr)
    {
        if (unlikely(ptr == nullptr))
        {
            return;
        }
        #ifndef ENABLE_DEFAULT_MALLOC
        // LINEAR SEARCH HOWEVER owns_pointer CHECK IS FAST IN BOUNDED LOCAL HEAPS
        // THEY DON'T DO ANOTHER INTERNAL LINEAR SEARCH THROUGH FREELISTS
        // SO THERE IS NO NESTED LINEAR SEARCHES BUT JUST ONE
        for (std::size_t i = 0; i < m_active_local_heap_count; i++)
        {
            LocalHeapType* local_heap = reinterpret_cast<LocalHeapType*>(m_metadata_buffer + (i * sizeof(LocalHeapType)));

            if (local_heap->owns_pointer(ptr))
            {
                local_heap->deallocate(ptr);
                return;
            }
        }
        // If we are here, ptr belongs to the central heap
        m_central_heap.deallocate(ptr);
        #else
        builtin_aligned_free(ptr);
        #endif

    }

    std::size_t get_usable_size(void* ptr)
    {
        if (ptr == nullptr) return 0;

        for (std::size_t i = 0; i < m_active_local_heap_count; i++)
        {
            LocalHeapType* local_heap = reinterpret_cast<LocalHeapType*>(m_metadata_buffer + (i * sizeof(LocalHeapType)));

            if (local_heap->owns_pointer(ptr))
            {
                return local_heap->get_usable_size(ptr);
            }
        }
        // If we are here, ptr belongs to the central heap
        return m_central_heap.get_usable_size(ptr);
    }

    CentralHeapType* get_central_heap() { return &m_central_heap; }

    #ifdef UNIT_TEST
    std::size_t get_observed_unique_thread_count() const { return m_observed_unique_thread_count; }
    #endif

    #ifdef ENABLE_STATS
    bool save_stats_to_file(const std::string_view file_name)
    {
        std::ofstream outfile(file_name.data());

        if (outfile.is_open())
        {
            outfile << "Central heap hit count = " << m_central_heap_hit_count << "\n";
            outfile << "Created thread local heap count = " << m_used_thread_local_heap_count << "\n\n";

            auto arena_stats = m_objects_arena.get_stats();
            outfile << "Virtual memory latest usage = " << SizeUtilities::get_human_readible_size(arena_stats.m_latest_used_size) << "\n";
            outfile << "Virtual memory allocation count = " << arena_stats.m_vm_allocation_count << "\n";

            for(std::size_t i=0; i< arena_stats.m_vm_allocation_count; i++)
            {
                outfile << "\tVirtual memory allocation size = " << SizeUtilities::get_human_readible_size(arena_stats.m_vm_allocation_sizes[i]) << "\n";
            }

            auto process_heap_stats = [&] (LocalHeapType* heap, const char* name)
            {
                auto heap_segments_stats = heap->get_stats();
                auto heap_segment_count = heap_segments_stats.size();

                outfile << "--------------------------------------------------------------\n";
                outfile << name << "\n\n";

                for (std::size_t i = 0; i < heap_segment_count; i++)
                {
                    outfile << "Heap segment sizeclass = " << heap_segments_stats[i].m_size_class << "\n";
                    outfile << "\t\tLatest logical page count = " << heap_segments_stats[i].m_latest_logical_page_count << "\n";
                    outfile << "\t\tRecycle count = " << heap_segments_stats[i].m_recycle_count << "\n";
                    outfile << "\t\tGrow count = " << heap_segments_stats[i].m_grow_size_count << "\n";

                    for (std::size_t j = 0; j < heap_segments_stats[i].m_grow_size_count; j++)
                    {
                        outfile << "\t\t\t\tGrow size = " << heap_segments_stats[i].m_grow_sizes[j] << "\n";
                    }
                }

                outfile << "--------------------------------------------------------------\n\n";
            };


            process_heap_stats(reinterpret_cast<LocalHeapType*>(&m_central_heap), "CENTRAL HEAP");

            std::size_t local_heap_count = m_active_local_heap_count;

            if(m_cached_thread_local_heap_count > local_heap_count )
            {
                local_heap_count = m_cached_thread_local_heap_count;
            }

            for (std::size_t i = 0; i < local_heap_count; i++)
            {
                LocalHeapType* local_heap = reinterpret_cast<LocalHeapType*>(m_metadata_buffer + (i * sizeof(LocalHeapType)));
                process_heap_stats(local_heap, "LOCAL HEAP");
            }

            outfile.close();
            return true;
        }
        return false;
    }
    #endif

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    // WRAPPER METHODS FOR MALLOC REPLACEMENT/INTEGRATION
    [[nodiscard]] void* operator_new(std::size_t size)
    {
        void* ret = allocate(size);

        if( unlikely(ret==nullptr) )
        {
            handle_operator_new_failure();
        }

        return ret;
    }

    [[nodiscard]] void* operator_new_aligned(std::size_t size, std::size_t alignment)
    {
        void* ret = allocate_aligned(size, alignment);

        if( unlikely(ret==nullptr) )
        {
            handle_operator_new_failure();
        }

        return ret;
    }

    void handle_operator_new_failure()
    {
        std::new_handler handler;

        this->enter_concurrent_context();
        ///////////////////////////////////////
        handler = std::get_new_handler();
        ///////////////////////////////////////
        this->leave_concurrent_context();

        if(handler != nullptr)
        {
            handler();
        }
        else
        {
            throw std::bad_alloc();
        }
    }

    [[nodiscard]] void* allocate_and_zero_memory(std::size_t num, std::size_t size)
    {
        auto total_size = num * size;
        void* ret = allocate(total_size);

        if (ret != nullptr)
        {
            builtin_memset(ret, 0, total_size);
        }

        return ret;
    }

    [[nodiscard]] void* reallocate(void* ptr, std::size_t size)
    {
        if (ptr == nullptr)
            return  allocate(size);

        if (size == 0)
        {
            deallocate(ptr);
            return nullptr;
        }

        void* new_ptr = allocate(size);

        if (new_ptr != nullptr)
        {
            std::size_t old_size = get_usable_size(ptr);
            std::size_t copy_size = (old_size < size) ? old_size : size;
            builtin_memcpy(new_ptr, ptr, copy_size);
            deallocate(ptr);
        }

        return new_ptr;
    }

    [[nodiscard]] void* aligned_reallocate(void* ptr, std::size_t size, std::size_t alignment)
    {
        if (ptr == nullptr)
            return  allocate_aligned(size, alignment);


        if (size == 0)
        {
            deallocate(ptr);
            return nullptr;
        }

        void* new_ptr = allocate_aligned(size, alignment);

        if (new_ptr != nullptr)
        {
            std::size_t old_size = get_usable_size(ptr);
            std::size_t copy_size = (old_size < size) ? old_size : size;
            builtin_memcpy(new_ptr, ptr, copy_size);
            deallocate(ptr);
        }

        return new_ptr;
    }

    [[nodiscard]]void* reallocate_and_zero_memory(void *ptr, std::size_t num, std::size_t size)
    {
        auto total_size = num*size;
        auto ret = reallocate(ptr, total_size);

        if(ret != nullptr)
        {
            builtin_memset(ret, 0, total_size);
        }

        return ret;
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////

private:
    CentralHeapType m_central_heap;
    ArenaType m_objects_arena;
    char* m_metadata_buffer = nullptr;
    std::size_t m_metadata_buffer_size = 131072;       // Default 128KB
    std::size_t m_active_local_heap_count = 0;
    std::size_t m_max_thread_local_heap_count = 0;    // Used for only thread local heaps
    std::size_t m_cached_thread_local_heap_count = 0; // Used for only thread local heaps , its number of available passive heaps

    typename LocalHeapType::HeapCreationParams m_local_heap_creation_params;

    static inline std::atomic<bool> m_initialised_successfully = false;
    static inline std::atomic<bool> m_shutdown_started = false;

    #ifdef UNIT_TEST
    std::size_t m_observed_unique_thread_count = 0;
    #endif

    #ifdef ENABLE_STATS
    std::size_t m_central_heap_hit_count = 0;
    std::size_t m_used_thread_local_heap_count =0 ;
    #endif

    #ifdef ENABLE_PERF_TRACES
    std::size_t m_central_heap_hit_count = 0;
    #endif

    ScalableAllocator()
    {
        static_assert(std::is_base_of<HeapBase<CentralHeapType, ConcurrencyPolicy::CENTRAL>, CentralHeapType>::value);
        static_assert(std::is_base_of<HeapBase<LocalHeapType, ConcurrencyPolicy::THREAD_LOCAL>, LocalHeapType>::value || std::is_base_of<HeapBase<LocalHeapType, ConcurrencyPolicy::SINGLE_THREAD>, LocalHeapType>::value);
        static_assert(std::is_base_of<ArenaBase<ArenaType>, ArenaType>::value);
    }

    ~ScalableAllocator()
    {
        #ifdef ENABLE_STATS
        save_stats_to_file("metamalloc_stats.txt");
        #endif

        if(m_initialised_successfully.load() == true )
        {
            // We call it here it in case not called earlier and there are still running threads which are not destructed , no need to move logical pages between heaps
            m_shutdown_started.store(true);

            destroy_heaps();

            ThreadLocalStorage::get_instance().destroy();
        }
    }

    ScalableAllocator(const ScalableAllocator& other) = delete;
    ScalableAllocator& operator= (const ScalableAllocator& other) = delete;
    ScalableAllocator(ScalableAllocator&& other) = delete;
    ScalableAllocator& operator=(ScalableAllocator&& other) = delete;

    static void thread_specific_destructor(void* arg)
    {
        if( m_initialised_successfully.load() == true && m_shutdown_started.load() == false )
        {
            auto thread_local_heap = reinterpret_cast<LocalHeapType*>(arg);

            if(thread_local_heap) // Thread local arg is not supposed to be nullptr by OS specs but just to be safe
            {
                auto central_heap = get_instance().get_central_heap();
                central_heap->transfer_logical_pages_from(reinterpret_cast<CentralHeapType*>(thread_local_heap));
            }
        }
    }

    std::size_t get_created_heap_count()
    {
        auto heap_count = m_cached_thread_local_heap_count > m_active_local_heap_count ? m_cached_thread_local_heap_count : m_active_local_heap_count;
        return heap_count;
    }

    void destroy_heaps()
    {
        if (m_metadata_buffer)
        {
            auto heap_count = get_created_heap_count();

            for (std::size_t i = 0; i < heap_count; i++)
            {
                LocalHeapType* local_heap = reinterpret_cast<LocalHeapType*>(m_metadata_buffer + (i * sizeof(LocalHeapType)));
                local_heap->~LocalHeapType();
            }
        }
    }

    LocalHeapType* get_thread_local_heap()
    {
        return get_thread_local_heap_internal();
    }

    FORCE_INLINE LocalHeapType* get_thread_local_heap_internal()
    {
        auto thread_local_heap = reinterpret_cast<LocalHeapType*>(ThreadLocalStorage::get_instance().get());

        if (thread_local_heap == nullptr)
        {
            // LOCKING HERE WILL HAPPEN ONLY ONCE FOR EACH THREAD , AT THEIR START
            // AS THERE ARE SHARED VARIABLES FOR THREAD-LOCAL HEAP CREATION
            this->enter_concurrent_context();

            #ifdef UNIT_TEST
            m_observed_unique_thread_count++;
            #endif

            #ifdef ENABLE_STATS
            m_used_thread_local_heap_count++;
            #endif

            ///////////////////////////////////////////////////////////////////////////////////////////////////////////
            if (m_active_local_heap_count + 1 >= m_max_thread_local_heap_count)
            {
                // If we are here , it means that metadata buffer size is not sufficient to handle all threads of the application
                this->leave_concurrent_context();
                return nullptr;
            }

            if (m_active_local_heap_count >= m_cached_thread_local_heap_count)
            {
                thread_local_heap = create_local_heap(m_active_local_heap_count);
            }
            else
            {
                thread_local_heap = reinterpret_cast<LocalHeapType*>(m_metadata_buffer + (m_active_local_heap_count * sizeof(LocalHeapType)));
            }

            m_active_local_heap_count++;
            ThreadLocalStorage::get_instance().set(thread_local_heap);
            ///////////////////////////////////////////////////////////////////////////////////////////////////////////
            this->leave_concurrent_context();
        }

        return thread_local_heap;
    }

    bool create_heaps()
    {
        m_max_thread_local_heap_count = m_metadata_buffer_size / sizeof(LocalHeapType);

        if (m_max_thread_local_heap_count == 0)
        {
            return false;
        }

        if (m_max_thread_local_heap_count < m_cached_thread_local_heap_count)
        {
            m_cached_thread_local_heap_count = m_max_thread_local_heap_count;
        }

        for (std::size_t i{ 0 }; i < m_cached_thread_local_heap_count; i++)
        {
            auto local_heap = create_local_heap(i);
            if (!local_heap) return false;
        }

        return true;
    }

    LocalHeapType* create_local_heap(std::size_t metadata_buffer_index)
    {
        LocalHeapType* local_heap = new(m_metadata_buffer + (metadata_buffer_index * sizeof(LocalHeapType))) LocalHeapType();    // Placement new , does not invoke memory allocation

        if (local_heap->create(m_local_heap_creation_params, &m_objects_arena) == false)
        {
            return nullptr;
        }

        return local_heap;
    }
};

#endif