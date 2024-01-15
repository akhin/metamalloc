#define UNIT_TEST
#include "../../metamalloc.h"
using namespace metamalloc;

#include "../../tests/unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro
#include "../../examples/simple_heap_pow2.h"

#include <iostream>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace std;

UnitTest unit_test;

struct Allocation
{
    void* ptr = nullptr;
    uint16_t size_class = 0;
};

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
bool validate_allocation(HeapType* heap, std::size_t allocation_size, std::size_t logical_page_size, std::vector<Allocation>& pointer_vector, std::size_t repeat_count = 1)
{
    for (std::size_t i = 0; i < repeat_count; i++)
    {
        void* ptr = nullptr;
        ptr = heap->allocate(allocation_size);

        if (ptr == nullptr)
        {
            std::cout << "ALLOCATION FAILED !!!" << std::endl;
            return false;
        }
        else
        {
            if (allocation_size <= 2048)
            {
                auto size_adjusted = heap->adjust_size_for_small_object_bin(allocation_size);
                bool is_page_header_good = size_adjusted == HeapType::SegmentSmallObject::get_size_class_from_address(ptr, logical_page_size);

                if (is_page_header_good == false)
                {
                    std::cout << "PAGE HEADER VALIDATION FAILED" << std::endl;
                    return false;

                }
            }

            bool buffer_ok = validate_buffer(reinterpret_cast<void*>(ptr), allocation_size);

            if (buffer_ok == false)
            {
                std::cout << "BUFFER VALIDATION FAILED" << std::endl;
                return false;

            }
            Allocation allocation;
            allocation.ptr = ptr;
            allocation.size_class = static_cast<uint16_t>(allocation_size);
            pointer_vector.push_back(allocation);
        }
    }

    return true;
}

template <typename HeapType>
bool validate_aligned_allocation(HeapType* heap, std::size_t allocation_size, std::size_t alignment, std::vector<Allocation>& pointer_vector, std::size_t repeat_count = 1)
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

            Allocation allocation;
            allocation.ptr = ptr;
            allocation.size_class = static_cast<uint16_t>(allocation_size);
            pointer_vector.push_back(allocation);
        }
    }

    return true;
}

int main()
{
    std::vector<std::size_t> size_classes = { 16,32,64,128,256,512,1024,2048 };

    ///////////////////////////////////////////////////////////////////////////////////////
    // HEAP CREATION FAILURE
    {
        SimpleHeapPow2<> heap;
        Arena<> arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }
        SimpleHeapPow2<>::HeapCreationParams params;
        params.m_small_object_logical_page_size = 65500;
        params.m_big_object_logical_page_size = 65536;
        success = heap.create(params, &arena);
        unit_test.test_equals(success, false, "heap pow 2", "creation failure due to incorrect parameters"); // 65500 is incorrect as it is not a multiple of 4096, and if this is for Windows it is also less than min page alloc granularity (65536)
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    // HEAP BIN INDEX FINDING
    {
        auto bin16_index = SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(16);
        auto bin32_index = SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(32);
        auto bin64_index = SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(64);
        auto bin128_index = SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(128);
        auto bin256_index = SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(256);
        auto bin512_index = SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(512);
        auto bin1024_index = SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(1024);
        auto bin2048_index = SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(2048);

        unit_test.test_equals(bin16_index, 0, "heap pow 2", "bin index finding for bin16");
        unit_test.test_equals(bin32_index, 1, "heap pow 2", "bin index finding for bin32");
        unit_test.test_equals(bin64_index, 2, "heap pow 2", "bin index finding for bin64");
        unit_test.test_equals(bin128_index, 3, "heap pow 2", "bin index finding for bin128");
        unit_test.test_equals(bin256_index, 4, "heap pow 2", "bin index finding for bin256");
        unit_test.test_equals(bin512_index, 5, "heap pow 2", "bin index finding for bin512");
        unit_test.test_equals(bin1024_index, 6, "heap pow 2", "bin index finding for bin1024");
        unit_test.test_equals(bin2048_index, 7, "heap pow 2", "bin index finding for bin2048");
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    // HEAP GENERAL TESTS , WHEN SMALL OBJECT LOGICAL PAGE SIZE IS 64KB
    {
        SimpleHeapPow2<> heap;
        Arena<> arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }
        SimpleHeapPow2<>::HeapCreationParams params;
        params.m_small_object_logical_page_size = 65536;
        params.m_big_object_logical_page_size = 655360;
        params.m_big_object_page_recycling_threshold = 4;
        params.m_segment_grow_coefficient = 0;

        std::size_t small_object_capacity_per_size_class = 1024 * 16;
        params.m_small_object_page_recycling_threshold = small_object_capacity_per_size_class * 2;
        auto counter = 0;
        for (const auto& size_class : size_classes)
        {
            params.m_small_object_bin_page_counts[counter] = (small_object_capacity_per_size_class * size_class) / 65536;
            counter++;
        }

        success = heap.create(params, &arena);
        if (!success) { std::cout << "HEAP CREATION FAILED !!!" << std::endl; return -1; }

        std::size_t big_object_allocation_size = 65536 - sizeof(LogicalPageAnySize<>);

        // ALLOCATING BIG OBJECTS
        std::vector<Allocation> big_object_allocations;
        for (std::size_t i = 0; i < 10; i++)
        {
            bool success = validate_allocation(&heap, big_object_allocation_size, params.m_big_object_logical_page_size, big_object_allocations);

            if (!success)
            {
                return -1;
            }
        }

        unit_test.test_equals(big_object_allocations.size(), 10, "heap pow 2", "verifying big object allocation count");

        unit_test.test_equals(heap.get_big_object_bin_page_count(), 1, "heap pow 2", "BigObject bin logical page count");
        if (validate_allocation(&heap, 32768, params.m_big_object_logical_page_size, big_object_allocations) == false) return -1; // triggering segment grow
        unit_test.test_equals(heap.get_big_object_bin_page_count(), 2, "heap pow 2", "BigObject bin logical page count post grow");

        // ALLOCATING SMALL OBJECTS

        std::vector<Allocation> small_object_allocations;

        for (const auto& size_class : size_classes)
        {
            for (std::size_t i = 0; i < small_object_capacity_per_size_class; ++i)
            {
                bool success = validate_allocation(&heap, size_class, params.m_small_object_logical_page_size, small_object_allocations);

                if (!success)
                {
                    return -1;
                }
            }
        }

        unit_test.test_equals(small_object_allocations.size(), small_object_capacity_per_size_class * size_classes.size(), "heap pow 2", "verifying small object allocation count");

        // VERIFY SMALL OBJECT LOGICAL PAGE COUNTS BEFORE GROWING IN SIZE
        unit_test.test_equals(heap.get_small_object_bin_page_count(0), 5, "heap pow 2", "Bin16   logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(1), 9, "heap pow 2", "Bin32   logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(2), 17, "heap pow 2", "Bin64   logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(3), 33, "heap pow 2", "Bin128  logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(4), 65, "heap pow 2", "Bin256  logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(5), 130, "heap pow 2", "Bin512  logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(6), 261, "heap pow 2", "Bin1024 logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(7), 529, "heap pow 2", "Bin2048 logical page count");

        // TRIGGER GROWING IN SMALL OBJECT BINS THEN VALIDATE LOGICAL PAGE COUNTS
        if (validate_allocation(&heap, 16, params.m_small_object_logical_page_size, small_object_allocations, small_object_capacity_per_size_class) == false) return -1;
        if (validate_allocation(&heap, 32, params.m_small_object_logical_page_size, small_object_allocations, small_object_capacity_per_size_class) == false) return -1;
        if (validate_allocation(&heap, 64, params.m_small_object_logical_page_size, small_object_allocations, small_object_capacity_per_size_class) == false) return -1;
        if (validate_allocation(&heap, 128, params.m_small_object_logical_page_size, small_object_allocations, small_object_capacity_per_size_class) == false) return -1;
        if (validate_allocation(&heap, 256, params.m_small_object_logical_page_size, small_object_allocations, small_object_capacity_per_size_class) == false) return -1;
        if (validate_allocation(&heap, 512, params.m_small_object_logical_page_size, small_object_allocations, small_object_capacity_per_size_class) == false) return -1;
        if (validate_allocation(&heap, 1024, params.m_small_object_logical_page_size, small_object_allocations, small_object_capacity_per_size_class) == false) return -1;
        if (validate_allocation(&heap, 2048, params.m_small_object_logical_page_size, small_object_allocations, small_object_capacity_per_size_class) == false) return -1;

        unit_test.test_equals(heap.get_small_object_bin_page_count(0), 9, "heap pow 2", "Bin16   logical page coun post growth");
        unit_test.test_equals(heap.get_small_object_bin_page_count(1), 17, "heap pow 2", "Bin32   logical page count post growth");
        unit_test.test_equals(heap.get_small_object_bin_page_count(2), 33, "heap pow 2", "Bin64   logical page count post growth");
        unit_test.test_equals(heap.get_small_object_bin_page_count(3), 65, "heap pow 2", "Bin128  logical page count post growth");
        unit_test.test_equals(heap.get_small_object_bin_page_count(4), 129, "heap pow 2", "Bin256  logical page count post growth");
        unit_test.test_equals(heap.get_small_object_bin_page_count(5), 259, "heap pow 2", "Bin512  logical page count post growth");
        unit_test.test_equals(heap.get_small_object_bin_page_count(6), 521, "heap pow 2", "Bin1024 logical page count post growth");
        unit_test.test_equals(heap.get_small_object_bin_page_count(7), 1058, "heap pow 2", "Bin2048 logical page count post growth");

        // DEALLOCATIONS
        for (const auto small_object_allocation : small_object_allocations)
        {
            auto size_class = SimpleHeapPow2<>::SegmentSmallObject::get_size_class_from_address(small_object_allocation.ptr, params.m_small_object_logical_page_size);

            if (size_class != small_object_allocation.size_class)
            {
                std::cout << "PAGE HEADER VALIDATION FAILED : FOUND SIZECLASS IS WRONG !!!" << std::endl;
                return -1;
            }

            heap.deallocate(reinterpret_cast<void*>(small_object_allocation.ptr));
        }

        for (const auto big_object_allocation : big_object_allocations)
        {
            heap.deallocate(reinterpret_cast<void*>(big_object_allocation.ptr));
        }
    }
    ///////////////////////////////////////////////////////////////////////////////////////
    // ALIGNED ALLOCATIONS
    {
        SimpleHeapPow2<> heap;
        Arena<> arena;
        bool success =  arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }
        SimpleHeapPow2<>::HeapCreationParams params;
        params.m_small_object_logical_page_size = 65536;
        params.m_big_object_logical_page_size = 65536;
        params.m_big_object_page_recycling_threshold = 4;

        std::size_t small_object_capacity_per_size_class = 1024 * 16;
        params.m_small_object_page_recycling_threshold = small_object_capacity_per_size_class * 2;
        auto counter = 0;
        for (const auto& size_class : size_classes)
        {
            params.m_small_object_bin_page_counts[counter] = (small_object_capacity_per_size_class * size_class) / 65536;
            counter++;
        }

        success = heap.create(params, &arena);
        unit_test.test_equals(success, true, "heap pow 2", "aligned allocations creation success");

        struct aligned_allocation
        {
            std::size_t size = 0;
            std::size_t alignment = 0;
        };

        std::vector<aligned_allocation> aligned_allocations = { {5,32}, {23,128}, {1025, 64} , {1008,1024} , {240, 256}, {496, 512}, {2032, 2048}, {4080, 4096}, {8176, 8192}, {3328,16}, {3520,16}, {5248,16} };
        std::vector<Allocation> allocations;

        for (const auto& allocation : aligned_allocations)
        {
            if (validate_aligned_allocation(&heap, allocation.size, allocation.alignment, allocations) == false) { return -1; }
        }

        // DEALLOCATIONS
        for (const auto allocation : allocations)
        {
            heap.deallocate(reinterpret_cast<void*>(allocation.ptr));
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    // TRANSFER LOGICAL PAGES
    {
        SimpleHeapPow2<> heap_one;
        SimpleHeapPow2<> heap_two;

        Arena<> arena_one;
        bool success = arena_one.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }

        Arena<> arena_two;
        success = arena_two.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }

        SimpleHeapPow2<>::HeapCreationParams params;
        params.m_small_object_logical_page_size = 65536;
        params.m_big_object_logical_page_size = 655360;
        params.m_big_object_page_recycling_threshold = 4;

        std::size_t small_object_capacity_per_size_class = 1024 * 16;
        params.m_small_object_page_recycling_threshold = small_object_capacity_per_size_class * 2;
        auto counter = 0;
        for (const auto& size_class : size_classes)
        {
            params.m_small_object_bin_page_counts[counter] = (small_object_capacity_per_size_class * size_class) / 65536;
            counter++;
        }

        success = heap_one.create(params, &arena_one);
        unit_test.test_equals(success, true, "heap pow 2 transfer logical pages", "creation success 1");

        success = heap_two.create(params, &arena_two);
        unit_test.test_equals(success, true, "heap pow 2 transfer logical pages", "creation success 2");

        heap_two.transfer_logical_pages_from(&heap_one);
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    // BOUNDED HEAP
    {
        using TestHeapType = SimpleHeapPow2<ConcurrencyPolicy::THREAD_LOCAL>;
        TestHeapType heap;

        Arena<> arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }
        TestHeapType::HeapCreationParams params;
        params.m_small_object_logical_page_size = 65536;
        params.m_big_object_logical_page_size = 655360;
        params.m_big_object_page_recycling_threshold = 4;

        std::size_t small_object_capacity_per_size_class = 1024 * 16;
        params.m_small_object_page_recycling_threshold = small_object_capacity_per_size_class * 2;
        auto counter = 0;
        for (const auto& size_class : size_classes)
        {
            params.m_small_object_bin_page_counts[counter] = SizeUtilities::get_required_page_count_for_allocation(params.m_small_object_logical_page_size, sizeof(LogicalPage<>), size_class, small_object_capacity_per_size_class);
            counter++;
        }

        success = heap.create(params, &arena);
        unit_test.test_equals(success, true, "heap pow 2", "creation success");

        std::size_t big_object_allocation_size = 65536 - sizeof(LogicalPageAnySize<>);

        // ALLOCATING BIG OBJECTS
        std::vector<Allocation> big_object_allocations;
        for (std::size_t i = 0; i < 10; i++)
        {
            bool success = validate_allocation(&heap, big_object_allocation_size, params.m_big_object_logical_page_size, big_object_allocations);

            if (!success)
            {
                return -1;
            }
        }

        unit_test.test_equals(big_object_allocations.size(), 10, "heap pow 2", "verifying big object allocation count");

        unit_test.test_equals(heap.get_big_object_bin_page_count(), 1, "heap pow 2", "BigObject bin logical page count");
        if (validate_allocation(&heap, 32768, params.m_big_object_logical_page_size, big_object_allocations) == true) return -1; // Allocation should fail as we are bounded
        unit_test.test_equals(heap.get_big_object_bin_page_count(), 1, "heap pow 2", "BigObject bin logical page count post grow attempt");

        // ALLOCATING SMALL OBJECTS
        std::vector<std::size_t> size_classes = { 16,32,64,128,256,512,1024,2048 };
        std::vector<Allocation> small_object_allocations;

        for (const auto& size_class : size_classes)
        {
            for (std::size_t i = 0; i < small_object_capacity_per_size_class; ++i)
            {
                bool success = validate_allocation(&heap, size_class, params.m_small_object_logical_page_size, small_object_allocations);

                if (!success)
                {
                    return -1;
                }
            }
        }

        unit_test.test_equals(small_object_allocations.size(), small_object_capacity_per_size_class * size_classes.size(), "heap pow 2", "verifying small object allocation count");

        // VERIFY SMALL OBJECT LOGICAL PAGE COUNTS
        unit_test.test_equals(heap.get_small_object_bin_page_count(0), 5, "heap pow 2", "Bin16   logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(1), 9, "heap pow 2", "Bin32   logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(2), 17, "heap pow 2", "Bin64   logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(3), 33, "heap pow 2", "Bin128  logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(4), 65, "heap pow 2", "Bin256  logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(5), 130, "heap pow 2", "Bin512  logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(6), 261, "heap pow 2", "Bin1024 logical page count");
        unit_test.test_equals(heap.get_small_object_bin_page_count(7), 529, "heap pow 2", "Bin2048 logical page count");

        // DEALLOCATIONS
        for (const auto small_object_allocation : small_object_allocations)
        {
            auto size_class = TestHeapType::SegmentSmallObject::get_size_class_from_address(small_object_allocation.ptr, params.m_small_object_logical_page_size);

            if (size_class != small_object_allocation.size_class)
            {
                std::cout << "PAGE HEADER VALIDATION FAILED : FOUND SIZECLASS IS WRONG !!!" << std::endl;
                return -1;
            }

            heap.deallocate(reinterpret_cast<void*>(small_object_allocation.ptr));
        }

        for (const auto big_object_allocation : big_object_allocations)
        {
            heap.deallocate(reinterpret_cast<void*>(big_object_allocation.ptr));
        }
    }

    {
        Arena<> m_arena;

        bool success = m_arena.create(655360, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }


        SimpleHeapPow2<> allocator;

        SimpleHeapPow2<>::HeapCreationParams params;
        params.m_small_object_logical_page_size = 65536;
        params.m_big_object_logical_page_size = 196608;
        params.m_big_object_page_recycling_threshold = 4;

        std::size_t small_object_capacity_per_size_class = 1024 * 16;
        params.m_small_object_page_recycling_threshold = small_object_capacity_per_size_class * 2;
        auto counter = 0;
        for (const auto& size_class : size_classes)
        {
            params.m_small_object_bin_page_counts[counter] = (small_object_capacity_per_size_class * size_class) / 65536;
            counter++;
        }

        success = allocator.create(params, &m_arena);
        if (!success) { std::cout << "HEAP CREATION FAILED !!!" << std::endl; return -1; }

        constexpr std::size_t    ITERATION_COUNT = 1000;
        constexpr std::size_t     ALLOCATION_SIZE_COUNT = 37;
        constexpr std::size_t    ALLOCATION_SIZES[ALLOCATION_SIZE_COUNT] = { 32, 16, 32, 16, 16, 16, 32, 64, 32, 12, 14, 8, 20, 22, 23, 27, 35, 96, 128, 100, 256, 512, 1024, 40, 50, 43, 74, 2048, 1500, 29, 7, 41, 77, 60, 80, 84, 106 };
        std::uintptr_t             g_addresses[ALLOCATION_SIZE_COUNT * ITERATION_COUNT] = {};

        counter = 0;
        for (std::size_t i = 0; i < ITERATION_COUNT; i++)
        {
            for (std::size_t j{ 0 }; j < ALLOCATION_SIZE_COUNT; j++)
            {
                g_addresses[counter] = reinterpret_cast<std::uintptr_t>(allocator.allocate(ALLOCATION_SIZES[j]));
                counter++;
            }
        }

        // WRITE TO ALLOCATED ADDRESSES
        for (std::size_t i = 0; i < ALLOCATION_SIZE_COUNT * ITERATION_COUNT; i++)
        {
            *reinterpret_cast<std::size_t*>(g_addresses[i]) = i;
        }

        // READ FROM ALLOCATED ADDRESSES
        for (std::size_t i = 0; i < ALLOCATION_SIZE_COUNT * ITERATION_COUNT; i++)
        {
            std::size_t current = 0;
            current = *reinterpret_cast<std::size_t*>(g_addresses[i]);
            if (i != current)
            {
                std::cout << "ERROR !!!" << std::endl;
                return -1;
            }
        }

        counter = 0;
        std::size_t half_size = ALLOCATION_SIZE_COUNT * ITERATION_COUNT / 2;

        // DEALLOCATE FIRST HALF IN FIFO ORDER
        for (std::size_t i = 0; i < half_size; i++)
        {
            allocator.deallocate(reinterpret_cast<void*>(g_addresses[i]));
        }

        // DEALLOCATE SECOND HALF IN LIFO ORDER
        for (std::size_t i = ALLOCATION_SIZE_COUNT * ITERATION_COUNT - 1; i >= half_size; i--)
        {
            allocator.deallocate(reinterpret_cast<void*>(g_addresses[i]));
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    // RANDOM SMALL OBJECT ALLOCATIONS
    {
        SimpleHeapPow2<> heap;
        Arena<> arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }
        SimpleHeapPow2<>::HeapCreationParams params;
        params.m_small_object_logical_page_size = 65536;
        params.m_big_object_logical_page_size = 655360;
        params.m_big_object_page_recycling_threshold = 4;

        std::size_t small_object_capacity_per_size_class = 1024 * 16;
        params.m_small_object_page_recycling_threshold = small_object_capacity_per_size_class * 2;
        auto counter = 0;
        for (const auto& size_class : size_classes)
        {
            params.m_small_object_bin_page_counts[counter] = (small_object_capacity_per_size_class * size_class) / 65536;
            counter++;
        }

        success = heap.create(params, &arena);
        if (!success) { std::cout << "HEAP CREATION FAILED !!!" << std::endl; return -1; }

        std::vector<Allocation> allocations;

        for (std::size_t i = 0; i < 102400; i++)
        {
            auto size = RandomNumberGenerator::get_random_integer(2048);
            if (validate_allocation(&heap, size, 65536, allocations) == false)
            {
                std::cout << "Validation failed for size : " << size << std::endl;
                return -1;
            }
        }

        for (auto& allocation : allocations)
        {
            auto size_class = SimpleHeapPow2<>::SegmentSmallObject::get_size_class_from_address(allocation.ptr, params.m_small_object_logical_page_size);

            if (size_class != heap.adjust_size_for_small_object_bin(allocation.size_class))
            {
                std::cout << "PAGE HEADER VALIDATION FAILED : FOUND SIZECLASS IS WRONG. ALLOCATION SIZE WAS : " << allocation.size_class << std::endl;
                return -1;
            }

            heap.deallocate(allocation.ptr);
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    // STRESS TEST
    {
        struct  AlignedAllocation
        {
            std::size_t size = 0;
            std::size_t alignment = 0;
        };

        std::vector<AlignedAllocation> aligned_allocations = { {1008,1024}, {240,256}, {240,256}, {240,256}, {240,256}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {240,256}, {240,256}, {240,256}, {240,256}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {4080,4096}, {2032,2048}, {8176,8192}, {2032,2048}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {2032,2048}, {2032,2048}, {2032,2048}, {240,256}, {240,256}, {240,256}, {240,256}, {2032,2048}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {4080,4096}, {1008,1024}, {1008,1024}, {2032,2048}, {4080,4096}, {1008,1024}, {2032,2048}, {2032,2048}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {2032,2048}, {1008,1024}, {1008,1024}, {2032,2048}, {1008,1024}, {496,512}, {496,512}, {496,512}, {2032,2048}, {2032,2048}, {1008,1024}, {1008,1024}, {2032,2048}, {2032,2048}, {1008,1024}, {1008,1024}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {2032,2048}, {2032,2048}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {4080,4096}, {1008,1024}, {496,512}, {496,512}, {496,512}, {2032,2048}, {240,256}, {240,256}, {240,256}, {240,256}, {2032,2048}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {2032,2048}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {240,256}, {240,256}, {240,256}, {240,256}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {2032,2048}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {240,256}, {240,256}, {240,256}, {240,256}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {2032,2048}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {2032,2048}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {8176,8192}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {240,256}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {1008,1024}, {1008,1024}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, {496,512}, };


        SimpleHeapPow2<> heap;
        Arena<> arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }
        SimpleHeapPow2<>::HeapCreationParams params;
        params.m_small_object_logical_page_size = 65536;
        params.m_big_object_logical_page_size = 655360;
        params.m_big_object_page_recycling_threshold = 4;
        params.m_segment_grow_coefficient = 0;
        std::size_t small_object_capacity_per_size_class = 1024 * 16;
        params.m_small_object_page_recycling_threshold = small_object_capacity_per_size_class * 2;

        success = heap.create(params, &arena);
        if (!success) { std::cout << "HEAP CREATION FAILED !!!" << std::endl; return -1; }

        for (const auto& aligned_allocation : aligned_allocations)
        {
            auto ptr = heap.allocate_aligned(aligned_allocation.size, aligned_allocation.alignment);

            if (ptr == nullptr)
            {
                std::cout << "allocation failed\n";
                return -1;
            }

            bool buffer_validated = validate_buffer(ptr, aligned_allocation.size);

            if (!buffer_validated)
            {
                std::cout << "buffer validation failed\n";
                return -1;
            }


            bool alignment_check = AlignmentChecks::is_address_aligned(ptr, aligned_allocation.alignment);

            if (!alignment_check)
            {
                std::cout << "alignment failed\n";
                return -1;
            }
        }
    }

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("simple_heap_pow2");

    #if _WIN32
    std::system("pause");
    #endif

    return unit_test.did_all_pass();
}