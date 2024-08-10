#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include "../../src/logical_page.h"
#include "../../src/logical_page_anysize.h"
#include "../../src/arena.h"
#include "../../src/segment.h"

#include <iostream>
#include <cstddef>
#include <cstdlib>
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

template <typename LogicalPageType, bool aligned_logical_page_addresses=true>
bool run_test(const std::string name, std::size_t logical_page_buffer_size, uint16_t size_class, std::size_t logical_page_count, std::size_t allocation_count, bool is_anysize)
{
    //////////////////////////////////////
    // CHECK CREATION FAILURES
    {
        Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPageType, Arena<>, PageRecyclingPolicy::IMMEDIATE, aligned_logical_page_addresses> segment;
        Arena<> arena;
        bool success = arena.create(65536 * 10, logical_page_buffer_size);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

        SegmentCreationParameters params;
        params.m_size_class = 0;
        params.m_logical_page_count = 0;
        params.m_logical_page_size = logical_page_buffer_size;
        params.m_page_recycling_threshold = 0;

        success = segment.create(nullptr , &arena, params);
        unit_test.test_equals(success, false, "creation checks " + name, "invalid segment creation argument");
    }
    //////////////////////////////////////
    Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPageType, Arena<>, PageRecyclingPolicy::IMMEDIATE, aligned_logical_page_addresses> segment;
    Arena<> arena;
    bool success = arena.create(65536 * 10, logical_page_buffer_size);
    if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

    char* initial_buffer = static_cast <char*>(arena.allocate(logical_page_count* logical_page_buffer_size));

    SegmentCreationParameters params;
    params.m_size_class = size_class;
    params.m_logical_page_count = logical_page_count;
    params.m_logical_page_size = logical_page_buffer_size;
    params.m_page_recycling_threshold = logical_page_count * 2;
    params.m_grow_coefficient = 1;

    success = segment.create(initial_buffer , &arena, params);
    unit_test.test_equals(true, success, "creation checks " + name, "positive creation case");

    auto too_big_allocation = segment.allocate(logical_page_buffer_size+1);
    unit_test.test_equals(true, too_big_allocation == nullptr, "allocation checks " + name, "too big size");

    std::vector<Allocation> allocations;

    // WE ALLOCATE ALL THE BUFFER
    for (std::size_t i{ 0 }; i < allocation_count; i++)
    {
        auto ptr = segment.allocate(size_class);
        if (ptr)
        {
            Allocation allocation;
            allocation.size_class = size_class;
            allocation.ptr = ptr;
            allocations.push_back(allocation);

            
            bool is_page_header_good = size_class == Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPage<>, Arena<>, PageRecyclingPolicy::IMMEDIATE, aligned_logical_page_addresses>::get_size_class_from_address(ptr, logical_page_buffer_size);

            if (is_page_header_good == false)
            {
                std::cout << "PAGE HEADER VALIDATION FAILED" << std::endl;
                return false;

            }
            
        }
        else
        {
            std::cout << "ALLOCATION FAILED" << std::endl;
            return false;
        }
    }

    std::cout << std::endl << std::endl << "VALIDATING THE ALL ALLOCATED BUFFERS" << std::endl << std::endl;

    for (auto& allocation : allocations)
    {
        bool buffer_ok = validate_buffer(reinterpret_cast<void*>(allocation.ptr), size_class);
        if (buffer_ok == false)
        {
            std::cout << "BUFFER VALIDATION FAILED" << std::endl;
            return false;
        }
    }

    unit_test.test_equals(allocations.size(), allocation_count, "segment allocation " + name, "number of successful allocations");

    if (is_anysize)
    {
        auto ptr1 = reinterpret_cast<void*>(allocations[0].ptr);
        auto ptr2 = reinterpret_cast<void*>(allocations[1].ptr);
        auto ptr3 = reinterpret_cast<void*>(allocations[2].ptr);

        segment.deallocate(ptr1);
        segment.deallocate(ptr2);
        segment.deallocate(ptr3);

        allocations.erase(allocations.begin() + 0);
        allocations.erase(allocations.begin() + 1);
        allocations.erase(allocations.begin() + 2);


        auto new_ptr1 = segment.allocate(size_class);
        auto new_ptr2 = segment.allocate(size_class);
        auto new_ptr3 = segment.allocate(size_class);

        UNUSED(new_ptr1);
        UNUSED(new_ptr2);
        UNUSED(new_ptr3);
    }

    // 1 MORE ALLOCATION TO TRIGGER GROW
    auto current_logical_page_count = segment.get_logical_page_count();
    unit_test.test_equals(logical_page_count, current_logical_page_count, name + " segment allocation", "logical page count before grow");

    auto latest_ptr = segment.allocate(size_class);
    UNUSED(latest_ptr);

    current_logical_page_count = segment.get_logical_page_count();
    unit_test.test_equals(logical_page_count * 2, current_logical_page_count, name + " segment allocation", "logical page count after grow");

    // Deallocate all the rest
    for (auto& allocation : allocations)
    {
        
        auto size_class = Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPage<>, Arena<>, PageRecyclingPolicy::IMMEDIATE, aligned_logical_page_addresses>::get_size_class_from_address(allocation.ptr, logical_page_buffer_size);

        if (size_class != allocation.size_class)
        {
            std::cout << "PAGE HEADER VALIDATION FAILED : FOUND SIZECLASS IS WRONG !!!" << std::endl;
            return false;
        }
        

        segment.deallocate(reinterpret_cast<void*>(allocation.ptr));
    }

    return true;
}

int main()
{
    /////////////////////////////////////////////////////////////////////////////////
    std::size_t logical_page_count_per_segment = 32;
    std::size_t constexpr size_class = 128;

    //////////////////////////////////////////////////////////////////////////
    // UNBOUNDED SEGMENT TESTS, LOGICAL PAGE SIZE 4K, WE DON'T SUPPORT THIS ON WINDOWS SO ONLY LINUX
    // LogicalPageHeader is normally 32 bytes. However if UNIT_TEST defined, logical pages declare 2 more variables , therefore 48 bytes
    #ifdef __linux__
    run_test<LogicalPage<>>("LogicalPage", 4096, size_class, logical_page_count_per_segment, 992, false); // no padding bytes , 64 bytes page header so 4096-40=4032 , 4032/128=31 , 31*32 = 992
    #endif

    //////////////////////////////////////////////////////////////////////////
    // UNBOUNDED SEGMENT TESTS, LOGICAL PAGE SIZE 64K
    run_test<LogicalPageAnySize<>>("LogicalPageAnySize", 65536, size_class, logical_page_count_per_segment, 14528, true); // for 128 bytes there is 16 byte alloc header so it is 144 bytes , also 64 bytes page header so 65472 -> 65496 /144 * 32 = 14528
    run_test<LogicalPage<>>("LogicalPage", 65536, size_class, logical_page_count_per_segment, 16352, false); // no padding bytes , 64 bytes page header so 65536-64=65472 , 65472/128=511 , 511*32 = 16352

    //////////////////////////////////////////////////////////////////////////
    // BOUNDED SEGMENT TEST ( THREAD LOCAL )
    {
        Arena<>  arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

        Segment<ConcurrencyPolicy::THREAD_LOCAL, LogicalPage<>, Arena<>> segment;
        std::vector<std::uint64_t> pointers;

        char* initial_buffer = static_cast <char*>(arena.allocate(65536));

        SegmentCreationParameters params;
        params.m_size_class = 2048;
        params.m_logical_page_count = 1;
        params.m_logical_page_size = 65536;
        params.m_page_recycling_threshold = 1;

        success = segment.create(initial_buffer, &arena,params); // We start with 1 logical page , and threshold is also 1
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        // Logical page actual capacity 65536-40 = 65496 , 65496/2038 = 31 objects
        for (std::size_t i = 0; i < 31; i++)
        {
            auto ptr = segment.allocate(2048);
            pointers.push_back(reinterpret_cast<std::uint64_t>(ptr));
        }

        unit_test.test_equals(segment.get_logical_page_count(), 1, "bounded segment", "exhaustion");

        auto ptr = segment.allocate(2048); // Attempt to grow
        if (ptr) { std::cout << "Bounded segment failed. It should have not grown" << std::endl; return -1; }
        unit_test.test_equals(segment.get_logical_page_count(), 1, "bounded segment", "allocation after exhaustion");

        for (const auto& ptr : pointers)
        {
            segment.deallocate(reinterpret_cast<void*>(ptr));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // PAGE RECYCLING , MODE AUTO
    {
        Arena<>  arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

        Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPage<>, Arena<>, PageRecyclingPolicy::IMMEDIATE> segment;
        std::vector<std::uint64_t> pointers;

        char* initial_buffer = static_cast <char*>(arena.allocate(65536));

        SegmentCreationParameters params;
        params.m_size_class = 2048;
        params.m_logical_page_count = 1;
        params.m_logical_page_size = 65536;
        params.m_page_recycling_threshold = 1;

        success = segment.create(initial_buffer , &arena, params); // We start with 1 logical page , and threshold is also 1
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        // Logical page actual capacity 65536-40 = 65496 , 65496/2038 = 31 objects
        for (std::size_t i = 0; i < 31; i++)
        {
            auto ptr = segment.allocate(2048);
            pointers.push_back(reinterpret_cast<std::uint64_t>(ptr));
        }

        unit_test.test_equals(segment.get_logical_page_count(), 1, "segment", "exhaustion");

        auto ptr = segment.allocate(2048); // Segment will grow by 1 page
        unit_test.test_equals(segment.get_logical_page_count(), 2, "segment", "allocation after exhaustion");

        segment.deallocate(ptr); // INVOKING PAGE RECYCLING
        unit_test.test_equals(segment.get_logical_page_count(), 1, "segment", "allocation after recycling");

        for (const auto& ptr : pointers)
        {
            segment.deallocate(reinterpret_cast<void*>(ptr));
        }
    }
    //////////////////////////////////////////////////////////////////////////
    // PAGE RECYCLING , MODE MANUAL
    {
        Arena<>  arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }
        Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPage<>, Arena<>, PageRecyclingPolicy::DEFERRED> segment;
        std::vector<std::uint64_t> pointers;

        char* initial_buffer = static_cast <char*>(arena.allocate(65536));

        SegmentCreationParameters params;
        params.m_size_class = 2048;
        params.m_logical_page_count = 1;
        params.m_logical_page_size = 65536;
        params.m_page_recycling_threshold = 1;

        success = segment.create(initial_buffer, &arena, params); // We start with 1 logical page , and threshold is also 1
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        // Logical page actual capacity 65536-40 = 65496 , 65496/2038 = 31 objects
        for (std::size_t i = 0; i < 31; i++)
        {
            auto ptr = segment.allocate(2048);
            pointers.push_back(reinterpret_cast<std::uint64_t>(ptr));
        }

        unit_test.test_equals(segment.get_logical_page_count(), 1, "segment recycling", "exhaustion");

        auto ptr = segment.allocate(2048); // Segment will grow by 1 page
        unit_test.test_equals(segment.get_logical_page_count(), 2, "segment recycling", "allocation after exhaustion");

        segment.deallocate(ptr);
        unit_test.test_equals(segment.get_logical_page_count(), 2, "segment recycling", "allocation after exhaustion and deallocation");
        segment.recycle_free_logical_pages(); // INVOKING PAGE RECYCLING
        unit_test.test_equals(segment.get_logical_page_count(), 1, "segment recycling", "allocation after recycling");

        for (const auto& ptr : pointers)
        {
            segment.deallocate(reinterpret_cast<void*>(ptr));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // LOGICAL PAGE TRANSFER BETWEEN SEGMENTS
    {
        Arena<>  arena_one;
        bool success = arena_one.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }
        Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPage<>, Arena<>> segment_one;

        char* initial_buffer_one = static_cast <char*>(arena_one.allocate(4*65536));

        SegmentCreationParameters params_one;
        params_one.m_size_class = 32;
        params_one.m_logical_page_count = 4;
        params_one.m_logical_page_size = 65536;
        params_one.m_page_recycling_threshold = 4;

        success = segment_one.create(initial_buffer_one, &arena_one, params_one);
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        Arena<>  arena_two;
        success = arena_two.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }
        Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPage<>, Arena<>> segment_two;

        char* initial_buffer_second = static_cast <char*>(arena_two.allocate(4 * 65536));

        SegmentCreationParameters params_two;
        params_two.m_size_class = 32;
        params_two.m_logical_page_count = 4;
        params_two.m_logical_page_size = 65536;
        params_two.m_page_recycling_threshold = 4;

        success = segment_two.create(initial_buffer_second, &arena_two , params_two);
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        unit_test.test_equals(segment_one.get_logical_page_count(), 4, "segment logical page transfer", "before transfer , source segment");
        unit_test.test_equals(segment_two.get_logical_page_count(), 4, "segment logical page transfer", "before transfer , destination segment");
        segment_two.transfer_logical_pages_from(segment_one);
        unit_test.test_equals(segment_one.get_logical_page_count(), 0, "segment logical page transfer", "after transfer , source segment");
        unit_test.test_equals(segment_two.get_logical_page_count(), 8, "segment logical page transfer", "after transfer , destination segment");
    }

    // DEALLOCATE_FAST , IT CAN ONLY BE USED WHEN LOGICAL PAGE START ADDRESSES ARE ALIGNED TO LOGICAL_PAGE_SIZE
    {
        Arena<>  arena;
        bool success = arena.create(65536 * 10, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

        Segment<ConcurrencyPolicy::SINGLE_THREAD, LogicalPageAnySize<>, Arena<>,PageRecyclingPolicy::IMMEDIATE, true> segment;
        std::vector<std::uint64_t> pointers;

        char* initial_buffer = static_cast <char*>(arena.allocate(65536));

        SegmentCreationParameters params;
        params.m_size_class = 0;
        params.m_logical_page_count = 1;
        params.m_logical_page_size = 65536;
        params.m_page_recycling_threshold = 1;

        success = segment.create(initial_buffer, &arena, params);
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        for (std::size_t i = 0; i < 10; i++)
        {
            auto ptr = segment.allocate(6500);
            pointers.push_back(reinterpret_cast<uint64_t>(ptr));
        }

        auto ptr = segment.allocate(4096); // trigger 2nd page
        pointers.push_back(reinterpret_cast<uint64_t>(ptr));

        unit_test.test_equals(segment.get_logical_page_count(), 2, "segment auto recycling with aligned logical page addreses", "exhaustion");

        // IF LOGICAL PAGE SIZES IN THE SEGMENT DON'T MEET ALIGNMENT REQUIREMENTS WE WILL CRASH DURING DEALLOCATIONS
        for (auto ptr : pointers)
        {
            segment.deallocate(reinterpret_cast<void*>(ptr)); // Invokes deallocate_from_aligned_logical_page
        }

        unit_test.test_equals(segment.get_logical_page_count(), 1, "segment auto recycling with aligned logical page addreses", "post recycling");
    }

    // STRESS TEST
    {
        Arena<>  arena;
        bool success = arena.create(655360, 65536);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

        Segment<ConcurrencyPolicy::CENTRAL, LogicalPageAnySize<>, Arena<>> segment;
        std::vector<std::uint64_t> pointers;

        char* initial_buffer = static_cast <char*>(arena.allocate(65536));

        SegmentCreationParameters params;
        params.m_logical_page_count = 1;

        ///////////////////////////////////////////////
        params.m_logical_page_size = 655360;
        params.m_page_recycling_threshold = 1;
        std::size_t ITERATION_COUNT = 100;
        bool do_deallocations = true;
        ///////////////////////////////////////////////

        success = segment.create(initial_buffer, &arena, params); // We start with 1 logical page , and threshold is also 1
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        void* previous_ptr = nullptr;

        for (std::size_t j = 0; j < ITERATION_COUNT; j++)
        {
            for (std::size_t i = 2048; i < 8192; i++)
            {
                auto ptr = segment.allocate(i);
                if (ptr == nullptr)
                {
                    std::cout << "allocation failed\n";
                    return -1;
                }

                if (previous_ptr)
                {
                    if (do_deallocations)
                    {
                        segment.deallocate(previous_ptr);
                    }
                }

                previous_ptr = ptr;
            }
        }

    }

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("Segment");

    #if _WIN32
    std::system("pause");
    #endif

    return unit_test.did_all_pass();
}