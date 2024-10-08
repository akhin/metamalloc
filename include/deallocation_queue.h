/*
    UNBOUNDED THREAD SAFE QUEUE FOR STORING 64 BIT POINTERS
    STORES THEM IN A DOUBLY LINKED LIST OF 64KB "POINTER PAGE"S
*/
#ifndef __DEALLOCATION_QUEUE__
#define __DEALLOCATION_QUEUE__

#include <cstddef>
#include <cstdint>
#include "compiler/builtin_functions.h"
#include "compiler/hints_branch_predictor.h"
#include "compiler/packed.h"
#include "compiler/hints_hot_code.h"
#include "utilities/lockable.h"

#ifdef ENABLE_PERF_TRACES // VOLTRON_EXCLUDE
#include <cstdio>
#endif // VOLTRON_EXCLUDE

// FIRST 16 BYTES OF EACH PAGE IS "next" and "prev" PTRS
// THE REST WILL BE USED TO STORE POINTERS
PACKED
(
    struct PointerPage
    {
        static constexpr std::size_t POINTER_CAPACITY = 8190; // 8190=(65536-(2*8))/8
        PointerPage* m_next = nullptr;
        PointerPage* m_prev = nullptr;
        uint64_t m_pointers[POINTER_CAPACITY] = { 0 };
    }
);

template <typename AllocatorType>
class DeallocationQueue : public Lockable<LockPolicy::USERSPACE_LOCK>
{
    public:

        DeallocationQueue() = default;

        ~DeallocationQueue()
        {
            this->enter_concurrent_context();
            auto iter = m_head;
            while (iter)
            {
                auto next = iter->m_next;
                AllocatorType::deallocate(iter, sizeof(PointerPage));
                iter = next;
            }
            this->leave_concurrent_context();
        }

        [[nodiscard]] bool create(std::size_t initial_pointer_page_count, void* external_buffer = nullptr)
        {
            if (!initial_pointer_page_count) { return false; }

            PointerPage* buffer = nullptr;

            if (external_buffer == nullptr)
            {
                buffer = reinterpret_cast<PointerPage*>(AllocatorType::allocate(initial_pointer_page_count * sizeof(PointerPage)));

                if (buffer == nullptr)
                {
                    return false;
                }
            }
            else
            {
                buffer = reinterpret_cast<PointerPage*>(external_buffer);
            }

            builtin_memset(reinterpret_cast<void*>(buffer), 0, initial_pointer_page_count * sizeof(PointerPage));

            m_head = buffer;
            m_head->m_next = nullptr;
            m_head->m_prev = nullptr;

            auto iter = m_head;

            for (std::size_t i = 0; i < initial_pointer_page_count; i++)
            {
                if (i + 1 < initial_pointer_page_count)
                {
                    auto* next = &(buffer[i + 1]);
                    iter->m_next = next;
                    next->m_prev = iter;
                }
                else
                {
                    iter->m_next = nullptr;
                }
            }

            m_active_page = m_head;

            return true;
        }

        FORCE_INLINE void push(void* pointer)
        {
            this->enter_concurrent_context();
            ////////////////////////////////////
            if (unlikely(m_active_page_used_count == PointerPage::POINTER_CAPACITY))
            {
                // We need to start using a new page

                if (m_active_page->m_next == nullptr)
                {
                    #ifdef ENABLE_PERF_TRACES // INSIDE ALLOCATION CALLSTACK SO CAN'T ALLOCATE MEMORY HENCE OUTPUT TO stderr
                    fprintf(stderr, "deallocation queue grow\n");
                    #endif
                    // We need to allocate a new page
                    auto new_page = reinterpret_cast<PointerPage*>(AllocatorType::allocate(sizeof(PointerPage)));
                    new_page->m_next = nullptr;

                    m_active_page->m_next = new_page;
                    new_page->m_prev = m_active_page;
                    m_active_page = new_page;
                }
                else
                {
                    m_active_page = m_active_page->m_next;
                }

                m_active_page_used_count = 0;
            }

            m_active_page->m_pointers[m_active_page_used_count] = reinterpret_cast<uint64_t>(pointer);
            m_active_page_used_count++;
            ////////////////////////////////////
            this->leave_concurrent_context();
        }

        FORCE_INLINE [[nodiscard]] void* pop()
        {
            void* ret = nullptr;
            this->enter_concurrent_context();
            ////////////////////////////////////
            if (m_active_page_used_count == 0)
            {
                if (m_head == m_active_page)
                {
                    this->leave_concurrent_context();
                    return nullptr;
                }
                else
                {
                    m_active_page = m_active_page->m_prev;
                    m_active_page_used_count = PointerPage::POINTER_CAPACITY;
                }
            }

            ret = reinterpret_cast<void*>(m_active_page->m_pointers[m_active_page_used_count - 1]);
            m_active_page_used_count--;

            ////////////////////////////////////
            this->leave_concurrent_context();
            return ret;
        }

        DeallocationQueue(const DeallocationQueue& other) = delete;
        DeallocationQueue& operator= (const DeallocationQueue& other) = delete;
        DeallocationQueue(DeallocationQueue&& other) = delete;
        DeallocationQueue& operator=(DeallocationQueue&& other) = delete;

    private:
        PointerPage* m_head = nullptr;
        PointerPage* m_active_page = nullptr;
        std::size_t m_active_page_used_count = 0;
};

#endif