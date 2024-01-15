/*
    A HEAP WITH ONLY ONE SEGMENT AS A HELLO WORLD HEAP
*/
#ifndef __MINIMAL_HEAP_H__
#define __MINIMAL_HEAP_H__

#include <cstddef> // std::size_t
#include <cstdint> // uint64_t
#include <metamalloc.h>
using namespace metamalloc;

template <ConcurrencyPolicy concurrency_policy>
class MinimalHeap : public HeapBase<MinimalHeap<concurrency_policy>, concurrency_policy>
{
    public:

        MinimalHeap() {}
        ~MinimalHeap() {}
        MinimalHeap(const MinimalHeap& other) = delete;
        MinimalHeap& operator= (const MinimalHeap& other) = delete;
        MinimalHeap(MinimalHeap&& other) = delete;
        MinimalHeap& operator=(MinimalHeap&& other) = delete;

        using SegmentType = Segment<concurrency_policy, LogicalPageAnySize<>, Arena<>>;

        struct HeapCreationParams
        {
            std::size_t total_size = 0;
        };

        [[nodiscard]] bool create(const HeapCreationParams& params, Arena<>* arena_ptr)
        {
            m_arena = arena_ptr;

            this->m_buffer_length = params.total_size;
            this->m_buffer_address = reinterpret_cast<uint64_t>(m_arena->allocate(this->m_buffer_length));

            SegmentCreationParameters segment_params;
            segment_params.m_size_class = 0;
            segment_params.m_logical_page_count = 1;
            segment_params.m_logical_page_size = 65536;
            segment_params.m_page_recycling_threshold = 1;

            bool ret = m_segment.create(reinterpret_cast<char*>(this->m_buffer_address), m_arena, segment_params);
            return ret;
        }

        [[nodiscard]] void* allocate(std::size_t size)
        {
            return m_segment.allocate(size);
        }

        // Aligned allocations will be handled by HeapBase::allocate_aligned

        void deallocate(void* ptr)
        {
            m_segment.deallocate(ptr);
        }

        // You need to implement it in case it will be used in a thread caching allocator
        // If there are many short living threads in your application , the framework will transfer unused memory to the central heap by calling it
        void transfer_logical_pages_from(MinimalHeap* from)
        {
            m_segment.transfer_logical_pages_from(from->m_segment);
        }

        // You need to implement it if recycling policy is deferred instead of immediate
        void recycle()
        {
            m_segment.recycle_free_logical_pages();
        }

        std::size_t get_usable_size(void* ptr)
        {
            return m_segment.get_usable_size(ptr);
        }

    private:
        SegmentType m_segment;
        Arena<>* m_arena = nullptr;
};

#endif