#ifndef __POW2_UTILITIES_H__
#define __POW2_UTILITIES_H__

#include <cstddef>

class Pow2Utilities
{
    public:

        template <std::size_t N>
        static constexpr std::size_t compile_time_pow2()
        {
            return 1 << N;
        }

        // Reference : https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
        static std::size_t get_first_pow2_of(std::size_t input)
        {
            if (input <= 1)
            {
                return 1;
            }

            input--;
            input |= input >> 1;
            input |= input >> 2;
            input |= input >> 4;
            input |= input >> 8;
            input |= input >> 16;

            return input + 1;
        }

        static bool is_power_of_two(std::size_t input)
        {
            if (input == 0)
            {
                return false;
            }

            return (input & (input - 1)) == 0;
        }

        template <std::size_t N>
        static constexpr bool compile_time_is_power_of_two()
        {
            return N && ((N & (N - 1)) == 0);
        }
};

#endif