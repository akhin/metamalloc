#define ENABLE_REPORT_LEAKS        // Need this before metamalloc.h inclusion

#include "../../metamalloc.h"
#include "../simple_heap_pow2.h"
using namespace metamalloc;

#include <iostream>
#include <cstddef>

using namespace std;

using CentralHeapType = SimpleHeapPow2<ConcurrencyPolicy::CENTRAL>;
using LocalHeapType = SimpleHeapPow2<ConcurrencyPolicy::THREAD_LOCAL>;

using AllocatorType = ScalableAllocator<
    CentralHeapType,
    LocalHeapType
>;

int main()
{
    CentralHeapType::HeapCreationParams params_central;
    LocalHeapType::HeapCreationParams params_local;
    constexpr std::size_t ARENA_CAPACITY = 1024 * 1024 * 16;

    bool success = AllocatorType::get_instance().create(params_central, params_local, ARENA_CAPACITY);

    if (!success)
    {
        cout << "Allocator creation failed !!!" << endl;
        return -1;
    }

    void* ptr = nullptr;
    ptr = AllocatorType::get_instance().allocate(42);
    UNUSED(ptr);

    // Creating a leak as we don't deallocate
    // AllocatorType::get_instance().deallocate(ptr);

    // The leak report will be printed to the console at the end of process

    return 0;
}