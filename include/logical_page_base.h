//BASE CLASS AND INTERFACE FOR LOGICAL PAGES
#ifndef __LOGICAL_PAGE_BASE_H__
#define __LOGICAL_PAGE_BASE_H__

#include <cstddef>
#include <cstdint>
#include "compiler/hints_hot_code.h"
#include "logical_page_header.h"

template <typename LogicalPageImplementation, typename NodeType>
class LogicalPageBase
{
    public:
        LogicalPageBase()
        {
            m_page_header.initialise();
        }

        ~LogicalPageBase() {}

        LogicalPageBase(const LogicalPageBase& other) = delete;
        LogicalPageBase& operator= (const LogicalPageBase& other) = delete;
        LogicalPageBase(LogicalPageBase&& other) = delete;
        LogicalPageBase& operator=(LogicalPageBase&& other) = delete;

        bool owns_pointer(void* ptr)
        {
            uint64_t address_in_question = reinterpret_cast<uint64_t>(ptr);

            if ( address_in_question >= m_page_header.m_logical_page_start_address && address_in_question < m_page_header.m_logical_page_start_address + m_page_header.m_logical_page_size )
            {
                return true;
            }

            return false;
        }

        bool can_be_recycled() { return m_page_header.get_flag<LogicalPageHeaderFlags::IS_USED>() == false; }

        void mark_as_used() { m_page_header.set_flag<LogicalPageHeaderFlags::IS_USED>();  }
        void mark_as_non_used() { m_page_header.clear_flag<LogicalPageHeaderFlags::IS_USED>(); }

        void mark_as_locked() { m_page_header.set_flag<LogicalPageHeaderFlags::IS_LOCKED>(); }
        void mark_as_non_locked() { m_page_header.clear_flag<LogicalPageHeaderFlags::IS_LOCKED>(); }

        uint64_t get_used_size() const { return m_page_header.m_used_size; }
        uint32_t get_size_class() { return m_page_header.m_size_class; }

        uint64_t get_next_logical_page() const { return m_page_header.m_next_logical_page_ptr; }
        void set_next_logical_page(void* address) { m_page_header.m_next_logical_page_ptr = reinterpret_cast<uint64_t>(address); }

        uint64_t get_previous_logical_page() const { return m_page_header.m_prev_logical_page_ptr; }
        void set_previous_logical_page(void* address) { m_page_header.m_prev_logical_page_ptr = reinterpret_cast<uint64_t>(address); }

        #ifdef UNIT_TEST
        const std::string get_type_name() const { return static_cast<LogicalPageImplementation*>(this)->get_type_name(); }
        std::size_t capacity() const { return m_capacity; }
        NodeType* get_head_node() { return reinterpret_cast<NodeType*>(m_page_header.m_head); };
        #endif

    protected:
        LogicalPageHeader m_page_header;

        #ifdef UNIT_TEST
        std::size_t m_capacity = 0;
        #endif
};

#endif