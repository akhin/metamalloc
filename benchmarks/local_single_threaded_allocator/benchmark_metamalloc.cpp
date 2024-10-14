#include <metamalloc.h>
#include <simple_heap_pow2.h>
#include <iostream>
#include <stdexcept>
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
            bool success = m_arena.create(2147483648, 65536);

            if (success == false)
            {
                throw std::runtime_error("Initialisation failed");
            }

            HeapType::HeapCreationParams params;
            params.m_logical_page_size = 65536;
            params.m_logical_page_recycling_threshold = 4;

            params.m_bin_logical_page_counts[0]=1000;
            params.m_bin_logical_page_counts[1]=1000;
            params.m_bin_logical_page_counts[2]=1000;
            params.m_bin_logical_page_counts[3]=1000;
            params.m_bin_logical_page_counts[4]=1000;
            params.m_bin_logical_page_counts[5]=1000;
            params.m_bin_logical_page_counts[6]=1000;
            params.m_bin_logical_page_counts[7]=1000;

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
    try
    {
        run_allocator_benchmark<MetamallocLocalAllocator>();
    }
    catch (const std::runtime_error& ex)
    {
        std::cout << ex.what();
    }

    #if _WIN32
    std::system("pause");
    #endif

    return 0;
}