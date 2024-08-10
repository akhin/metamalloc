/*
- IT IS A FREELIST IMPLEMENTATION THAT CAN HOLD MULTIPLE SIZES.
  COMPARED TO LOGICAL_PAGE , IT NEEDS MORE MEMORY AND ALSO IT IS SLOWER. HOWEVER IT IS USEFUL IN 2 CASES :

                        1. AS A CENTRAL SINGLE FREELIST FOR BIG-OBJECTS THAT DON'T FIT A VM PAGE AS THEIR SIZES WILL VARY

                        2. WHENEVER HIGH-LOCALITY NEEDED BY AVOIDING SIZE-BASED SEGREGATION,  EX: LOCAL ALLOCATIONS FOR SINGLE THREADED LOW LATENCY APPS


- IF THE PASSED BUFFER IS START OF A VIRTUAL PAGE AND THE PASSED SIZE IS A VM PAGE SIZE , THEN IT WILL BE CORRESPONDING TO AN ACTUAL VM PAGE
  IDEAL USE CASE IS ITS CORRESPONDING TO A VM PAGE / BEING VM PAGE ALIGNED . SO THAT A SINGLE PAYLOAD WILL NOT SPREAD TO DIFFERENT VM PAGES

- IF THE PASSED BUFFER SIZE IS A MULTIPLE OF VM PAGE SIZE, THEN IT CAN HOLD MULTIPLE CONTIGUOUS VM PAGES, SIMILARLY TO SPANS/PAGE RUNS

- USES 16 BYTE ALLOCATION HEADER. HEADERS ARE PLACED JUST BEFORE THE PAYLOADS. DEALLOCATIONS USE THE INFO IN THOSE ALLOCATION HEADERS

- ALWAYS RETURNS 16 BYTE ALIGNED POINTERS

- CURRENTLY SUPPORTS ONLY FIRST-FIT FOR SEARCHES

- IF THE PASSED BUFFER SIZE IS SAME AS TYPICAL VM PAGE SIZE -> 4KB = 4096 bytes , THEN IT CAN HOLD :

                                                                        metadata_percentage_for_node16
                                        128 * 32-BYTE   BLOCKS                50%                                (WORST CASE)
                                        64  * 64-BYTE   BLOCKS                25%
                                        32  * 128-BYTE  BLOCKS                12.5%
                                        16  * 256-BYTE  BLOCKS                6.25%
                                        8   * 512-BYTE  BLOCKS                3.12%
                                        4   * 1024-BYTE BLOCKS                1.6 %
                                        2   * 2048-BYTE BLOCKS                0.8%
                                        1   * 4096-BYTE BLOCK                 0.4%
*/
#ifndef __LOGICAL_PAGE_ANYSIZE_H__
#define __LOGICAL_PAGE_ANYSIZE_H__

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "compiler/hints_hot_code.h"
#include "compiler/hints_branch_predictor.h"
#include "compiler/packed.h"
#include "cpu/alignment_constants.h"
#include "utilities/alignment_checks.h"
#include "utilities/multiple_utilities.h"
#include "logical_page_base.h"

#ifdef UNIT_TEST // VOLTRON_EXCLUDE
#include <string>
#include <cstdio>
#endif // VOLTRON_EXCLUDE

// When memory is not allocated a Node will act as a freelist node
// And the same area will represent an allocation header after an allocation.
// Then if it is freed ,it will represent a freelist node again.

// No privates , member initialisers, ctors or dtors to stay as PACKED+POD
PACKED(
    struct LogicalPageAnySizeNode
    {                             // FREELIST_NODE_USAGE      ALLOCATION_HEADER_USAGE
        uint64_t m_first_part;    // next ptr/address         padding
        uint64_t m_second_part;   // block size               block size

        uint64_t get_next_pointer()
        {
            return m_first_part;
        }

        void set_next_pointer(uint64_t ptr)
        {
            m_first_part = ptr;
        }

        void clear_next_pointer()
        {
            m_first_part = 0;
        }

        std::size_t get_block_size() const
        {
            return m_second_part;
        }

        void set_block_size(std::size_t val)
        {
            m_second_part = val;
        }

        std::size_t get_padding_size() const
        {
            return m_first_part;
        }

        void set_padding_size(std::size_t val)
        {
            m_first_part = val;
        }

        // Not using std::numeric_limits as it needs extra #define or #undef on Windows
        static constexpr uint64_t MAX_BLOCK_SIZE = 0xFFFFFFFFFFFFFFFF;
        static constexpr std::size_t MIN_ALIGNMENT_SIZE = 16;
    }
);

enum class CoalescePolicy
{
    COALESCE,
    NO_COALESCING
};

template <CoalescePolicy coalesce_policy= CoalescePolicy::COALESCE>
class LogicalPageAnySize : public LogicalPageBase<LogicalPageAnySize<coalesce_policy>, LogicalPageAnySizeNode>
{
public:

    LogicalPageAnySize() {}
    ~LogicalPageAnySize() {}

    LogicalPageAnySize(const LogicalPageAnySize& other) = delete;
    LogicalPageAnySize& operator= (const LogicalPageAnySize& other) = delete;
    LogicalPageAnySize(LogicalPageAnySize&& other) = delete;
    LogicalPageAnySize& operator=(LogicalPageAnySize&& other) = delete;

    using NodeType = LogicalPageAnySizeNode;

    // Gets its memory from an external source such as a heap's arena
    [[nodiscard]] bool create(void* buffer, const std::size_t buffer_size, uint32_t size_class = 0)
    {
        if ( static_cast<uint64_t>(buffer_size) > NodeType::MAX_BLOCK_SIZE)
        {
            return false;
        }

        if (buffer_size <= NODE_SIZE || buffer == nullptr)
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

        // Creating very first freelist node
        NodeType* head_node = static_cast<NodeType*>(buffer);
        head_node->set_block_size(buffer_size); // Including the memory used for NodeType itself,
                                                // so the max data that can be stored is is block_size - NODE_SIZE

        head_node->clear_next_pointer();

        insert(nullptr, head_node);

        #ifdef UNIT_TEST
        this->m_capacity = buffer_size;
        #endif

        return true;
    }

    ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE) [[nodiscard]]
    void* allocate(const std::size_t size)
    {
        std::size_t allocation_size = size < NODE_SIZE ? NODE_SIZE : size;

        NodeType* affected_node = nullptr;
        NodeType* previous_node = nullptr;

        std::size_t padding{ 0 };

        find(allocation_size, previous_node, affected_node, LogicalPageAnySizeNode::MIN_ALIGNMENT_SIZE, padding);

        if (unlikely(affected_node == nullptr))
        {
            // Not enough memory
            return nullptr;
        }

        std::size_t total_size = allocation_size + padding + NODE_SIZE;
        const std::size_t excess_bytes = affected_node->get_block_size() - total_size;

        if (excess_bytes >= NODE_SIZE)
        {
            // We are returning extra bytes to the freelist as a new node
            NodeType* new_free_node = reinterpret_cast<NodeType*>(reinterpret_cast<std::size_t>(affected_node) + total_size /*padding + NODE_SIZE + allocation_size*/);
            new_free_node->set_block_size(excess_bytes);
            insert(affected_node, new_free_node);
        }
        else if (excess_bytes > 0 && excess_bytes < NODE_SIZE)
        {
            // Add extra bytes to the node we are about to release from the freelist
            // as it doesn't have enough space to hold a NodeType
            total_size += excess_bytes;
        }

        remove(previous_node, affected_node);

        // Calculate addresses based on the layout which is : <PADDING_BYTES><HEADER><PAYLOAD>
        const std::size_t header_address = reinterpret_cast<std::size_t>(affected_node) + padding;
        const std::size_t payload_address = header_address + NODE_SIZE;

        // Write the allocation header to the memory
        (reinterpret_cast<NodeType*>(header_address))->set_block_size(total_size);
        (reinterpret_cast<NodeType*>(header_address))->set_padding_size(padding);

        this->m_page_header.m_used_size += total_size;

        auto ret = reinterpret_cast<void*>(payload_address);

        #ifdef UNIT_TEST
        is_sane();
        #endif

        return ret;
    }

    ALIGN_CODE(AlignmentConstants::CACHE_LINE_SIZE)
    void deallocate(void* ptr)
    {
        if( unlikely(this->owns_pointer(ptr) == false) )
        {
            return;
        }

        const NodeType* allocation_header{ reinterpret_cast<NodeType*>(reinterpret_cast<std::size_t>(ptr) - NODE_SIZE) };
        const std::size_t current_address = reinterpret_cast<std::size_t>(ptr);
        std::size_t header_address = current_address - NODE_SIZE;
        std::size_t padding_size = allocation_header->get_padding_size();

        NodeType* free_node = reinterpret_cast<NodeType*>(header_address-padding_size);
        free_node->set_block_size(allocation_header->get_block_size());
        free_node->clear_next_pointer();

        NodeType* head = reinterpret_cast<NodeType*>(this->m_page_header.m_head);
        NodeType* iterator = head;
        NodeType* iterator_previous = nullptr;

        if (likely(head))
        {
            if (head->get_next_pointer() == 0)
            {
                insert(head, free_node);
            }
            else
            {
                bool inserted = false;
                while (iterator != nullptr)
                {
                    if (ptr < iterator)
                    {
                        insert(iterator_previous, free_node);
                        inserted = true;
                        break;
                    }
                    iterator_previous = iterator;
                    iterator = reinterpret_cast<NodeType*>(iterator->get_next_pointer());
                }

                // Inserting as the last element
                if (inserted == false)
                {
                    insert(iterator_previous, free_node);
                }
            }
        }
        else
        {
            insert(iterator_previous, free_node);
        }

        this->m_page_header.m_used_size -= free_node->get_block_size();

        if constexpr (coalesce_policy == CoalescePolicy::COALESCE)
        {
            // Merge them
            coalesce(iterator_previous, free_node);
        }

        #ifdef UNIT_TEST
        is_sane();
        #endif
    }

    static constexpr bool supports_any_size() { return true; }

    std::size_t get_usable_size(void* ptr)
    {
        const NodeType* allocation_header{ reinterpret_cast<NodeType*>(reinterpret_cast<std::size_t>(ptr) - NODE_SIZE) };
        return allocation_header->get_block_size() - NODE_SIZE;
    }

    #ifdef UNIT_TEST
    const std::string get_type_name() const { return "LogicalPageAnySize"; }

    double get_metadata_percentage_for_size_class(std::size_t size_class)
    {
        std::size_t single_data_metadata_pair_size = this->m_page_header.m_size_class + NODE_SIZE;
        std::size_t num_payloads = static_cast<std::size_t>(this->m_capacity / single_data_metadata_pair_size);
        std::size_t total_metadata_bytes = num_payloads * NODE_SIZE;
        return static_cast<double>(total_metadata_bytes) * 100 / this->m_capacity;
    }

    std::size_t get_node_count()
    {
        std::size_t count{ 0 };
        NodeType* iterator = reinterpret_cast<NodeType*>(this->m_page_header.m_head);
        while (iterator != nullptr)
        {
            count++;
            iterator = reinterpret_cast<NodeType*>(iterator->get_next_pointer());
        }
        return count;
    }

    bool is_sane()
    {
        NodeType* iterator = reinterpret_cast<NodeType*>(this->m_page_header.m_head);
        std::size_t total_available_memory{ 0 };

        while (iterator != nullptr)
        {
            total_available_memory += iterator->get_block_size();
            iterator = reinterpret_cast<NodeType*>(iterator->get_next_pointer());
        }

        auto computed_total_size = total_available_memory + this->m_page_header.m_used_size;

        bool is_sane = this->m_page_header.m_logical_page_size == computed_total_size;

        if (!is_sane)
        {
            fprintf(stderr, "CORRUPT FREELIST !!!");
        }

        return is_sane;
    }

    const std::string get_debug_info()
    {
        std::string output;
        NodeType* iterator = reinterpret_cast<NodeType*>(this->m_page_header.m_head);

        std::size_t counter{ 0 };
        std::size_t total_available_memory{ 0 };

        while (iterator != nullptr)
        {
            counter++;
            total_available_memory += iterator->get_block_size();
            output += "Node " + std::to_string(counter) + " : block size = " + std::to_string(iterator->get_block_size()) + "\n";
            iterator = reinterpret_cast<NodeType*>(iterator->get_next_pointer());
        }

        output += "USED SIZE = " + std::to_string(this->m_page_header.m_used_size) + "\n";
        output += "REMAINING SIZE = " + std::to_string(total_available_memory) + "\n";
        output += "USED + REMAINING SIZE = " + std::to_string(total_available_memory  + this->m_page_header.m_used_size) + "\n";
        output += "TOTAL = " + std::to_string(total_available_memory + this->m_page_header.m_used_size) + "\n";

        return output;
    }

    #endif

private:

    static inline constexpr std::size_t NODE_SIZE = sizeof(NodeType);

    void coalesce(NodeType* __restrict previous_node, NodeType* __restrict free_node)
    {
        if (reinterpret_cast<void*>(free_node->get_next_pointer()) != nullptr &&
            reinterpret_cast<std::size_t>(free_node) + free_node->get_block_size() == free_node->get_next_pointer())
        {
            // Free node and its next node are adjacent , adding the next node to free node and deleting the next node
            free_node->set_block_size(free_node->get_block_size() + reinterpret_cast<NodeType*>(free_node->get_next_pointer())->get_block_size());
            remove(free_node, reinterpret_cast<NodeType*>(free_node->get_next_pointer()));

            this->m_page_header.m_last_used_node = 0;
        }

        if (previous_node != nullptr &&
            reinterpret_cast<std::size_t>(previous_node) + previous_node->get_block_size() == reinterpret_cast<std::size_t>(free_node))
        {
            // Free node and its previous node are also adjacent , adding free node to the previous one and removing free node
            previous_node->set_block_size(previous_node->get_block_size() + free_node->get_block_size());
            remove(previous_node, free_node);

            this->m_page_header.m_last_used_node = 0;
        }
    }

    FORCE_INLINE void find(const std::size_t size, NodeType*& __restrict previous_node, NodeType*& __restrict found_node, const std::size_t alignment, std::size_t& padding_bytes)
    {
        NodeType* iterator = reinterpret_cast<NodeType*>(this->m_page_header.m_head);
        find_internal(iterator, nullptr, size, previous_node, found_node, alignment, padding_bytes);
    }

	// First-fit
    FORCE_INLINE void find_internal(NodeType*& __restrict search_start_node, NodeType* __restrict search_end_node, const std::size_t size, NodeType*& __restrict previous_node, NodeType*& __restrict found_node, const std::size_t alignment, std::size_t& padding_bytes)
    {
        NodeType* iterator = search_start_node;
        NodeType* iterator_previous = nullptr;

        while (iterator && iterator != search_end_node)
        {
            padding_bytes = calculate_padding_needed<NODE_SIZE>(reinterpret_cast<std::size_t>(iterator), alignment);
            const std::size_t required_space = size + NODE_SIZE + padding_bytes;

            if (iterator->get_block_size() >= required_space)
            {
                previous_node = iterator_previous;
                found_node = iterator;
                break;
            }

            NodeType* next = reinterpret_cast<NodeType*>(iterator->get_next_pointer());

            if (next)
            {
                iterator_previous = iterator;
            }

            iterator = next;
        }
    }

    void insert(NodeType* __restrict previous_node, NodeType* __restrict new_node)
    {
        NodeType* head = reinterpret_cast<NodeType*>(this->m_page_header.m_head);

        if (previous_node == nullptr)
        {
            // Will be inserted as head

            if (head != nullptr)
            {
                new_node->set_next_pointer(this->m_page_header.m_head);
            }
            else
            {
                new_node->clear_next_pointer();
            }

            this->m_page_header.m_head = reinterpret_cast<uint64_t>(new_node);
        }
        else
        {
            if ( reinterpret_cast<void *>(previous_node->get_next_pointer()) == nullptr)
            {
                // Becomes last element
                previous_node->set_next_pointer(reinterpret_cast<uint64_t>(new_node));
                new_node->clear_next_pointer();
            }
            else
            {
                // Gets into the middle
                new_node->set_next_pointer(previous_node->get_next_pointer());
                previous_node->set_next_pointer(reinterpret_cast<uint64_t>(new_node));
            }
        }
    }

    void remove(NodeType* __restrict previous_node, NodeType* __restrict delete_node)
    {
        if (previous_node == nullptr)
        {
            if ( reinterpret_cast<NodeType*>(delete_node->get_next_pointer()) == nullptr)
            {
                // Deleting head
                this->m_page_header.m_head = 0;
            }
            else
            {
                // Second node becomes head
                this->m_page_header.m_head = delete_node->get_next_pointer();
            }
        }
        else
        {
            // Deleting from middle
            previous_node->set_next_pointer(delete_node->get_next_pointer());
        }
    }

#ifdef UNIT_TEST
public:
#else
private:
#endif

    template<std::size_t node_size>
    static const std::size_t calculate_padding_needed(const uint64_t base_address, const std::size_t alignment)
    {
        std::size_t header_end_address = base_address + node_size;
        std::size_t remainder = header_end_address % alignment;

        std::size_t extra_padding = 0;

        if (remainder != 0)
        {
            extra_padding = (alignment - remainder);
        }

        return extra_padding;
    }
};

#endif