#ifndef __MULTIPLE_UTILITIES_H__
#define __MULTIPLE_UTILITIES_H__

#include <cassert>
#include <cstddef>
#include "../os/virtual_memory.h"
#include "modulo_utilities.h"
#include "pow2_utilities.h"

class MultipleUtilities
{
    public:

        // "multiple" should be power of 2
        static std::size_t get_next_pow2_multiple_of(std::size_t input, std::size_t multiple)
        {
            // Not checking if the given input is already a multiple , as "next" means that
            // we are called from a place that will add 'further bytes to that will 'grow' therefore no need to check
            assert(Pow2Utilities::is_power_of_two(multiple));
            return ((input + multiple - 1) & ~(multiple - 1));
        }

        static bool is_size_a_multiple_of_page_allocation_granularity(std::size_t input)
        {
            return ModuloUtilities::modulo_pow2(input, VirtualMemory::PAGE_ALLOCATION_GRANULARITY) == 0;
        }
};

#endif