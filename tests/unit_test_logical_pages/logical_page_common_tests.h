#ifndef __LOGICAL_PAGE_COMMON_TESTS_H__
#define __LOGICAL_PAGE_COMMON_TESTS_H__

#include <string>
#include "../../include/arena.h"
#include "../../include/logical_page.h"
#include "../../include/os/thread_utilities.h"

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

template <typename LogicalPageType, typename NodeType>
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

template <typename LogicalPageType, typename NodeType>
bool test_general(std::size_t buffer_size)
{
    Arena arena;
    std::size_t BUFFER_SIZE = buffer_size;

    LogicalPageType logical_page;

    bool success = logical_page.create(arena.allocate(BUFFER_SIZE), BUFFER_SIZE, 128);

    std::string test_category = "allocation general ";

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

#endif