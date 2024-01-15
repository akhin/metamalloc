#include "../../metamalloc.h"
#include "../simple_heap_pow2.h"
using namespace metamalloc;

#include <iostream>
using namespace std;

int main()
{
    using ArenaType = Arena<LockPolicy::NO_LOCK>;
    ArenaType arena;
    bool success = arena.create(655360, 65536); // Size and alignment

    if(!success) {std::cout << "arena creation failed\n"; return -1;};

    using HeapType = SimpleHeapPow2<ConcurrencyPolicy::SINGLE_THREAD, ArenaType>;
    HeapType allocator;

    HeapType::HeapCreationParams params;
    params.m_small_object_logical_page_size = 65536;
    params.m_big_object_logical_page_size = 196608;
    params.m_big_object_page_recycling_threshold = 1;

    success = allocator.create(params, &arena);

    if(!success) {std::cout << "allocator creation failed\n"; return -1;};

    void * ptr = allocator.allocate(42);
    if(!ptr) {std::cout << "allocation failed\n"; return -1;};
    allocator.deallocate(ptr);

    void * ptr2 = allocator.allocate_aligned(512, AlignmentConstants::SIMD_AVX512_WIDTH);
    if(!ptr2) {std::cout << "allocation failed\n"; return -1;};
    allocator.deallocate(ptr2);

    return 0;
}