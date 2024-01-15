#include "../../metamalloc.h"
#include "../simple_heap_pow2.h"
using namespace metamalloc;

#include <cstddef>
#include <vector>
#include <iostream>

using namespace std;

using CentralHeapType = SimpleHeapPow2<ConcurrencyPolicy::CENTRAL>;
using LocalHeapType = SimpleHeapPow2<ConcurrencyPolicy::THREAD_LOCAL>;

using AllocatorType = ScalableAllocator<
    CentralHeapType,
    LocalHeapType
>;

template <class T>
class MetamallocSTLAllocator
{
    public:
        using value_type = T;

        MetamallocSTLAllocator() {}

        template <class U>
        MetamallocSTLAllocator(const MetamallocSTLAllocator<U>&) {}

        T* allocate(const std::size_t n)
        {
            T* ret = reinterpret_cast<T*>(AllocatorType::get_instance().allocate(n * sizeof(T)));
            return ret;
        }

        void deallocate(T* const p, const std::size_t n)
        {
            AllocatorType::get_instance().deallocate(p);
        }
};

int main()
{
    CentralHeapType::HeapCreationParams params_central;
    LocalHeapType::HeapCreationParams params_local;
    constexpr std::size_t ARENA_CAPACITY = 1024*1024*64;

    bool success = AllocatorType::get_instance().create(params_central, params_local, ARENA_CAPACITY);

    if (!success)
    {
        cout << "Allocator creation failed !!!" << endl;
        return -1;
    }

    std::vector<std::size_t, MetamallocSTLAllocator<std::size_t>> vector;

    for (std::size_t i = 8; i < 4096; i++)
    {
        vector.push_back(i);
    }

    return 0;
}