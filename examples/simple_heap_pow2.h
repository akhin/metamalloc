/*
    - HAS BINS WHERE EACH BIN MANAGES A POWER-OF-TWO SIZECLASS

    - LOGICAL PAGES ARE PLACED ON ADDRESSES WHICH ARE ALIGNED TO THEIR LOGICAL PAGE SIZES. 
      THEREFORE SEGMENT WILL TAKE ADVANTAGE OF IT DURING DEALLOCATIONS BY  DIRECTLY ACCESSING A LOGICAL PAGE HEADER BY BITWISE-APPLYING A MASK
      (SEE THE LAST TEMPLATE ARG OF SegmentSmallObject/ CLASS SEGMENT.)
*/
#ifndef __SIMPLE_HEAP_POW2_H__
#define __SIMPLE_HEAP_POW2_H__

#include <cstddef>
#include <cstdint>
#include <metamalloc.h>
using namespace metamalloc;

template <
            ConcurrencyPolicy concurrency_policy = ConcurrencyPolicy::SINGLE_THREAD,
            typename ArenaType = Arena<>,
            PageRecyclingPolicy page_recycling_policy = PageRecyclingPolicy::IMMEDIATE
        >
class SimpleHeapPow2 : public HeapBase<SimpleHeapPow2<concurrency_policy, ArenaType, page_recycling_policy>, concurrency_policy> // CRTP derivation
{
    public:

        SimpleHeapPow2() = default;
        SimpleHeapPow2(const SimpleHeapPow2& other) = delete;
        SimpleHeapPow2(SimpleHeapPow2&& other) = delete;
        ~SimpleHeapPow2() = default;
        SimpleHeapPow2& operator= (const SimpleHeapPow2& other) = delete;       
        SimpleHeapPow2& operator=(SimpleHeapPow2&& other) = delete;

        using SegmentType = Segment <concurrency_policy, LogicalPage<>, ArenaType, page_recycling_policy, true>; // The last is true as we place logical pages at "logical page size" aligned addresses

        static constexpr std::size_t MIN_SIZE_CLASS = 16;
        static constexpr std::size_t BIN_COUNT = 12; // 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768
        static constexpr std::size_t MAX_BIN_INDEX = BIN_COUNT - 1;

        struct HeapCreationParams
        {
            // BINS
            std::size_t m_logical_page_size = 65536;
            std::size_t m_bin_logical_page_counts[BIN_COUNT] = { 1,1,1,1,1,1,1,1,1,1,1,1 };
            // SEGMENT LEVEL
            std::size_t m_logical_page_recycling_threshold = 0;
            double m_segment_grow_coefficient = 1.0;
            std::size_t m_segment_deallocation_queue_initial_capacity = 65536; // applies in thread-local case
        };

        [[nodiscard]] bool create(const HeapCreationParams& params, ArenaType* arena)
        {
            //////////////////////////////////////////////////////////////////////////////////////////////
            // 1. CHECKS
            if (arena == nullptr || params.m_logical_page_size <= 0 || arena->page_alignment() != params.m_logical_page_size)
            {
                return false;
            }

            // Logical page sizes should be multiples of page allocation granularity ( 4KB on Linux ,64 KB on Windows )
            if (!MultipleUtilities::is_size_a_multiple_of_page_allocation_granularity(params.m_logical_page_size) )
            {
                return false;
            }

            m_logical_page_size = params.m_logical_page_size;

            //////////////////////////////////////////////////////////////////////////////////////////////
            // 2. CALCULATE REQUIRED BUFFER SIZE
            std::size_t required_buffer_size{ 0 };
            std::size_t size_class = MIN_SIZE_CLASS;

            for (std::size_t i = 0; i < BIN_COUNT; i++)
            {
                required_buffer_size += (params.m_bin_logical_page_counts[i] * m_logical_page_size);
                size_class = size_class << 1;
            }

            this->m_buffer_length = required_buffer_size;

            //////////////////////////////////////////////////////////////////////////////////////////////
            // 3. ALLOCATE BUFFER
            this->m_buffer_address = reinterpret_cast<uint64_t>(arena->allocate(this->m_buffer_length));

            //////////////////////////////////////////////////////////////////////////////////////////////
            // 4. DISTRIBUTE BUFFER TO BINS
            std::size_t buffer_index{ 0 };
            size_class = MIN_SIZE_CLASS;
            
            for (std::size_t i = 0; i < BIN_COUNT; i++)
            {
                auto required_logical_page_count = params.m_bin_logical_page_counts[i];
                auto bin_buffer_size = required_logical_page_count * m_logical_page_size;

                SegmentCreationParameters segment_params;
                segment_params.m_size_class = static_cast<uint32_t>(size_class);
                segment_params.m_logical_page_count = required_logical_page_count;
                segment_params.m_logical_page_size = params.m_logical_page_size;
                segment_params.m_page_recycling_threshold = params.m_logical_page_recycling_threshold;
                segment_params.m_grow_coefficient = params.m_segment_grow_coefficient;
                segment_params.m_deallocation_queue_initial_capacity = params.m_segment_deallocation_queue_initial_capacity;

                bool success = m_bins[i].create(reinterpret_cast<char*>(this->m_buffer_address) + buffer_index, arena, segment_params);

                if (!success)
                {
                    return false;
                }

                buffer_index += bin_buffer_size;
                size_class = size_class << 1;
            }

            return true;
        }

        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size)
        {
            std::size_t adjusted_size = Pow2Utilities::get_first_pow2_of(size);
            adjusted_size = adjusted_size < MIN_SIZE_CLASS ? MIN_SIZE_CLASS : adjusted_size;
            return m_bins[SizeUtilities::get_pow2_bin_index_from_size<MIN_SIZE_CLASS, MAX_BIN_INDEX>(adjusted_size)].allocate(adjusted_size);
        }

        // YOU DON'T NEED TO IMPLEMENT AN ALLOCATE METHOD THAT ACCEPTS AN ALIGNMENT PARAMETER, AS allocate_aligned IS IMPLEMENTED IN heap_base

        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE)
        void deallocate(void* ptr)
        {
            auto size_class = static_cast<std::size_t>(SegmentType::get_size_class_from_address(ptr, m_logical_page_size));
            m_bins[SizeUtilities::get_pow2_bin_index_from_size<MIN_SIZE_CLASS, MAX_BIN_INDEX>(size_class)].deallocate(ptr);
        }

        std::size_t get_usable_size(void* ptr)
        {
            auto size_class = static_cast<std::size_t>(SegmentType::get_size_class_from_address(ptr, m_logical_page_size));
            return size_class;
        }

        // You need to implement it in case it will be used in a thread caching allocator
        // If there are many short living threads in your application , the framework will transfer unused memory to the central heap by calling it
        void transfer_logical_pages_from(SimpleHeapPow2* from)
        {
            for (std::size_t i = 0; i < BIN_COUNT; i++)
            {
                m_bins[i].transfer_logical_pages_from(from->m_bins[i]);
            }
        }

        // You need to implement it if recycling policy is deferred instead of immediate
        void recycle()
        {
            for (std::size_t i = 0; i < BIN_COUNT; i++)
            {
                m_bins[i].recycle_free_logical_pages();
            }
        }

        std::size_t get_bin_logical_page_count(std::size_t bin_index)
        {
            return m_bins[bin_index].get_logical_page_count();
        }
        
        std::size_t get_max_allocation_size()
        {
            return LARGEST_SIZE_CLASS;
        }

    private:
        std::size_t m_logical_page_size = 0;
        SegmentType m_bins[BIN_COUNT];
        static constexpr inline std::size_t LARGEST_SIZE_CLASS = Pow2Utilities::compile_time_pow2<BIN_COUNT + 3>(); // +3 since we skip bin2 bin4 and bin8 as the sizeclasses start from 16
};

#endif