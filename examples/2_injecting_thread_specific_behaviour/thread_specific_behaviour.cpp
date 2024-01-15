#include "../../metamalloc.h"
#include "../simple_heap_pow2.h"
using namespace metamalloc;

#include <iostream>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include <thread>
#include <string>

using namespace std;

struct Allocation
{
    void* ptr = nullptr;
    uint16_t size_class = 0;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ALLOCATOR FOR ALL THREADS ( EXCEPT THE SPECIAL ONE )

using CentralHeapType = SimpleHeapPow2<ConcurrencyPolicy::CENTRAL>;
using LocalHeapType   = SimpleHeapPow2<ConcurrencyPolicy::THREAD_LOCAL>;

using AllocatorType = ScalableAllocator<
                                        CentralHeapType,
                                        LocalHeapType
                                       >;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SPECIAL THREAD
uint64_t special_thread_id = 0;

Arena<> special_thread_arena;

using SpecialHeapType = SimpleHeapPow2<ConcurrencyPolicy::SINGLE_THREAD>;
SpecialHeapType special_thread_heap;

template<>
LocalHeapType* AllocatorType::get_thread_local_heap()
{
    auto tls_id = ThreadLocalStorage::get_thread_local_storage_id();

    if (tls_id == special_thread_id)
    {
        return reinterpret_cast<LocalHeapType*>(&special_thread_heap);
    }

    return AllocatorType::get_thread_local_heap_internal();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main()
{
    CentralHeapType::HeapCreationParams params_central;
    LocalHeapType::HeapCreationParams params_local;
    constexpr std::size_t ARENA_CAPACITY = 16777216;

    bool success = AllocatorType::get_instance().create(params_central, params_local, ARENA_CAPACITY);

    if (!success) { cout << "Allocator creation failed !!!" << endl; return -1; }

    success = special_thread_arena.create(65536, 65536);
    if (!success) { cout << "Special heap arena creation failed !!!" << endl; return -1; }

    SpecialHeapType::HeapCreationParams params_special;
    success = special_thread_heap.create(params_special, &special_thread_arena);
    if (!success) { cout << "Special heap creation failed !!!" << endl; return -1; }

    std::vector<Allocation> allocations;
    std::mutex allocations_mutex; // For shared allocations vector

    std::vector<Allocation> allocations_special;

    auto thread_count = 4;
    auto allocation_per_thread_count = 32;
    auto allocation_size = 8192;

    auto allocating_thread_function = [&](unsigned int thread_id, bool is_special_thread = false)
    {
        if (is_special_thread)
        {
            special_thread_id = ThreadLocalStorage::get_thread_local_storage_id();
        }

        ThreadUtilities::set_thread_name(ThreadUtilities::get_current_thread_id(), "THREAD_" + std::to_string(thread_id));

        for (auto i{ 0 }; i < allocation_per_thread_count; i++)
        {
            void* ptr = nullptr;
            ptr = AllocatorType::get_instance().allocate(allocation_size);

            Allocation allocation;
            allocation.ptr = ptr;
            allocation.size_class = static_cast<uint16_t>(allocation_size);

            if (is_special_thread==false)
            {
                std::lock_guard<std::mutex> lock(allocations_mutex);
                allocations.push_back(allocation);
            }
            else
            {
                allocations_special.push_back(allocation);
            }
        }
    };

    std::vector<std::unique_ptr<std::thread>> allocating_threads;

    for (auto i{ 0 }; i < thread_count; i++)
    {
        if (i == 0) // First one is our sprecial thread
        {
            allocating_threads.emplace_back(new std::thread(allocating_thread_function, i, true));
        }
        else
        {
            allocating_threads.emplace_back(new std::thread(allocating_thread_function, i));
        }
    }

    for (auto& thread : allocating_threads)
    {
        thread->join();
    }

    for (auto& allocation : allocations)
    {
        AllocatorType::get_instance().deallocate(allocation.ptr);
    }

    for (auto& allocation : allocations_special)
    {
        special_thread_heap.deallocate(allocation.ptr);
    }

    return 0;
};