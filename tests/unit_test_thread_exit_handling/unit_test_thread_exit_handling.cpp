#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro


#include "../../examples/simple_heap_pow2.h"
using namespace metamalloc;

#include <vector>
#include <memory>
#include <thread>
#include <cstring>
#include <iostream>
using namespace std;

using CentralHeapType = SimpleHeapPow2<ConcurrencyPolicy::CENTRAL>;
using LocalHeapType = SimpleHeapPow2<ConcurrencyPolicy::THREAD_LOCAL>;

using AllocatorType =
ScalableAllocator<
        CentralHeapType,
        LocalHeapType
>;

UnitTest unit_test;

int main(int argc, char* argv[])
{
    constexpr std::size_t ARENA_CAPACITY = 2147483648; // 2GB

    CentralHeapType::HeapCreationParams params_central;
    LocalHeapType::HeapCreationParams params_local;

    AllocatorType::get_instance().set_thread_local_heap_cache_count(1);
    bool success = AllocatorType::get_instance().create(params_central, params_local, ARENA_CAPACITY);
    if (!success) { std::cout << "Creation failed !!!\n"; }

    auto thread_function = [&](unsigned int cpu_id)
    {
        auto ptr = AllocatorType::get_instance().allocate(5);
        UNUSED(ptr);
    };

    auto central_heap = AllocatorType::get_instance().get_central_heap();

    unit_test.test_equals(central_heap->get_bin_logical_page_count(11), 1, "thread exit handling", "logical page count before transfer");

    std::vector<std::unique_ptr<std::thread>> threads;
    threads.emplace_back(new std::thread(thread_function, 0));

    for (auto& thread : threads)
    {
        thread->join();
    }

    unit_test.test_equals(central_heap->get_bin_logical_page_count(11), 2, "thread exit handling", "logical page count after transfer");

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("ThreadExitHandling");
    std::cout.flush();
    
    #if _WIN32
    bool pause = true;
    if(argc > 1)
    {
        if (std::strcmp(argv[1], "no_pause") == 0)
            pause = false;
    }
    if(pause)
        std::system("pause");
    #endif

    return unit_test.did_all_pass();
}