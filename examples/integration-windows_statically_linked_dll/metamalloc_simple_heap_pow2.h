#ifndef __METAMALLOC_SIMPLE_HEAP_POW2__
#define __METAMALLOC_SIMPLE_HEAP_POW2__

#ifndef DLL_EXPORTS
#pragma comment(lib, "metamalloc_simple_heap_pow2.lib")
#endif

#include <cstddef>
#include <new>
#include <windows.h>

#ifdef DLL_EXPORTS
#define DLL_FUNCTION __declspec(dllexport)
#else
#define DLL_FUNCTION __declspec(dllimport)
#endif

extern "C"
{
    DLL_FUNCTION void *metamalloc_malloc(std::size_t size);
    DLL_FUNCTION void* metamalloc_aligned_malloc(std::size_t size, std::size_t alignment);
    DLL_FUNCTION void metamalloc_free(void* ptr);
    DLL_FUNCTION void metamalloc_aligned_free(void* ptr);
    DLL_FUNCTION void *metamalloc_calloc(std::size_t num, std::size_t size);
    DLL_FUNCTION void *metamalloc_realloc(void *ptr, std::size_t size);
    DLL_FUNCTION void *metamalloc_operator_new(std::size_t size);
    DLL_FUNCTION void *metamalloc_operator_new_aligned(std::size_t size, std::size_t alignment);
    DLL_FUNCTION std::size_t metamalloc_usable_size(void* ptr);
} // extern "C"

#ifndef DLL_EXPORTS
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
    metamalloc_free(ptr);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return metamalloc_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
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
    metamalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    metamalloc_free(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    metamalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    metamalloc_free(ptr);
}

#endif

#endif