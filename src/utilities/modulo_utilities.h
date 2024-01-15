#ifndef __MODULO_UTILITIES_H__
#define __MODULO_UTILITIES_H__

#include <cassert>
#include <cstddef>
#include "../compiler/builtin_functions.h"
#include "pow2_utilities.h"

class ModuloUtilities
{
    public:
        // "second" should be power of 2
        static std::size_t modulo_pow2(std::size_t first, std::size_t second)
        {
            assert(Pow2Utilities::is_power_of_two(second));
            return first & (second - 1);
        }

        static std::size_t modulo(std::size_t first, std::size_t second)
        {
            std::size_t shift = builtin_ctzl(static_cast<unsigned long>(second));
            std::size_t remainder = first - ((first >> shift) << shift);
            return remainder;
        }
};

#endif