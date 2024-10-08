/*
    THE MAIN FUNCTIONALITY HERE IS "allocate_aligned" IMPLEMENTATION : DURING DEALLOCATIONS, WE AIM FIND OUT LOGICAL PAGES OF THE ADDRESSES WHICH ARE BEING FREED BY APPLYING MODULO ON THE ADDRESS,
    SINCE HEADERS WILL IDEALLY BE PLACED ON THE VERY START OF LOGICAL PAGES. SO WE NEED LOGICAL PAGES TO BE ALIGNED TO CHOSEN LOGICAL_PAGE_SIZES.

    LINUX MMAP RETURNS ONLY 4KB-ALIGNED ADDRESSES AND WINDOWS VIRTUALALLOC ONLY 64KB-ALIGNED ADDRESSES. ( AS THAT IS PAGE ALLOC GRANULARITY ON WINDOWS )
    THEY DON'T GUARANTEE ALIGNMENTS APART FROM 4KB & 64KB. THEREFORE ARENA HANDLES THE ALIGNMENT FOR ARBITRARY ALIGNMENTS BEYOND 4KB & 64 KB WITH OVER-SIZED ALLOCATIONS
*/

#ifndef __ARENA_BASE_H__
#define __ARENA_BASE_H__

#include <cstddef>
#include "utilities/modulo_utilities.h"

template <typename ArenaImplementation>
class ArenaBase
{
    public:
        ArenaBase() = default;
        ~ArenaBase() = default;
        ArenaBase(const ArenaBase& other) = delete;
        ArenaBase& operator= (const ArenaBase& other) = delete;
        ArenaBase(ArenaBase&& other) = delete;
        ArenaBase& operator=(ArenaBase&& other) = delete;

        [[nodiscard]] void* allocate_from_system(std::size_t size) { return static_cast<ArenaImplementation*>(this)->allocate_from_system(size); }
        void release_to_system(void* address, std::size_t size) { static_cast<ArenaImplementation*>(this)->release_to_system(address, size); }

    protected:

        char* allocate_aligned(std::size_t size, std::size_t alignment)
        {
            std::size_t actual_size = size + alignment;
            char* buffer{ nullptr };

            buffer = static_cast<char*>(allocate_from_system(actual_size));

            if (buffer == nullptr)
            {
                return nullptr;
            }

            std::size_t remainder = ModuloUtilities::modulo( reinterpret_cast<std::size_t>(buffer), alignment);
            std::size_t delta = 0;

            if (remainder > 0)
            {
                // WE NEED PADDING FOR SPECIFIED PAGE ALIGNMENT
                delta = alignment - remainder;
                // RELEASING PADDING PAGE
                release_to_system(buffer, delta);
            }
            else
            {
                // PADDING IS NOT NEEDED, HENCE THE EXTRA ALLOCATED PAGE IS EXCESS
                release_to_system(buffer + actual_size - alignment, alignment);
            }
            return buffer + delta;
        }
};

#endif