#ifndef __SIZE_UTILITIES_H__
#define __SIZE_UTILITIES_H__

#include <cstddef>
#include <cmath>
#include "log2_utilities.h"

#ifdef ENABLE_STATS // VOLTRON_EXCLUDE
#include <string>
#include <sstream>
#include <iomanip>
#endif // VOLTRON_EXCLUDE

class SizeUtilities
{
    public:

        // When your heap is size segregated , where each size is a pow2,
        // you need to calculate zero based bin index from a given size
        //
        // Ex: if size is 96 & min size class is 16, then we shall return 3 ( 4th bin , bin1:16s bin2:32s, bin3:64s, bin4:128s ...)
        //
        template <std::size_t MINIMUM_SIZE_CLASS, std::size_t MAX_BIN_INDEX>
        static std::size_t get_pow2_bin_index_from_size(std::size_t size)
        {
            std::size_t index = Log2Utilities::log2_power_of_two(size) - Log2Utilities::compile_time_log2(MINIMUM_SIZE_CLASS) ;
            index = index > MAX_BIN_INDEX ? MAX_BIN_INDEX : index;
            return index;
        }

        static std::size_t get_required_page_count_for_allocation(std::size_t page_size, std::size_t page_header_size, std::size_t object_size, std::size_t object_count)
        {
            std::size_t object_count_per_page = static_cast<std::size_t>(std::ceil( (page_size - page_header_size) / object_size));
            std::size_t needed_page_count = static_cast<std::size_t>(std::ceil(static_cast<double>(object_count) / static_cast<double>(object_count_per_page)));

            if (needed_page_count == 0)
            {
                needed_page_count = 1;
            }

            return needed_page_count;
        }

        #ifdef ENABLE_STATS
        static const std::string get_human_readible_size(std::size_t size_in_bytes)
        {
            const char* suffixes[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
            constexpr int suffix_count = sizeof(suffixes) / sizeof(suffixes[0]);

            std::ostringstream oss;

            double size = static_cast<double>(size_in_bytes);
            constexpr int multiplier = 1024;
            int suffix_index = 0;

            while (size >= multiplier && suffix_index < suffix_count - 1)
            {
                size /= multiplier;
                suffix_index++;
            }

            oss << std::fixed << std::setprecision(2) << size << ' ' << suffixes[suffix_index];
            return oss.str();
        }
        #endif
};

#endif