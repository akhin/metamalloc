#include <metamalloc.h>
#include <simple_heap_pow2.h>
#include <iostream>
#include <stdexcept>
#include "benchmark.h"

class MetamallocLocalAllocatorWithHugePage
{
    public:

        using ArenaType = Arena<LockPolicy::NO_LOCK, VirtualMemoryPolicy::HUGE_PAGE>;
        using HeapType = SimpleHeapPow2<
                        ConcurrencyPolicy::SINGLE_THREAD,
                        ArenaType,
                        PageRecyclingPolicy::DEFERRED>;

        MetamallocLocalAllocatorWithHugePage()
        {
            auto huge_page_size = VirtualMemory::get_huge_page_size();
            bool success = m_arena.create(2147483648, huge_page_size);

            if (success == false)
            {
                throw std::runtime_error("Initialisation failed");
            }

            HeapType::HeapCreationParams params;
            params.m_logical_page_size = huge_page_size;
            params.m_logical_page_recycling_threshold = 4;

            params.m_bin_logical_page_counts[0]=1;
            params.m_bin_logical_page_counts[1]=1;
            params.m_bin_logical_page_counts[2]=1;
            params.m_bin_logical_page_counts[3]=1;
            params.m_bin_logical_page_counts[4]=1;
            params.m_bin_logical_page_counts[5]=1;
            params.m_bin_logical_page_counts[6]=1;
            params.m_bin_logical_page_counts[7]=1;


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

        const char* title() const{ return "Metamalloc with huge page";}
    private :
        ArenaType m_arena;
        HeapType m_allocator;
};

int main ()
{
    if (VirtualMemory::is_huge_page_available() == false)
    {
        #ifdef __linux__
        std::cout << "Huge page not available. Try to run \"echo 20 | sudo tee /proc/sys/vm/nr_hugepages\" \n";
        #else
        std::cout << "Huge page not available. You need to enable it using gpedit.msc\n";
        #endif
        return -1;
    }

    auto huge_page_size = VirtualMemory::get_huge_page_size();
    std::cout << "Available huge page size = " <<  huge_page_size << " bytes\n\n";

    try
    {
        run_allocator_benchmark<MetamallocLocalAllocatorWithHugePage>();
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