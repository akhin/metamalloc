#ifndef __LOGICAL_PAGE_COMMON_TESTS_H__
#define __LOGICAL_PAGE_COMMON_TESTS_H__

#include <string>
#include "../../src/arena.h"
#include "../../src/logical_page.h"
#include "../../src/logical_page_anysize.h"
#include "../../src/os/thread_utilities.h"

bool validate_buffer(void* buffer, std::size_t buffer_size);
void test_padding_calculation();

template <typename LogicalPageType>
void test_incorrect_creation(std::size_t buffer_size);

template <typename LogicalPageType, typename NodeType>
void test_excess_block_handling();

template <typename LogicalPageType, typename NodeType>
bool test_exhaustion(std::size_t buffer_size, std::size_t allocation_count, std::size_t allocation_size);

template <typename LogicalPageType>
void test_errors();

template <typename LogicalPageType, typename NodeType, CoalescePolicy coalesce_policy>
bool test_general(std::size_t);

int test_extras_logical_page_any_size();

template <typename LogicalPageType>
void test_incorrect_creation(std::size_t buffer_size)
{
    Arena arena;
    LogicalPageType logical_page;
    bool success = logical_page.create(arena.allocate(buffer_size), buffer_size, 8);
    unit_test.test_equals(false, success, "creation checks", logical_page.get_type_name() +  " - too small buffer size");
}

template <CoalescePolicy coalesce_policy>
constexpr std::string coalesce_policy_to_string()
{
    std::string ret;
    if constexpr (coalesce_policy == CoalescePolicy::COALESCE)
    {
        ret = "COALESCE";
    }
    else if constexpr (coalesce_policy == CoalescePolicy::NO_COALESCING)
    {
        ret = "NO_COALESCING";
    }

    return ret;
}

void test_padding_calculation()
{
    struct PaddingTest
    {
        std::size_t address;
        std::size_t alignment;
        std::size_t expected_padding;
    };

    std::array<PaddingTest, 6> cases;
    cases[0] = { 0x04, 4, 0 };   // 4+16=20
    cases[1] = { 0x04, 8, 4 };   // 4+16=20, 24-20=4
    cases[2] = { 0x03, 15, 11 }; // 3+16=19, 30-19=11
    cases[3] = { 0x04, 16, 12 }; // 4+16=20, 32-20=12
    cases[4] = { 0x01, 32, 15 }; // 1+16=17, 32-17=15
    cases[5] = { 0x10, 32, 0 };  // 16+16=32

    for (const auto padding_test_case : cases)
    {
        unit_test.test_equals(LogicalPageAnySize<>::calculate_padding_needed<sizeof(LogicalPageAnySizeNode)>(padding_test_case.address, padding_test_case.alignment), padding_test_case.expected_padding, "logical page any size padding calculation", "address:" +  std::to_string(padding_test_case.address) + " alignment:" + std::to_string(padding_test_case.alignment));
    }
}

template <typename LogicalPageType, typename NodeType>
void test_excess_block_handling()
{
    ///////////////////////////////////////////////////////////////////////////////////////////////
    // ALLOCATION NO EXCESS BYTES
    {
        Arena arena;
        std::size_t buffer_size = VirtualMemory::PAGE_ALLOCATION_GRANULARITY / 2;

        LogicalPageType logical_page;
        bool success = logical_page.create(arena.allocate(buffer_size), buffer_size);

        std::string category = "allocation - excess bytes ";

        unit_test.test_equals(true, success, category, "page creation");

        if (success == false)
        {
            return;
        }

        std::size_t allocation_size = buffer_size - sizeof(NodeType);

        auto ptr = logical_page.allocate(allocation_size);

        if (ptr == nullptr)
        {
            return;
        }

        unit_test.test_equals(logical_page.get_used_size(), buffer_size, category, "no excess bytes , used size after allocation");

        logical_page.deallocate(ptr);

        unit_test.test_equals(logical_page.get_used_size(), 0, category, "no excess bytes , used size after free");
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    // ALLOCATION EXCESS BYTES LESS THAN FREELIST NODE SIZE , WE SHOULD NOT LOSE THAT BLOCK , IT WILL BE ADDED TO WHAT WE ARE ALLOCATING
    {
        Arena arena;
        std::size_t buffer_size = VirtualMemory::PAGE_ALLOCATION_GRANULARITY / 2;

        LogicalPageType logical_page;
        bool success = logical_page.create(arena.allocate(buffer_size), buffer_size);

        if (success == false)
        {
            return;
        }

        std::string category = "allocation - excess bytes ";

        unit_test.test_equals(success, true, category, "page creation");

        std::size_t allocation_size = buffer_size - sizeof(NodeType) - 1;

        auto ptr = logical_page.allocate(allocation_size);

        if (ptr == nullptr)
        {
            return;
        }

        unit_test.test_equals(logical_page.get_used_size(), buffer_size, category, "excess less than node size, used size after allocation");

        logical_page.deallocate(ptr);

        unit_test.test_equals(logical_page.get_used_size(), 0, category, "excess less than node size, used size after free");
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////
    // ALLOCATION EXCESS BYTES MORE THAN FREELIST NODE SIZE , IT IS BEING RETURNED TO THE FREELIST
    {
        Arena arena;
        std::size_t buffer_size = VirtualMemory::PAGE_ALLOCATION_GRANULARITY / 2;

        LogicalPageType logical_page;
        bool success = logical_page.create(arena.allocate(buffer_size), buffer_size);

        if (success == false)
        {
            return;
        }

        std::string category = "allocation - excess bytes";

        unit_test.test_equals(success, true, category, "page creation");

        auto ptr = logical_page.allocate(sizeof(NodeType));

        if (ptr == nullptr)
        {
            return;
        }

        unit_test.test_equals(logical_page.get_used_size() >= sizeof(NodeType), true, category, "excess more than node size, used size after allocation");

        unit_test.test_equals(logical_page.capacity() - logical_page.get_used_size() >= sizeof(NodeType), true, category, "excess more than node size, available size after allocation");

        logical_page.deallocate(ptr);

        unit_test.test_equals(logical_page.get_used_size(), 0, category, "excess more than node size, used size after free");
    }
}

bool validate_buffer(void* buffer, std::size_t buffer_size)
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

template <typename LogicalPageType, typename NodeType>
bool test_exhaustion(std::size_t buffer_size, std::size_t allocation_count, std::size_t allocation_size)
{
    Arena arena;
    LogicalPageType logical_page;

    bool success = logical_page.create(arena.allocate(buffer_size), buffer_size, static_cast<uint16_t>(allocation_size));

    if (success == false)
    {
        return false;
    }

    std::vector<std::size_t> pointers;

    // WE ALLOCATE ALL THE BUFFER
    for (std::size_t i{ 0 }; i < allocation_count; i++)
    {
        auto ptr = logical_page.allocate(allocation_size);
        if (ptr)
        {
            pointers.push_back(reinterpret_cast<std::size_t>(ptr));
        }
    }

    unit_test.test_equals(logical_page.get_used_size(), buffer_size, "logical page exhaustion", "Verify that the page exhausted");

    auto first_node = reinterpret_cast<void*>(pointers[0]);
    pointers.erase(pointers.begin() + 0);

    logical_page.deallocate(first_node);
    unit_test.test_equals(false, logical_page.get_head_node() == nullptr, "logical page exhaustion", "The head node should not be empty after exhaustion");

    // Deallocations
    for (auto& pointer : pointers)
    {
        logical_page.deallocate(reinterpret_cast<void*>(pointer));
    }

    unit_test.test_equals(logical_page.get_used_size(), 0, "logical page exhaustion", "Verify that the page is fully available");

    return true;
}

template <typename LogicalPageType>
void test_errors()
{
    // Out of memory
    {

        Arena<LockPolicy::NO_LOCK, VirtualMemoryPolicy::OUT_OF_MEMORY> arena;

        std::size_t BUFFER_SIZE = VirtualMemory::PAGE_ALLOCATION_GRANULARITY;

        LogicalPageType logical_page;
        bool success = logical_page.create(arena.allocate(BUFFER_SIZE), BUFFER_SIZE, 128);
        unit_test.test_equals(false, success, "allocation - test errors", "out of memory");

    }

    /*
    // Double free
    {
        Arena arena;
        std::size_t BUFFER_SIZE = VirtualMemory::PAGE_ALLOCATION_GRANULARITY;

        LogicalPageType logical_page;
        bool success = logical_page.create(arena.allocate(BUFFER_SIZE), BUFFER_SIZE);
        unit_test.test_equals(true, success, "allocation - test errors", "page creation");

        auto ptr = logical_page.allocate(32);
        logical_page.deallocate(ptr);
        unit_test.test_equals(logical_page.used_size(), 0, "allocation - test errors", "double free , size after first free");
        logical_page.deallocate(ptr);
        unit_test.test_equals(logical_page.used_size(), 0, "allocation - test errors", "double free , size after second free");
    }
    */

    // Freeing invalid pointer which was not allocated by the allocator
    {
        Arena arena;
        std::size_t BUFFER_SIZE = VirtualMemory::PAGE_ALLOCATION_GRANULARITY;

        LogicalPageType logical_page;
        bool success = logical_page.create(arena.allocate(BUFFER_SIZE), BUFFER_SIZE, 128);
        unit_test.test_equals(true, success, "allocation - test errors", "page creation");

        auto used_size = logical_page.get_used_size();
        auto invalid_ptr = std::malloc(4);
        logical_page.deallocate(invalid_ptr);
        unit_test.test_equals(logical_page.get_used_size(), used_size, "allocation - test errors", "freeing invalid pointer");
    }
}

template <typename LogicalPageType, typename NodeType, CoalescePolicy coalesce_policy>
bool test_general(std::size_t buffer_size)
{
    Arena arena;
    std::size_t BUFFER_SIZE = buffer_size;

    LogicalPageType logical_page;

    bool success = logical_page.create(arena.allocate(BUFFER_SIZE), BUFFER_SIZE, 128);

    std::string test_category = "allocation general " + coalesce_policy_to_string<coalesce_policy>();

    unit_test.test_equals(true, success, test_category, "logical_page creation");

    std::vector<uintptr_t> pointers;
    std::vector<std::size_t> increment_sizes;
    std::size_t allocation_size = 12;
    std::size_t last_used_size = 0;

    std::size_t counter = 0;

    while (true)
    {
        auto ptr = logical_page.allocate(allocation_size);

        if (ptr == nullptr)
        {
            if (counter == 0)
            {
                return false;
            }
            else
            {
                break;
            }
        }

        /*
        const NodeType* allocation_header{ reinterpret_cast<NodeType*>( reinterpret_cast<std::size_t>(ptr) - LogicalPageAnySizeConstants::ALLOCATION_HEADER_SIZE) };
        std::cout << "allocation size : " << allocation_size << " used_size : " << logical_page.used_size() << " increment size : " << logical_page.used_size() - last_used_size << " header size : " << allocation_header->get_block_size() << " << std::endl;
        */

        if constexpr (std::is_same<NodeType, LogicalPageAnySizeNode>::value)
        {
            if (AlignmentChecks::is_address_aligned(ptr, 16) == false)
            {
                return false;
            }
        }

        increment_sizes.push_back(logical_page.get_used_size() - last_used_size);

        pointers.push_back(reinterpret_cast<uintptr_t>(ptr));

        allocation_size++;
        last_used_size = logical_page.get_used_size();
        counter++;
    }

    // std::cout << std::endl << "ALLOCATIONS DONE" << std::endl << std::endl;

    counter = 0;

    for (auto ptr : pointers)
    {
        /*
        const NodeType* allocation_header{ reinterpret_cast<NodeType*>(ptr - LogicalPageAnySizeConstants::ALLOCATION_HEADER_SIZE) };
        auto allocation_header_block_size = allocation_header->get_block_size();
        */

        logical_page.deallocate(reinterpret_cast<void*>(ptr));

        unit_test.test_equals(logical_page.get_used_size(), last_used_size - increment_sizes[counter], test_category, "deallocation " + std::to_string(counter));

        // std::cout <<  "header size : " << allocation_header_block_size << " << " used_size : " << logical_page.used_size() << std::endl;

        last_used_size = logical_page.get_used_size();
        counter++;
    }

    unit_test.test_equals(logical_page.get_used_size(), 0, test_category, "cumulative deallocations ");
    return true;
}

int test_extras_logical_page_any_size()
{
    // ALLOCATION ATTEMPT FROM A COMPLETELY EXHAUSTED LOGICAL PAGE
    {
        Arena<> arena;
        LogicalPageAnySize<> allocator;

        std::vector<uint64_t> pointers;
        char* buffer = arena.allocate(65536);
        bool success = allocator.create(buffer, 65536, 0);

        if (!success) { std::cout << "LogicalPageAnySize creation failed !!! " << std::endl; return -1; }

        auto ptr = allocator.allocate(65536 - 16); // 16: allocation_header

        success = validate_buffer(ptr, 65536 - 16); // 16: allocation_header

        if (!success) { std::cout << "LogicalPageAnySize ALLOCATION FAILED !!!" << std::endl; return -1; }

        auto ptr2 = allocator.allocate(32);

        unit_test.test_equals(ptr2 == nullptr, true, "LogicalPageAnySize", "Allocation from completely used logical page");

        allocator.deallocate(ptr);
        VirtualMemory::deallocate(buffer, 65536);
    }

    // NO SUITABLE BLOCK
    {
        Arena<> arena;
        LogicalPageAnySize<> allocator;

        bool success = allocator.create(arena.allocate(65536), 65536, 0);
        if (!success) { std::cout << "LogicalPageAnySize creation failed !!! " << std::endl; return -1; }

        auto ptr = allocator.allocate(65500); // Total size will be 65516 , only block remaining will have 20 bytes , so only 4 bytes allocatable
        success = validate_buffer(ptr, 65500);

        auto ptr2 = allocator.allocate(5);
        unit_test.test_equals(ptr2, nullptr, "LogicalPageAnySize", "trying to allocate when only available block doesn't have enough size");

        allocator.deallocate(ptr);
    }


    // LAST ALLOCATION ADD EXCESS BYTES AS A NEW NODE THEN NEED TO REMOVE FROM THE MIDDLE OF THE FREELIST
    {
        Arena<> arena;
        LogicalPageAnySize<CoalescePolicy::NO_COALESCING> allocator;

        bool success = allocator.create(arena.allocate(65536), 65536, 0);
        if (!success) { std::cout << "LogicalPageAnySize creation failed !!! " << std::endl; return -1; }

        ////////////////////////////////////////////////////////////////// 1 NODE
        //std::cout << "After allocating 8 bytes" << std::endl;
        auto ptr = allocator.allocate(8);
        if (!ptr) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        ////////////////////////////////////////////////////////////////// 1 NODE
        //std::cout << "After allocating 16 bytes" << std::endl;
        auto ptr2 = allocator.allocate(16);
        if (!ptr2) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        ////////////////////////////////////////////////////////////////// 2 NODES AS NO_COALESCING
        //std::cout << "After deallocating 8 bytes" << std::endl;
        allocator.deallocate(ptr);
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        //////////////////////////////////////////////////////////////////
        //std::cout << "After allocating 24 bytes" << std::endl;
        auto ptr3 = allocator.allocate(24);
        if (!ptr3) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;

        unit_test.test_equals(allocator.get_node_count(), 2, "LogicalPageAnysize", "Removal from the middle of the freelist");
    }

    // ALLOCATIONS AND DEALLOCATIONS IN MIXED ORDER
    {
        Arena<> arena;
        LogicalPageAnySize<CoalescePolicy::NO_COALESCING> allocator;

        bool success = allocator.create(arena.allocate(65536), 65536, 0);
        if (!success) { std::cout << "LogicalPageAnySize creation failed !!! " << std::endl; return -1; }

        //////////////////////////////////////////////////////////////////
        //std::cout << "After allocating 8 bytes" << std::endl;
        auto ptr = allocator.allocate(8);
        if (!ptr) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        //////////////////////////////////////////////////////////////////
        //std::cout << "After allocating 16 bytes" << std::endl;
        auto ptr2 = allocator.allocate(16);
        if (!ptr2) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        //////////////////////////////////////////////////////////////////
        //std::cout << "After deallocating 8 bytes" << std::endl;
        allocator.deallocate(ptr);
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        //////////////////////////////////////////////////////////////////
        //std::cout << "After allocating 24 bytes" << std::endl;
        auto ptr3 = allocator.allocate(24);
        if (!ptr3) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        //////////////////////////////////////////////////////////////////
        //std::cout << "After dellocating 16 bytes" << std::endl;
        allocator.deallocate(ptr2);
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        //////////////////////////////////////////////////////////////////
        //std::cout << "After allocating 32 bytes" << std::endl;
        auto ptr4 = allocator.allocate(32);
        if (!ptr4) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
        //std::cout << allocator.get_debug_info() << std::endl;
        //std::cout << "------------------------" << std::endl;
        //////////////////////////////////////////////////////////////////
        auto ptr5 = allocator.allocate(48);
        if (!ptr5) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
    }

    // COALESCE TEST NEGATIVE , NODE COUNT SHOULD INCREASE
    {
        Arena<> arena;
        LogicalPageAnySize<CoalescePolicy::COALESCE> allocator;

        bool success = allocator.create(arena.allocate(65536), 65536, 0);
        if (!success) { std::cout << "LogicalPageAnySize creation failed !!! " << std::endl; return -1; }

        auto ptr = allocator.allocate(8);
        if (!ptr) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
        auto ptr2 = allocator.allocate(16);
        if (!ptr2) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        unit_test.test_equals(allocator.get_node_count(), 1, "LogicalPageAnySize coalescing negative", "Node count before coalescing");
        //std::cout << allocator.get_debug_info() << std::endl << "----------------------" << std::endl;
        allocator.deallocate(ptr); // ptr2 is just between ptr1 and existing node , sine they are not adjacent coalescing will not work
        //std::cout << allocator.get_debug_info() << std::endl << "----------------------" << std::endl;
        unit_test.test_equals(allocator.get_node_count(), 2, "LogicalPageAnySize coalescing negative", "Node count after coalescing");
    }

    // STL VECTOR LIKE ALLOCATIONS
    {
        Arena<> arena;
        LogicalPageAnySize<CoalescePolicy::COALESCE> allocator;

        bool success = allocator.create(arena.allocate(65536), 65536, 0);
        if (!success) { std::cout << "LogicalPageAnySize creation failed !!! " << std::endl; return -1; }

        auto ptr = allocator.allocate(16);
        if (!ptr) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        auto ptr2 = allocator.allocate(8);
        if (!ptr2) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        auto ptr3 = allocator.allocate(16);
        if (!ptr3) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        allocator.deallocate(ptr2);

        auto ptr4 = allocator.allocate(24);
        if (!ptr4) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        allocator.deallocate(ptr3); // or ptr

        auto ptr5 = allocator.allocate(32);
        if (!ptr5) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        allocator.deallocate(ptr4);

        auto ptr6 = allocator.allocate(48);
        if (!ptr6) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        allocator.deallocate(ptr5);

        auto ptr7 = allocator.allocate(72);
        if (!ptr7) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        allocator.deallocate(ptr6);

        auto ptr8 = allocator.allocate(104);
        if (!ptr8) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        allocator.deallocate(ptr7);

        auto ptr9 = allocator.allocate(152);
        if (!ptr9) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        allocator.deallocate(ptr8);

        auto ptr10 = allocator.allocate(24);
        if (!ptr10) { std::cout << "Allocation failed !!!" << std::endl; return -1; }

        allocator.deallocate(ptr9);

        auto ptr11 = allocator.allocate(336);
        if (!ptr11) { std::cout << "Allocation failed !!!" << std::endl; return -1; }
    }

    //
    {
        Arena<> arena;
        LogicalPageAnySize<> allocator;

        std::vector<uint64_t> pointers;
        bool success = allocator.create(arena.allocate(65536), 65536, 0);

        if (!success) { std::cout << "LogicalPageAnySize creation failed !!! " << std::endl; return -1; }

        for (std::size_t i = 1; i < 1025; i++)
        {
            auto ptr = allocator.allocate(i);

            if (ptr)
            {
                success = validate_buffer(ptr, i);

                if (success)
                {
                    pointers.push_back(reinterpret_cast<uint64_t>(ptr));
                }
                else
                {
                    std::cout << "FREELIST ALLOCATION FAILED !!!" << std::endl;
                    return -1;
                }
            }
            else
            {
                break; // Exhausted
            }
        }

        for (const auto& pointer : pointers)
        {
            allocator.deallocate(reinterpret_cast<void*>(pointer));
        }
    }

    return 0;
}

#endif