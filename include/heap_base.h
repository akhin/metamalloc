//INTERFACE FOR HEAPS WITH ONE CONCRETE IMPLEMENTATION : HeapBase::allocate_aligned
#ifndef __HEAP_BASE_H__
#define __HEAP_BASE_H__

#include "compiler/hints_hot_code.h"
#include "cpu/alignment_constants.h"
#include <cstdint>
#include <cstddef>
#include "utilities/modulo_utilities.h"
#include "segment.h" // Concurrency policy

template <typename HeapImplementation, ConcurrencyPolicy concurrency_policy = ConcurrencyPolicy::SINGLE_THREAD>
class HeapBase
{
    public:
        HeapBase() = default;
        ~HeapBase() = default;
        HeapBase(const HeapBase& other) = delete;
        HeapBase& operator= (const HeapBase& other) = delete;
        HeapBase(HeapBase&& other) = delete;
        HeapBase& operator=(HeapBase&& other) = delete;

        [[nodiscard]] void* allocate(std::size_t size) { return static_cast<HeapImplementation*>(this)->allocate(size); }

        /*
            Alignment has to be a power of two

            Note that if alignment sizes are too large , than memory will be wasted in unncesserily big padding bytes.
            If that is the case , override this method in your CRTP derived heap class
            And call this base method only if alignment size is small. Otherwise redirect it to a bin that uses logical_page_any_size ( for ex big object)
            as also logical_page_any_size can handle alignments and it will also be minimising used padding bytes during its search in its freelist
            
            As for deallocations, since LogicalPage holds free chunks for only one sizeclass and since it knows it start address, it re-adjusts the pointer during deallocation
        */
        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate_aligned(std::size_t size, std::size_t alignment)
        {
            if (alignment <= MINIMUM_ALIGNMENT)
            {
                // Framework already provides minimum 16 bit alignment
                return allocate(size);
            }

            std::size_t adjusted_size = size + alignment; // Adding padding bytes
            /////////////////////////////////////////////
            auto ptr = allocate(adjusted_size);
            /////////////////////////////////////////////
            std::size_t offset = alignment - (ModuloUtilities::modulo_pow2((reinterpret_cast<std::uint64_t>(ptr)), alignment));
            void* ret = reinterpret_cast<void*>(reinterpret_cast<std::uint64_t>(ptr) + offset);
            return ret;
        }

        // Should be called only from bounded heaps as unbounded heaps may not have contigious memory
        // Only Central and SingleThread concurrency policies are unbounded
        // In other words owns_pointer is supposed to be called from only Thread Local or CPU Local heaps
        ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
        bool owns_pointer(void* ptr)
        {
            if constexpr (concurrency_policy != ConcurrencyPolicy::THREAD_LOCAL)
            {
                assert(0 == 1);
            }

            uint64_t address_in_question = reinterpret_cast<uint64_t>(ptr);

            if ( address_in_question >= m_buffer_address && address_in_question < m_buffer_address + m_buffer_length )
            {
                return true;
            }

            return false;
        }

    protected:
        uint64_t m_buffer_address = 0;
        std::size_t m_buffer_length = 0;
        static constexpr inline std::size_t MINIMUM_ALIGNMENT = 16;
};

#endif