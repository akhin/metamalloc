/*
    LIST OF FUNCTIONS & METHODS THAT ARE HANDLED/OVERLOADED :

                                                    C
                                        malloc
                                        free
                                        calloc
                                        realloc

                                                    OPERATOR NEW AND DELETE ( THE LIST IS NOT COMPLETE BY C++ STANDARDS, BUT THOSE ARE THE ONLY ONES IMPLEMENTED BY GNU LIBC & MS CRT / UCRT )

                                        void* operator new(std::size_t size)
                                        void operator delete(void* ptr) noexcept
                                        void* operator new[](std::size_t size)
                                        void operator delete[](void* ptr) noexcept

                                        void* operator new(std::size_t size, std::align_val_t alignment)
                                        void operator delete(void* ptr, std::align_val_t alignment) noexcept
                                        void* operator new[](std::size_t size, std::align_val_t alignment);
                                        void operator delete[](void* ptr, std::align_val_t alignment) noexcept;

                                        void* operator new(std::size_t size, const std::nothrow_t&) noexcept;
                                        void operator delete(void* ptr, const std::nothrow_t&) noexcept;
                                        void* operator new[](std::size_t size, const std::nothrow_t&) noexcept;
                                        void operator delete[](void* ptr, const std::nothrow_t&) noexcept;

                                        void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
                                        void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
                                        void operator delete(void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
                                        void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t &) noexcept

                                        void operator delete(void* p, std::size_t size) noexcept
                                        void operator delete[](void* p, std::size_t size) noexcept
                                        void operator delete(void* p, std::size_t size, std::align_val_t align) noexcept
                                        void operator delete[](void* p, std::size_t size, std::align_val_t align) noexcept

                                                        LINUX-ONLY ONES

                                        aligned_alloc        https://linux.die.net/man/3/aligned_alloc    ( MSVC does not provide std::aligned_alloc , therefore only Linux )
                                        malloc_usable_size    https://linux.die.net/man/3/malloc_usable_size

                                                        WINDOWS-ONLY ONES

                                        _aligned_malloc        https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc?view=msvc-170
                                        _aligned_free        https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-free?view=msvc-170
                                        _msize                https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/msize?view=msvc-170
*/
#include <cstddef>
#include <new>
#include <cstdlib>
#include <stdexcept>

#include "../../metamalloc.h"
#include "../simple_heap_pow2.h"
using namespace metamalloc;

using CentralHeapType = SimpleHeapPow2<ConcurrencyPolicy::CENTRAL>;
using LocalHeapType = SimpleHeapPow2<ConcurrencyPolicy::THREAD_LOCAL>;

using ScalableAllocatorType = ScalableAllocator<
    CentralHeapType,
    LocalHeapType
>;

static bool g_metamalloc_initialise_called = false;

void metamalloc_initialise()
{
    if(g_metamalloc_initialise_called) return;
    /////////////////////////////////////////////////////////////////////////////////////////////
    std::size_t default_arena_capacity = 149946368;

    std::size_t ARENA_CAPACITY = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_arena_capacity", default_arena_capacity);
    std::size_t thread_local_heap_cache_count = EnvironmentVariable::get_variable("metamalloc_thread_local_heap_cache_count", 4);

    CentralHeapType::HeapCreationParams params_central;
    params_central.m_small_object_logical_page_size         = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_small_object_logical_page_size", 65536);
    params_central.m_big_object_logical_page_size           = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_big_object_logical_page_size", 655360);    // THIS SHOULD BE 16 BYTES MORE THAN THE LARGEST EXPECTED ALLOCATION SIZE
    params_central.m_segment_grow_coefficient              = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_grow_coefficient", 0.0);
    params_central.m_small_object_page_recycling_threshold= EnvironmentVariable::get_variable("metamalloc_simple_heappow2_small_object_page_recycling_threshold", 1000);
    params_central.m_big_object_page_recycling_threshold = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_big_object_page_recycling_threshold", 1000);

    LocalHeapType::HeapCreationParams params_local;
    params_local.m_small_object_logical_page_size            = params_central.m_small_object_logical_page_size;
    params_local.m_big_object_logical_page_size           = params_central.m_big_object_logical_page_size;
    params_local.m_small_object_page_recycling_threshold  = params_central.m_small_object_page_recycling_threshold;
    params_local.m_segment_deallocation_queue_initial_capacity      = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_deallocation_queue_initial_capacity", 3276800);
    params_local.m_big_object_page_recycling_threshold = params_central.m_big_object_page_recycling_threshold;

    EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(params_central.m_small_object_bin_page_counts, EnvironmentVariable::get_variable("metamalloc_simple_heappow2_central_page_counts", "1,1,1,1,1,1,1,1"));
    EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(params_local.m_small_object_bin_page_counts, EnvironmentVariable::get_variable("metamalloc_simple_heappow2_local_page_counts", "100,100,100,100,100,100,100,100"));
    /////////////////////////////////////////////////////////////////////////////////////////////
    ScalableAllocatorType::get_instance().set_thread_local_heap_cache_count(thread_local_heap_cache_count);
    bool success = ScalableAllocatorType::get_instance().create(params_central, params_local, ARENA_CAPACITY);
    if(!success) { throw std::runtime_error("Metamalloc initialisation failed");}

    g_metamalloc_initialise_called = true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// ALL REPLACEMENTS WILL BE DIRECTED TO FUNCTIONS BELOW
void* metamalloc_malloc(std::size_t size)
{
    // It may be called by static objects that allocate memory in their constructors
    // In that case that call will be before initialisation
    if(g_metamalloc_initialise_called==false)
    {
        metamalloc_initialise();
    }
    return ScalableAllocatorType::get_instance().allocate(size);
}

void* metamalloc_aligned_malloc(std::size_t size, std::size_t alignment)
{
    return ScalableAllocatorType::get_instance().allocate_aligned(size, alignment);
}

void* metamalloc_operator_new(std::size_t size)
{
    // It may be called by static objects that allocate memory in their constructors
    // In that case that call will be before initialisation
    if(g_metamalloc_initialise_called==false)
    {
        metamalloc_initialise();
    }
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void* metamalloc_operator_new_aligned(std::size_t size, std::size_t alignment)
{
    // It may be called by static objects that allocate memory in their constructors
    // In that case that call will be before initialisation
    if(g_metamalloc_initialise_called==false)
    {
        metamalloc_initialise();
    }
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, alignment);
}

void metamalloc_free(void* ptr)
{
    // It may be called by static objects that allocate and free memory in their constructors
    // In that case that call will be before initialisation
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* metamalloc_calloc(std::size_t num, std::size_t size)
{
    return  ScalableAllocatorType::get_instance().allocate_and_zero_memory(num, size);
}

void* metamalloc_realloc(void* ptr, std::size_t size)
{
    return  ScalableAllocatorType::get_instance().reallocate(ptr, size);
}

std::size_t metamalloc_usable_size(void* ptr)
{
    return ScalableAllocatorType::get_instance().get_usable_size(ptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
#define malloc(size) metamalloc_malloc(size)
#define free(ptr) metamalloc_free(ptr)
#define calloc(num, size) metamalloc_calloc(num, size)
#define realloc(ptr, size) metamalloc_realloc(ptr, size)
#ifdef _WIN32
#define _aligned_malloc(size, alignment) metamalloc_aligned_malloc(size, alignment)
#define _aligned_free(ptr) metamalloc_free(ptr)
#define _msize(ptr) metamalloc_usable_size(ptr)
#endif
#ifdef __linux__
#define aligned_alloc(alignment, size) metamalloc_aligned_malloc(size, alignment)
#endif
///////////////////////////////////////////////////////////////////////
// USUAL OVERLOADS
void* operator new(std::size_t size)
{
    return metamalloc_operator_new(size);
}

void operator delete(void* ptr) noexcept
{
    metamalloc_free(ptr);
}

void* operator new[](std::size_t size)
{
    return metamalloc_operator_new(size);
}

void operator delete[](void* ptr) noexcept
{
    metamalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return metamalloc_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    UNUSED(alignment);
    metamalloc_free(ptr);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return metamalloc_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    UNUSED(alignment);
    metamalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH std::nothrow_t
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return metamalloc_malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    metamalloc_free(ptr);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return metamalloc_malloc(size);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    metamalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT and std::nothrow_t

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    return metamalloc_aligned_malloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    return metamalloc_aligned_malloc(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    metamalloc_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    metamalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// DELETES WITH SIZES
void operator delete(void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    metamalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    metamalloc_free(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    metamalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    metamalloc_free(ptr);
}
