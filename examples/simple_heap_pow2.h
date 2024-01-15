/*
    - THE 1ST LAYER OF SEGREGATION IS SMALL OBJECT / BIG OBJECT DISTINCTION :
      MOST OBJECTS ARE "SMALL OBJECTS" ( UP TO 2048 BYTES ). OBJECTS LARGER THAN 2K ARE CLASSIFIED AS "BIG" OBJECTS
      (MULTIPLE SMALL OBJECTS CAN FIT INTO A SINGLE VIRTUAL MEMORY PAGE UNLIKE BIG OBJECTS)

    - THE 2ND LAYER OF SEGREGATION IS FOR SMALL OBJECTS. THEY ARE SEGREGATED INTO BINS WHERE EACH BIN MANAGES A POWER-OF-TWO SIZECLASS

    - FOR ALIGNED ALLOCATIONS, CALL "allocate_aligned" WHICH IS IMPLEMENTED IN HEAP_BASE

    - THE CHOSEN BIG OBJECT LOGICAL PAGE SIZE HAS TO BE A MULTIPLE OF SMALL OBJECT LOGICAL PAGE SIZE. IT ALSO HAS TO BE 16 BYTES GREATER THAN MAX EXPECTED ALLOCATION SIZE DUE TO ALLOCATION HEADER.

    - FOR SMALL OBJECT DEALLOCATIONS,  LOGICAL PAGES ARE PLACED ON ADDRESSES WHICH ARE ALIGNED TO THEIR LOGICAL PAGE SIZES. SEE THE LAST TEMPLATE ARG OF SegmentSmallObject/ CLASS SEGMENT.
      THEREFORE SEGMENT WILL TAKE ADVANTAGE OF IT DURING DEALLOCATIONS BY  DIRECTLY ACCESSING A LOGICAL PAGE HEADER BY BITWISE-APPLYING A MASK
*/
#ifndef __SIMPLE_HEAP_POW2_H__
#define __SIMPLE_HEAP_POW2_H__

#include <cstddef>
#include <cstdint>

#include <metamalloc.h>
using namespace metamalloc;

class SimpleHeapPow2Constants
{
    public:
        static constexpr std::size_t SMALL_OBJECT_MIN_SIZE_CLASS = 16;
        static constexpr std::size_t SMALL_OBJECT_BIN_COUNT = 8; // 16 32 64 128 256 512 1024 2048
        static constexpr std::size_t MAX_SMALL_OBJECT_BIN_INDEX = SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT - 1;
        static constexpr std::size_t LARGEST_SMALL_OBJECT_SIZE_CLASS = Pow2Utilities::compile_time_pow2<SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT + 3>(); // +2 is because our first bin is bin8 , so we skip bin2 bin4 and bin8
};

template <
            ConcurrencyPolicy concurrency_policy = ConcurrencyPolicy::SINGLE_THREAD,
            typename ArenaType = Arena<>,
            typename SmallObjectLogicalPageType = LogicalPage<>,
            typename BigObjectLogicalPageType = LogicalPageAnySize<>,
            PageRecyclingPolicy page_recycling_policy = PageRecyclingPolicy::IMMEDIATE
        >
class SimpleHeapPow2 : public HeapBase<SimpleHeapPow2<concurrency_policy, ArenaType, SmallObjectLogicalPageType, BigObjectLogicalPageType, page_recycling_policy>, concurrency_policy>
{
    public:

        SimpleHeapPow2()
        {
            // No size segregation for big objects
            static_assert(BigObjectLogicalPageType::supports_any_size());
        }
        ~SimpleHeapPow2() {}

        SimpleHeapPow2(const SimpleHeapPow2& other) = delete;
        SimpleHeapPow2& operator= (const SimpleHeapPow2& other) = delete;
        SimpleHeapPow2(SimpleHeapPow2&& other) = delete;
        SimpleHeapPow2& operator=(SimpleHeapPow2&& other) = delete;

        struct HeapCreationParams
        {
            // SMALL OBJECTS
            std::size_t m_small_object_logical_page_size = 65536;
            // Small object capacities , if a specific bin size specified , that specific value will be used
            // otherwise m_small_object_capacity_per_size_class will be used
            std::size_t m_small_object_bin_page_counts[SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT] = { 1,1,1,1,1,1,1,1 };
            std::size_t m_small_object_page_recycling_threshold = 0;
            // BIG OBJECTS
            std::size_t m_big_object_logical_page_size = 196608;
            std::size_t m_big_object_page_recycling_threshold = 0;
            // OTHERS / SEGMENT LEVEL
            double m_segment_grow_coefficient = 1.0;
            std::size_t m_segment_deallocation_queue_initial_capacity = 65536; // applies in thread-local case
        };

        using SegmentSmallObject = Segment <concurrency_policy, SmallObjectLogicalPageType, ArenaType, page_recycling_policy, true>;  //  We place small object logical pages at aligned addresses
        using SegmentBigObject   = Segment <concurrency_policy, BigObjectLogicalPageType,   ArenaType, page_recycling_policy, false>; //  But not for big object hence false

        [[nodiscard]] bool create(const HeapCreationParams& params, ArenaType* arena)
        {
            // Basic checks
            if (arena == nullptr || params.m_small_object_logical_page_size <= 0
                || params.m_small_object_logical_page_size < VirtualMemory::PAGE_ALLOCATION_GRANULARITY
                || arena->page_alignment() != params.m_small_object_logical_page_size
                || params.m_big_object_logical_page_size <= 0
                || ModuloUtilities::modulo(params.m_big_object_logical_page_size, params.m_small_object_logical_page_size) > 0)
            {
                return false;
            }

            // Logical page sizes should be multiples of page allocation granularity ( 4KB on Linux ,64 KB on Windows )
            if (!MultipleUtilities::is_size_a_multiple_of_page_allocation_granularity(params.m_small_object_logical_page_size) || !MultipleUtilities::is_size_a_multiple_of_page_allocation_granularity(params.m_big_object_logical_page_size))
            {
                return false;
            }

            m_small_object_logical_page_size = params.m_small_object_logical_page_size;
            m_big_object_logical_page_size = params.m_big_object_logical_page_size;

            this->m_buffer_length = calculate_required_buffer_size(params);
            this->m_buffer_address = reinterpret_cast<uint64_t>(arena->allocate(this->m_buffer_length));

            std::size_t buffer_index{ 0 };
            std::size_t size_class = SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS;

            // DISTRIBUTE BUFFER TO SMALL OBJECT BINS
            for (std::size_t i = 0; i < SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT; i++)
            {
                auto required_page_count = params.m_small_object_bin_page_counts[i];
                auto bin_buffer_size = required_page_count * m_small_object_logical_page_size;

                SegmentCreationParameters small_segment_params;
                small_segment_params.m_size_class = static_cast<uint32_t>(size_class);
                small_segment_params.m_logical_page_count = required_page_count;
                small_segment_params.m_logical_page_size = params.m_small_object_logical_page_size;
                small_segment_params.m_page_recycling_threshold = params.m_small_object_page_recycling_threshold;
                small_segment_params.m_grow_coefficient = params.m_segment_grow_coefficient;
                small_segment_params.m_deallocation_queue_initial_capacity = params.m_segment_deallocation_queue_initial_capacity;

                bool success = m_bins[i].create(reinterpret_cast<char*>(this->m_buffer_address) + buffer_index, arena, small_segment_params);

                if (!success)
                {
                    return false;
                }

                buffer_index += bin_buffer_size;
                size_class = size_class << 1;

            }
            // PROVIDE BUFFER FOR THE BIG OBJECT BIN
            SegmentCreationParameters big_segment_params;
            big_segment_params.m_size_class = 0;
            big_segment_params.m_logical_page_count = 1;
            big_segment_params.m_logical_page_size = params.m_big_object_logical_page_size;
            big_segment_params.m_page_recycling_threshold = params.m_big_object_page_recycling_threshold;

            bool success = m_big_object_bin.create(reinterpret_cast<char*>(this->m_buffer_address) + buffer_index, arena, big_segment_params);

            if (!success)
            {
                return false;
            }

            return true;
        }

        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size)
        {
            if (size <= SimpleHeapPow2Constants::LARGEST_SMALL_OBJECT_SIZE_CLASS)
            {
                // SMALL OBJECT
                std::size_t adjusted_size = adjust_size_for_small_object_bin(size);
                return m_bins[SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(adjusted_size)].allocate(adjusted_size);
            }
            else
            {
                // BIG OBJECT
                return m_big_object_bin.allocate(size);
            }
        }

        // YOU DON'T NEED TO IMPLEMENT AN ALLOCATE METHOD THAT ACCEPTS AN ALIGNMENT PARAMETER, AS allocate_aligned IS IMPLEMENTED IN heap_base

        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE)
        void deallocate(void* ptr)
        {
            // For local ( bounded ) heaps, owns_pointer is contant time
            // For central heaps, it is a linear search
            // However as long as num of big object logical pages are at a reasonable level , performance will be fine.
            if (m_big_object_bin.owns_pointer(ptr) == false)
            {
                auto size_class = static_cast<std::size_t>(SegmentSmallObject::get_size_class_from_address(ptr, m_small_object_logical_page_size));
                m_bins[SizeUtilities::get_pow2_bin_index_from_size<SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS, SimpleHeapPow2Constants::MAX_SMALL_OBJECT_BIN_INDEX>(size_class)].deallocate(ptr);
            }
            else
            {
                m_big_object_bin.deallocate(ptr);
            }
        }

        std::size_t get_usable_size(void* ptr)
        {
            if (m_big_object_bin.owns_pointer(ptr) == false)
            {
                auto size_class = static_cast<std::size_t>(SegmentSmallObject::get_size_class_from_address(ptr, m_small_object_logical_page_size));
                return size_class;
            }
            else
            {
                return m_big_object_bin.get_usable_size(ptr);
            }
        }

        // You need to implement it in case it will be used in a thread caching allocator
        // If there are many short living threads in your application , the framework will transfer unused memory to the central heap by calling it
        void transfer_logical_pages_from(SimpleHeapPow2* from)
        {
            m_big_object_bin.transfer_logical_pages_from(from->m_big_object_bin);

            for (std::size_t i = 0; i < SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT; i++)
            {
                m_bins[i].transfer_logical_pages_from(from->m_bins[i]);
            }
        }

        // You need to implement it if recycling policy is deferred instead of immediate
        void recycle()
        {
            m_big_object_bin.recycle_free_logical_pages();

            for (std::size_t i = 0; i < SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT; i++)
            {
                m_bins[i].recycle_free_logical_pages();
            }
        }

        #ifdef UNIT_TEST
        std::size_t get_small_object_bin_page_count(std::size_t bin_index)
        {
            return m_bins[bin_index].get_logical_page_count();
        }

        std::size_t get_big_object_bin_page_count()
        {
            return m_big_object_bin.get_logical_page_count();
        }
        #endif

        #ifdef ENABLE_STATS
        using SegmentStatsArray = std::array<SegmentStats, SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT+1>;
        SegmentStatsArray get_stats()
        {
            SegmentStatsArray ret;
            std::size_t counter = 0;
            std::size_t size_class = SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS;
            for (; counter < SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT; counter++)
            {
                ret[counter] = m_bins[counter].get_stats();
                ret[counter].m_size_class = size_class;
                size_class = size_class << 1;
            }

            ret[counter] = m_big_object_bin.get_stats();

            return ret;
        }
        #endif

    private:
        // Small object bins
        std::size_t m_small_object_logical_page_size = 0;
        SegmentSmallObject m_bins[SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT];
        // Big object bin
        std::size_t m_big_object_logical_page_size = 0;
        SegmentBigObject m_big_object_bin;

        std::size_t calculate_required_buffer_size(const HeapCreationParams& params)
        {
            std::size_t ret{ 0 };
            std::size_t size_class = SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS;

            for (std::size_t i = 0; i < SimpleHeapPow2Constants::SMALL_OBJECT_BIN_COUNT; i++)
            {
                auto required_page_count = params.m_small_object_bin_page_counts[i];
                ret += (required_page_count * m_small_object_logical_page_size);
                size_class = size_class << 1;
            }

            ret += (params.m_big_object_logical_page_size);

            return ret;
        }

    #ifndef UNIT_TEST
    private:
    #else
    public:
    #endif

        FORCE_INLINE std::size_t adjust_size_for_small_object_bin(const std::size_t original_size)
        {
            std::size_t adjusted_size = Pow2Utilities::get_first_pow2_of(original_size);
            adjusted_size = adjusted_size < SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS ? SimpleHeapPow2Constants::SMALL_OBJECT_MIN_SIZE_CLASS : adjusted_size;
            return adjusted_size;
        }
};

#endif