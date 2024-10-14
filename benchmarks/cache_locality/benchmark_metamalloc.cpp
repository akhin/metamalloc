#include <metamalloc.h>
#include <simple_heap_pow2.h>
#include <iostream>
#include "benchmark.h"

class MetamallocLocalAllocator
{
    public:

        using HeapType = SimpleHeapPow2<
                        ConcurrencyPolicy::SINGLE_THREAD,
                        Arena<LockPolicy::NO_LOCK>,
                        PageRecyclingPolicy::DEFERRED>;

        MetamallocLocalAllocator()
        {
            bool success = m_arena.create(134217728, 65536);
            HeapType::HeapCreationParams params;
            params.m_logical_page_size = 65536;
            params.m_logical_page_recycling_threshold = 4;

            params.m_bin_logical_page_counts[0] = 1;
            params.m_bin_logical_page_counts[1] = 500;
            params.m_bin_logical_page_counts[2] = 1;
            params.m_bin_logical_page_counts[3] = 1;
            params.m_bin_logical_page_counts[4] = 1;
            params.m_bin_logical_page_counts[5] = 1;
            params.m_bin_logical_page_counts[6] = 1;
            params.m_bin_logical_page_counts[7] = 1;

            success = m_allocator.create(params, &m_arena);

            if (success == false)
            {
                throw std::runtime_error("Initialisation failed");
            }
        }

        void* allocate(std::size_t size)
        {
            return m_allocator.allocate(size);
        }

        void deallocate(void* ptr)
        {
            m_allocator.deallocate(ptr);
        }

        const char* title() const{ return "Metamalloc";}
    private :
        Arena<LockPolicy::NO_LOCK> m_arena;
        HeapType m_allocator;
};

int main ()
{
    if(geteuid() != 0)
    {
        std::cout << "You need to run this benchmark with sudo as we are accessing PMC to measure cache misses." << std::endl;
        return -1;
    }

    try
    {
        run_allocator_benchmark<MetamallocLocalAllocator>();
    }
    catch (const std::runtime_error& ex)
    {
        std::cout << ex.what();
    }

    return 0;
}