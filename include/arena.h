/*
    - ARENA ABSTRACTION REDUCES SYSCALLS BY CACHING AND SERVING VIRTUAL MEMORY PAGES.
      ( SOME ALLOCATORS LIKE JEMALLOC USES THE ARENA TERM DIFFERENTLY : AS THE HIGHEST LAYER ALLOCATOR WHICH OWNS MULTIPLE HEAPS.
        SHOULDN'T BE CONFUSED WITH THAT )

    - ARENA CLASS IS PAGE BASED ALLOCATOR AND USED TO ALLOCATE LOGICAL_PAGE BUFFERS WITHIN SEGMENTS.

    - IT RELEASES ONLY UNUSED PAGES. RELEASING USED PAGES IS UP TO THE CALLERS. ( THIS HELPS TO REDUCE METADATA USAGE BY AVOIDING A BITMAP )

    - IF HUGE PAGE IS SPECIFIED AND A HUGE PAGE ALLOCATION FAILS, FAILOVERS TO REGULAR PAGE ALLOCATION

    - SUPPORTS NO LOCKING AND LOCKING (EITHER OS_LOCK OR USERSPACE_SPINLOCK) SO THAT IT CAN BE USED BY A SINGLE HEAP OR SHARED BY MULTIPLE HEAPS

    - lock_pages & unlock_pages METHODS CAN BE USED SO THAT THE SYSTEM WILL NOT SWAP PAGES TO THE PAGING FILE

    - LINUX ALLOCATION GRANULARITY IS 4KB (4096) , OTH IT IS 64KB ( 16 * 4096 ) ON WINDOWS .
      REGARDING WINDOWS PAGE ALLOCATION GRANULARITY : https://devblogs.microsoft.com/oldnewthing/20031008-00/?p=42223
      FOR THEIR ALIGNMENTS SEE COMMENTS IN ARENA_BASE.H.
*/
#ifndef _ARENA_H_
#define _ARENA_H_

#include <cstddef>
#include <cstdint>

#include "os/virtual_memory.h"

#include "utilities/lockable.h"
#include "utilities/multiple_utilities.h"
#include "arena_base.h"

#ifdef UNIT_TEST // VOLTRON_EXCLUDE
#include <string>
#endif // VOLTRON_EXCLUDE

enum class VirtualMemoryPolicy
{
    DEFAULT,
    HUGE_PAGE,                // MAY BE MORE LOAD TO THE SYSTEM BUT REDUCES TLB MISSES AND MAKE VIRTUAL MEM PAGE MANAGEMENT LESS EXPENSIVE
    #ifdef UNIT_TEST
    OUT_OF_MEMORY            // FOR UNIT TESTS : TESTS THE CASE WHEN THERE IS NO AVAILABLE SYSTEM MEMORY
    #endif

};

#ifdef ENABLE_STATS
#include <array>
constexpr static inline std::size_t MAX_ALLOC_STAT_COUNT = 32;
struct ArenaStats
{
    std::size_t m_vm_allocation_count = 0;
    std::array<std::size_t, MAX_ALLOC_STAT_COUNT> m_vm_allocation_sizes = { 0 };
    std::size_t m_latest_used_size = 0;
};
#endif

#ifdef ENABLE_PERF_TRACES // VOLTRON_EXCLUDE
#include <cstdio>
#endif // VOLTRON_EXCLUDE

// MAINTAINS A SHARED CACHE THEREFORE LOCKED BY DEFAULT
template <LockPolicy lock_policy = LockPolicy::USERSPACE_LOCK, VirtualMemoryPolicy virtual_memory_policy = VirtualMemoryPolicy::DEFAULT, std::size_t numa_node = VirtualMemory::NO_NUMA, bool zero_memory = false>
class Arena : public Lockable<lock_policy>, public ArenaBase<Arena<lock_policy, virtual_memory_policy, numa_node, zero_memory>>
{
    public:

        Arena()
        {
            m_vm_page_size = VirtualMemory::get_page_size(); // DEFAULT VALUE
            m_page_alignment = VirtualMemory::PAGE_ALLOCATION_GRANULARITY;
        }

        ~Arena()
        {
            destroy();
        }

        Arena(const Arena& other) = delete;
        Arena& operator= (const Arena& other) = delete;
        Arena(Arena&& other) = delete;
        Arena& operator=(Arena&& other) = delete;

        [[nodiscard]] bool create(std::size_t cache_capacity, std::size_t page_alignment)
        {
            if (MultipleUtilities::is_size_a_multiple_of_page_allocation_granularity(m_page_alignment) == false)
            {
                return false;
            }

            this->enter_concurrent_context();
            //////////////////////////////////////////////////
            m_page_alignment = page_alignment;
            auto ret =  build_cache(cache_capacity);
            //////////////////////////////////////////////////
            this->leave_concurrent_context();

            return ret;
        }

        void destroy()
        {
            if (m_cache_size > m_cache_used_size)
            {
                // ARENA IS RESPONSIBLE OF CLEARING ONLY NEVER-REQUESTED PAGES.
                std::size_t release_start_address = reinterpret_cast<std::size_t>(m_cache_buffer + m_cache_used_size);
                std::size_t release_end_address = reinterpret_cast<std::size_t>(m_cache_buffer + m_cache_size);

                for (; release_start_address < release_end_address; release_start_address += m_vm_page_size)
                {
                    release_to_system(reinterpret_cast<void *>(release_start_address), m_vm_page_size);
                }

            }
            m_cache_size = 0;
            m_cache_used_size = 0;
            m_cache_buffer = nullptr;
        }

        [[nodiscard]] char* allocate(std::size_t size)
        {
            this->enter_concurrent_context();
            //////////////////////////////////////////////////
            if (size + m_page_alignment > (m_cache_size - m_cache_used_size))
            {
                destroy();

                if (!build_cache(size))
                {
                    return nullptr;
                }
            }

            auto ret = m_cache_buffer + m_cache_used_size;
            m_cache_used_size += size;
            //////////////////////////////////////////////////
            this->leave_concurrent_context();

            return ret;
        }

        std::size_t page_size()const { return m_vm_page_size; }
        std::size_t page_alignment() const { return m_page_alignment; }

        void lock_pages()
        {
            uint64_t address = reinterpret_cast<uint64_t>(m_cache_buffer);
            uint64_t end_address = reinterpret_cast<uint64_t>(m_cache_buffer + m_cache_size);

            for (; address < end_address; address += m_vm_page_size)
            {
                VirtualMemory::lock(reinterpret_cast<void*>(address), m_vm_page_size);
            }
        }

        void unlock_pages()
        {
            uint64_t address = reinterpret_cast<uint64_t>(m_cache_buffer);
            uint64_t end_address = reinterpret_cast<uint64_t>(m_cache_buffer + m_cache_size);

            for (; address < end_address; address += m_vm_page_size)
            {
                VirtualMemory::unlock(reinterpret_cast<void*>(address), m_vm_page_size);
            }
        }

        void* allocate_from_system(std::size_t size)
        {
            void* ret = nullptr;

            if constexpr (virtual_memory_policy == VirtualMemoryPolicy::DEFAULT)
            {
                ret = static_cast<char*>(VirtualMemory::allocate<false, numa_node, zero_memory>(size, nullptr));
            }
            else if  constexpr (virtual_memory_policy == VirtualMemoryPolicy::HUGE_PAGE)
            {
                ret = static_cast<char*>(VirtualMemory::allocate<true, numa_node, zero_memory>(size, nullptr));

                // If huge page fails, try regular ones
                if (ret == nullptr)
                {
                    ret = static_cast<char*>(VirtualMemory::allocate<false, numa_node, zero_memory>(size, nullptr));
                }
            }

            return ret;
        }

        void release_to_system(void* address, std::size_t size)
        {
            VirtualMemory::deallocate(address, size);
        }

        class MetadataAllocator
        {
            public:
                static void* allocate(std::size_t size, void* hint_address = nullptr)
                {
                    return VirtualMemory::allocate<false>(size, hint_address); // No hugepage, no NUMA and no zeroing
                }

                static void deallocate(void* address, std::size_t size)
                {
                    VirtualMemory::deallocate(address, size);
                }
        };

        #ifdef ENABLE_STATS
        ArenaStats get_stats() { m_stats.m_latest_used_size = m_cache_used_size;  return m_stats; }
        #endif

    private:
        std::size_t m_vm_page_size = 0;
        std::size_t m_page_alignment = 0;
        char* m_cache_buffer = nullptr;
        std::size_t m_cache_size = 0;
        std::size_t m_cache_used_size = 0;

        #ifdef ENABLE_STATS
        ArenaStats m_stats;
        #endif

        [[nodiscard]] bool build_cache(std::size_t size)
        {
            char* buffer = this->allocate_aligned(size, m_page_alignment);

            if (buffer == nullptr)
            {
                return false;
            }

            #ifdef ENABLE_PERF_TRACES // INSIDE ALLOCATION CALLSTACK SO CAN'T ALLOCATE MEMORY HENCE OUTPUT TO stderr
            fprintf(stderr, "arena build cache virtual memory allocation , size=%zu\n", size);
            #endif

            #ifdef ENABLE_STATS
            if(m_stats.m_vm_allocation_count<MAX_ALLOC_STAT_COUNT)
            {
                m_stats.m_vm_allocation_sizes[m_stats.m_vm_allocation_count] = size;
                m_stats.m_vm_allocation_count++;
            }
            #endif

            m_cache_buffer = buffer;
            m_cache_used_size = 0;
            m_cache_size = size;

            return true;
        }
};

#endif