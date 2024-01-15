/*
    - IT IS A FIRST-IN-LAST-OUT FREELIST IMPLEMENTATION. IT CAN HOLD ONLY ONE SIZE CLASS. ALLOCATE METHOD WILL IGNORE THE SIZE PARAMETER

    - IF THE PASSED BUFFER IS START OF A VIRTUAL PAGE AND THE PASSED SIZE IS A VM PAGE SIZE , THEN IT WILL BE CORRESPONDING TO AN ACTUAL VM PAGE
      IDEAL USE CASE IS ITS CORRESPONDING TO A VM PAGE / BEING VM PAGE ALIGNED. SO THAT A SINGLE PAYLOAD WILL NOT SPREAD TO DIFFERENT VM PAGES

    - IF THE PASSED BUFFER SIZE IS A MULTIPLE OF VM PAGE SIZE, THEN IT CAN HOLD MULTIPLE CONTIGUOUS VM PAGES, SIMILARLY TO SPANS/PAGE RUNS

    - MINIMUM ALLOCATION SIZE IS 8 BYTES. ( THAT IS BECAUSE A POINTER IN 64 BIT IS 8 BYTES )

    - DOESN'T SUPPORT ALIGNMENT. ALLOCATE METHOD WILL IGNORE THE ALIGNMENT PARAMETER

    - METADATA USAGE : 32 BYTES (PAGE HEADER) PER LOGICAL PAGE
*/
#ifndef __LOGICAL_PAGE_H__
#define __LOGICAL_PAGE_H__

#include <cstddef>
#include <cstdint>
#include "compiler/unused.h"
#include "compiler/packed.h"
#include "compiler/hints_hot_code.h"
#include "compiler/hints_branch_predictor.h"
#include "cpu/alignment_constants.h"
#include "utilities/alignment_checks.h"
#include "logical_page_base.h"

#ifdef UNIT_TEST // VOLTRON_EXCLUDE
#include <string>
#endif // VOLTRON_EXCLUDE

PACKED
(
    struct LogicalPageNode      // No private members/method to stay as POD+PACKED
    {
        LogicalPageNode* m_next = nullptr;      // When not allocated , first 8 bytes will hold address of the next node
                                                // When allocated , 8 bytes + chunksize-8 bytes will be available to hold data
    }
);

template <typename NodeType = LogicalPageNode>
class LogicalPage : public LogicalPageBase<LogicalPage<NodeType>, LogicalPageNode>
{
    public:
        LogicalPage() {}
        ~LogicalPage() {}

        LogicalPage(const LogicalPage& other) = delete;
        LogicalPage& operator= (const LogicalPage& other) = delete;
        LogicalPage(LogicalPage&& other) = delete;
        LogicalPage& operator=(LogicalPage&& other) = delete;

        // Gets its memory from an external source such as a heap's arena
        [[nodiscard]] bool create(void* buffer, const std::size_t buffer_size, uint32_t size_class)
        {
            // Chunk size can't be smaller than a 'next' pointer-or-offset which is 64bit
            if (buffer == nullptr || buffer_size < size_class || size_class < sizeof(uint64_t))
            {
                return false;
            }

            void* buffer_start_including_header = reinterpret_cast<void*>(reinterpret_cast<std::size_t>(buffer) - sizeof(*this)); // In case a caller is placing this object instance and the memory held by this instance sequentially

            if (!AlignmentChecks::is_address_page_allocation_granularity_aligned(buffer) && !AlignmentChecks::is_address_page_allocation_granularity_aligned(buffer_start_including_header))
            {
                return false; // You have to pass start of actual virtual memory pages which will be page size aligned
            }

            this->m_page_header.initialise();
            this->m_page_header.m_size_class = size_class;
            this->m_page_header.m_logical_page_start_address = reinterpret_cast<uint64_t>(buffer);
            this->m_page_header.m_logical_page_size = buffer_size;

            #ifdef UNIT_TEST
            this->m_capacity = buffer_size;
            #endif
            grow(buffer, buffer_size);

            return true;
        }

        // We don't use size as we always allocate a fixed size chunk
        // We need the paramaters in the method to conform the common logical page interface
        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(const std::size_t size)
        {
            UNUSED(size);
            NodeType* free_node = pop();

            if (unlikely(free_node == nullptr))
            {
                return nullptr;
            }

            this->m_page_header.m_used_size += this->m_page_header.m_size_class;

            return  reinterpret_cast<void*>(free_node);
        }

        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE)
        void deallocate(void* ptr)
        {
            if( unlikely(this->owns_pointer(ptr) == false) )
            {
                return;
            }

            this->m_page_header.m_used_size -= this->m_page_header.m_size_class;
            push(static_cast<NodeType*>(ptr));
        }

        std::size_t get_usable_size(void* ptr) { return  static_cast<std::size_t>(this->m_page_header.m_size_class); }
        static constexpr bool supports_any_size() { return false; }

        #ifdef UNIT_TEST
        const std::string get_type_name() const { return "LogicalPage"; }
        #endif

    private:

        void grow(void* buffer, std::size_t buffer_size)
        {
            const std::size_t chunk_count = buffer_size / this->m_page_header.m_size_class;

            for (std::size_t i = 0; i < chunk_count; ++i)
            {
                std::size_t address = reinterpret_cast<std::size_t>(buffer) + i * this->m_page_header.m_size_class;
                push(reinterpret_cast<NodeType*>(address));
            }
        }

        FORCE_INLINE void push(NodeType* new_node)
        {
            new_node->m_next = reinterpret_cast<NodeType*>(this->m_page_header.m_head);
            this->m_page_header.m_head = reinterpret_cast<uint64_t>(new_node);
        }

        FORCE_INLINE NodeType* pop()
        {
            if(unlikely(this->m_page_header.m_head == 0))
            {
                return nullptr;
            }

            NodeType* top = reinterpret_cast<NodeType*>(this->m_page_header.m_head);
            this->m_page_header.m_head = reinterpret_cast<uint64_t>(top->m_next);
            return top;
        }
};

#endif