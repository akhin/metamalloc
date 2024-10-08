#ifndef _BUILTIN_FUNCTIONS_H_
#define _BUILTIN_FUNCTIONS_H_

//////////////////////////////////////////////////////////////////////
// Count trailing zeroes
#if defined(__GNUC__)
#define builtin_ctzl(n)     __builtin_ctzl(n)
#elif defined(_MSC_VER)
#include <intrin.h>
#if defined(_WIN64)    // Implementation is for 64-bit only.
inline unsigned int builtin_ctzl(unsigned long long value)
{
    unsigned long trailing_zero = 0;

    if (_BitScanForward64(&trailing_zero, static_cast<unsigned __int64>(value)))
    {
        return static_cast<unsigned int>(trailing_zero);
    }
    else
    {
        return 64;    // Sizeof unsigned long long.
    }
}
#else
#error "This code is intended for 64-bit Windows platforms only."
#endif
#endif

//////////////////////////////////////////////////////////////////////
// Count leading zeroes
#if defined(__GNUC__)
#define builtin_clzl(n)     __builtin_clzl(n)
#elif defined(_MSC_VER)
#include <intrin.h>
#if defined(_WIN64)    // Implementation is for 64-bit only.
inline int builtin_clzl(unsigned long value)
{
    unsigned long index = 0;
    return _BitScanReverse64(&index, static_cast<unsigned __int64>(value)) ? static_cast<int>(63 - index) : 64;
}
#else
#error "This code is intended for 64-bit Windows platforms only."
#endif
#endif

#if defined(__GNUC__)
#define builtin_clz(n)     __builtin_clz(n)
#elif defined(_MSC_VER)
#include <intrin.h>
inline int builtin_clz(unsigned long value) // Implementation is for 32-bit only.
{
    unsigned long index = 0;
    return _BitScanReverse(&index, value) ? static_cast<int>(31 - index) : 32;
}
#endif

//////////////////////////////////////////////////////////////////////
// Compare and swap, standard C++ provides them however it requires non-POD std::atomic usage
// They are needed when we want to embed spinlocks in "packed" data structures which need all members to be POD such as headers
#if defined(__GNUC__)
#define builtin_cas(pointer, old_value, new_value) __sync_val_compare_and_swap(pointer, old_value, new_value)
#elif defined(_MSC_VER)
#include <intrin.h>
#define builtin_cas(pointer, old_value, new_value) _InterlockedCompareExchange(reinterpret_cast<long*>(pointer), new_value, old_value)
#endif

//////////////////////////////////////////////////////////////////////
// memcpy
#if defined(__GNUC__)
#define builtin_memcpy(destination, source, size)     __builtin_memcpy(destination, source, size)
#elif defined(_MSC_VER)
#include <cstring>
#define builtin_memcpy(destination, source, size)     std::memcpy(destination, source, size)
#endif

//////////////////////////////////////////////////////////////////////
// memset
#if defined(__GNUC__)
#define builtin_memset(destination, character, count)  __builtin_memset(destination, character, count)
#elif defined(_MSC_VER)
#include <cstring>
#define builtin_memset(destination, character, count)  std::memset(destination, character, count)
#endif

//////////////////////////////////////////////////////////////////////
// aligned_alloc , It exists because MSVC does not provide std::aligned_alloc
#include <cstddef>
#include <cstdlib>
#if defined(__GNUC__)
#define builtin_aligned_alloc(size, alignment)  std::aligned_alloc(alignment, size)
#define builtin_aligned_free(ptr)               std::free(ptr)
#elif defined(_MSC_VER)
#define builtin_aligned_alloc(size, alignment)  _aligned_malloc(size, alignment)
#define builtin_aligned_free(ptr)               _aligned_free(ptr)
#endif

//////////////////////////////////////////////////////////////////////
// builtin_byte_swap16
#if defined(__GNUC__)
#define builtin_byte_swap16(val)     __builtin_bswap16(val)
#elif defined(_MSC_VER)
#include <cstdlib>
#define builtin_byte_swap16(val)     _byteswap_ushort (val)
#endif

//////////////////////////////////////////////////////////////////////
// builtin_byte_swap32
#if defined(__GNUC__)
#define builtin_byte_swap32(val)     __builtin_bswap32(val)
#elif defined(_MSC_VER)
#include <cstdlib>
#define builtin_byte_swap32(val)     _byteswap_ulong  (val)
#endif

//////////////////////////////////////////////////////////////////////
// builtin_byte_swap64
#if defined(__GNUC__)
#define builtin_byte_swap64(val)     __builtin_bswap64(val)
#elif defined(_MSC_VER)
#include <cstdlib>
#define builtin_byte_swap64(val)     _byteswap_uint64  (val)
#endif

#endif