/*
    ! operator[] is used for only retrieval but not for insertion

    ! No checks/prevention if the caller uses same key value more than once
    
    - Memory layout : key value key value key value ...
    
    - Uses separate chaining ( linear chaining ) for collisions
   
    - Reallocates memory when the load factor reaches 1
    
    - Not thread safe
    
    - Default hash function : MurmurHash3 https://en.wikipedia.org/wiki/MurmurHash
*/
#ifndef __DICTIONARY_H__
#define __DICTIONARY_H__

#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

#include "../compiler/unused.h"
#include "../compiler/hints_hot_code.h"
#include "../compiler/hints_branch_predictor.h"

///////////////////////////////////////////////////////////
// DEFAULT HASH FUNCTOR
template <typename T, typename Enable = void>
struct MurmurHash3 
{
    std::size_t operator()(T h) const noexcept 
    {
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return static_cast<std::size_t>(h);
    }
};

// 64 bit specialisation
template <typename T>
struct MurmurHash3<T, typename std::enable_if<std::is_same<T, uint64_t>::value>::type>
{
    std::size_t operator()(uint64_t h) const noexcept 
    {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccd;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53;
        h ^= h >> 33;
        return static_cast<std::size_t>(h);
    }
};
///////////////////////////////////////////////////////////

template <typename Key, typename Value, typename Allocator, typename HashFunction = MurmurHash3<Key>>
class Dictionary
{
    public:

        struct DictionaryNode
        {
            Key key;                    // We need to store key hash values of different keys can be same
            Value value;
            DictionaryNode* next = nullptr;

            DictionaryNode() = default;
            DictionaryNode(const Key& k, const Value& v) : key(k), value(v), next(nullptr) {}
        };

        Dictionary() = default;

        ~Dictionary()
        {
            destroy();
        }

        bool initialise(std::size_t size)
        {
            return grow(size);
        }

        bool insert(const Key& key, const Value& value)
        {
            assert(m_table_size > 0 && m_node_cache != nullptr);

            if (unlikely(m_item_count == m_table_size)) // Load factor 1 , we need to resize
            {
                if (grow(m_table_size * 2) == false)
                {
                    return false;
                }
            }

            std::size_t index = modulo_table_size(m_hash(key));

            DictionaryNode* new_node = m_node_cache + m_item_count;
            new_node->key = key;
            new_node->value = value;
            
            new_node->next = m_table[index];
            m_table[index] = new_node;

            ++m_item_count;
            return true;
        }

        void remove(const Key& key)
        {
            assert(m_table_size > 0 && m_node_cache != nullptr);
            
            std::size_t index = modulo_table_size(m_hash(key));
            
            DictionaryNode* current = m_table[index];
            DictionaryNode* prev = nullptr;
            
            while (current != nullptr)
            {
                if (current->key == key)
                {
                    if (prev == nullptr)
                    {
                        m_table[index] = current->next;
                    }
                    else
                    {
                        prev->next = current->next;
                    }
                    --m_item_count;
                    return;
                }

                prev = current;
                current = current->next;
            }
        }

        bool get(const Key& key, Value& value) const
        {
            assert(m_table_size > 0 && m_node_cache != nullptr);

            std::size_t index = modulo_table_size(m_hash(key));
            DictionaryNode* current = m_table[index];

            while (current != nullptr)
            {
                if (current->key == key)
                {
                    value = current->value;
                    return true;
                }
                current = current->next;
            }
            return false;
        }

        bool set_existing_item(const Key& key, Value value)
        {
            assert(m_table_size > 0 && m_node_cache != nullptr);

            std::size_t index = modulo_table_size(m_hash(key));
            DictionaryNode* current = m_table[index];

            while (current != nullptr)
            {
                if (current->key == key)
                {
                    current->value = value;
                    return true;
                }
                current = current->next;
            }
            return false;
        }

        bool has_key(const Key& key) const
        {
            assert(m_table_size > 0 && m_node_cache != nullptr);

            std::size_t index = modulo_table_size(m_hash(key));
            DictionaryNode* current = m_table[index];

            while (current != nullptr)
            {
                if (current->key == key)
                {
                    return true;
                }

                current = current->next;
            }
            return false;
        }

        std::size_t get_item_count() const { return m_item_count; }
        std::size_t table_size() const { return m_table_size; } // Same as bucket count in std::unordered_map

        class iterator
        {
            public:
                using iterator_category = std::forward_iterator_tag;
                using difference_type = std::ptrdiff_t;
                using value_type = DictionaryNode;
                using pointer = DictionaryNode*;
                using reference = DictionaryNode&;

                iterator(pointer ptr) : m_ptr(ptr){}

                reference operator*() const { return *m_ptr; }
                pointer operator->() { return m_ptr; }

                // Prefix increment
                iterator& operator++()
                {
                    m_ptr++;
                    return *this;
                }

                // Postfix increment
                iterator operator++(int)
                {
                    iterator tmp = *this;
                    ++(*this);
                    return tmp;
                }

                friend bool operator==(const iterator& a, const iterator& b) { return a.m_ptr == b.m_ptr; }
                friend bool operator!=(const iterator& a, const iterator& b) { return a.m_ptr != b.m_ptr; }

            private:
                pointer m_ptr = nullptr;
        };

        iterator begin()
        {
            return iterator(m_node_cache);
        }

        iterator end()
        {
            return iterator(m_node_cache + m_item_count);
        }

    private:

        DictionaryNode** m_table = nullptr;
        DictionaryNode* m_node_cache = nullptr;

        std::size_t m_table_size = 0;
        std::size_t m_item_count = 0;

        HashFunction m_hash;

        bool grow(std::size_t size)
        {
            assert(size > 0);

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // CREATE

            auto new_node_cache = reinterpret_cast<DictionaryNode*>(Allocator::allocate(size * sizeof(DictionaryNode)));

            if (new_node_cache == nullptr)
            {
                return false;
            }

            auto new_table = reinterpret_cast<DictionaryNode**>(Allocator::allocate(size * sizeof(DictionaryNode*)));

            if (new_table == nullptr)
            {
                Allocator::deallocate(new_node_cache, size * sizeof(DictionaryNode*));
                return false;
            }

            for (std::size_t i = 0; i < size; ++i)
            {
                new_table[i] = nullptr;
            }

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // COPY EXISTING
            std::size_t copy_count = 0;

            if (m_table != nullptr)
            {
                for (std::size_t i = 0; i < m_table_size; ++i)
                {
                    DictionaryNode* current = m_table[i];

                    while (current != nullptr)
                    {
                        auto current_key_hash = m_hash(current->key);
                        std::size_t new_index = current_key_hash - (current_key_hash / size) * size; // Equivalent of current_key_hash% size;
                       
                        DictionaryNode* new_node = new (new_node_cache + copy_count) DictionaryNode(current->key, current->value); // Placement new
                        copy_count++;

                        new_node->next = new_table[new_index];
                        new_table[new_index] = new_node;

                        current = current->next;
                    }
                }
            }

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // CONSTRUCT THE REST
            for (std::size_t i = copy_count; i < size; i++)
            {
                DictionaryNode* new_node = new (new_node_cache + i) DictionaryNode(); // Placement new
                UNUSED(new_node);
            }

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // DESTROY EXISTING
            destroy();
            
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // SWAP POINTERS

            m_table = new_table;
            m_node_cache = new_node_cache;
            m_table_size = size;

            return true;
        }

        FORCE_INLINE std::size_t modulo_table_size(std::size_t input) const
        {
            assert(m_table_size > 0);
            return input - (input / m_table_size) * m_table_size;
        }

        void destroy()
        {
            if (m_table)
            {
                Allocator::deallocate(m_table , m_table_size * sizeof(DictionaryNode*) );
            }

            if (m_node_cache)
            {
                if constexpr (std::is_destructible<DictionaryNode>::value)
                {
                    for (std::size_t i = 0; i < m_table_size; ++i)
                    {
                        m_node_cache[i].~DictionaryNode();
                    }
                }

                Allocator::deallocate(m_node_cache, m_table_size * sizeof(DictionaryNode));
            }
        }
};

#endif