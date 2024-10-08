#ifndef __LOG2_UTILITIES_H__
#define __LOG2_UTILITIES_H__

#include <cstddef>
#include "../compiler/builtin_functions.h"

class Log2Utilities
{
    public:

        static constexpr unsigned int compile_time_log2(unsigned int n)
        {
            return (n <= 1) ? 0 : 1 + compile_time_log2(n / 2);
        }

        static std::size_t log2_power_of_two(std::size_t input)
        {
            // IMPLEMENTATION IS FOR 64 BIT ONLY
            return 63 - builtin_clzl(static_cast<unsigned long>(input));
        }

        static std::size_t log2(std::size_t n)
        {
            std::size_t result = 0;
            while (n >>= 1)
            {
                ++result;
            }
            return result;
        }
};

#endif