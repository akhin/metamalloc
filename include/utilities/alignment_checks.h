#ifndef __ALIGNMENT_CHECKS_H__
#define __ALIGNMENT_CHECKS_H__

#include <cstddef>
#include "../os/virtual_memory.h"

class AlignmentChecks
{
    public:

        static bool is_address_aligned(void* address, std::size_t alignment)
        {
            std::size_t alignment_mask = alignment - 1;
            std::size_t address_value = reinterpret_cast<std::size_t>(address);
            return (address_value & alignment_mask) == 0;
        }

        static bool is_address_page_allocation_granularity_aligned(void* address)
        {
            return is_address_aligned(address, VirtualMemory::PAGE_ALLOCATION_GRANULARITY);
        }
};

#endif