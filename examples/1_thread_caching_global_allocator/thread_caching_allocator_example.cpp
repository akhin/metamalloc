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
    constexpr std::size_t ARENA_CAPACITY = 1024 * 1024 * 16; // 16 MB

    bool success = AllocatorType::get_instance().create(params_central, params_local, ARENA_CAPACITY);

    if (!success)
    {
        cout << "Allocator creation failed !!!" << endl;
        return -1;
    }

    void* ptr = nullptr;
    ptr = AllocatorType::get_instance().allocate(42);
    AllocatorType::get_instance().deallocate(ptr);

    void*ptr2 = nullptr;
    ptr2 = AllocatorType::get_instance().allocate_aligned(512, AlignmentConstants::SIMD_AVX512_WIDTH);
    AllocatorType::get_instance().deallocate(ptr2);

    return 0;
}