#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include "../../src/utilities/alignment_checks.h"
#include "../../src/arena.h"
#include "../../src/heap_base.h"

#include <cstdlib>
#include <vector>
#include <iostream>
using namespace std;

UnitTest unit_test;

inline bool validate_buffer(void* buffer, std::size_t buffer_size)
{
    char* char_buffer = static_cast<char*>(buffer);

    // TRY WRITING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        char* dest = char_buffer + i;
        *dest = static_cast<char>(i);
    }

    // NOW CHECK READING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        auto test = char_buffer[i];
        if (test != static_cast<char>(i))
        {
            return false;
        }
    }

    return true;
}

template <typename HeapType>
bool validate_aligned_allocation(HeapType* heap, std::size_t allocation_size, std::size_t alignment, std::size_t repeat_count = 1)
{
    for (std::size_t i = 0; i < repeat_count; i++)
    {
        void* ptr = nullptr;
        ptr = heap->allocate_aligned(allocation_size, alignment);

        if (ptr == nullptr)
        {
            std::cout << "ALLOCATION FAILED !!!" << std::endl;
            return false;
        }
        else
        {
            bool buffer_ok = validate_buffer(reinterpret_cast<void*>(ptr), allocation_size);

            if (buffer_ok == false)
            {
                std::cout << "BUFFER VALIDATION FAILED" << std::endl;
                return false;
            }

            bool alignment_ok = AlignmentChecks::is_address_aligned(ptr, alignment);

            if (alignment_ok == false)
            {
                std::cout << "ALIGNMENT CHECK FAILED" << std::endl;
                return false;
            }
        }
    }

    return true;
}

struct DummyHeapCreationsParams
{
    };

template <typename ArenaType, typename DummyHeapCreationsParamsType>
class DummyHeap : public HeapBase<DummyHeap<ArenaType, DummyHeapCreationsParamsType>>
{
    public:
        bool create(const DummyHeapCreationsParamsType& params, ArenaType* arena_ptr) { return true; }

        void* allocate(std::size_t size)
        {
            return std::malloc(size);
        }

    private:
};

int main()
{
    DummyHeap<Arena<>, DummyHeapCreationsParams> heap;
    Arena<> arena;
    DummyHeapCreationsParams params;


    bool success = heap.create(params, &arena);
    unit_test.test_equals(success, true, "heap_base", "concrete heap creation success");

    struct aligned_allocation
    {
        std::size_t size = 0;
        std::size_t alignment = 0;
    };

    std::vector<aligned_allocation> aligned_allocations = { {17,32}, {23,128}, {1025, 64} };

    for (const auto& allocation : aligned_allocations)
    {
        if (validate_aligned_allocation(&heap, allocation.size, allocation.alignment) == false) { return -1; }
    }

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("HeapBase");

    #if _WIN32
    std::system("pause");
    #endif

    return unit_test.did_all_pass();
}