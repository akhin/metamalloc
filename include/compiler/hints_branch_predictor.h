#ifndef _HINTS_BRANCH_PREDICTOR_
#define _HINTS_BRANCH_PREDICTOR_

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LIKELY
#if defined(_MSC_VER)
//No implementation provided for MSVC for pre C++20 :
//https://social.msdn.microsoft.com/Forums/vstudio/en-US/2dbdca4d-c0c0-40a3-993b-dc78817be26e/branch-hints?forum=vclanguage
#define likely(x) x
#elif defined(__GNUC__)
#define likely(x)      __builtin_expect(!!(x), 1)
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UNLIKELY
#if defined(_MSC_VER)
//No implementation provided for MSVC for pre C++20 :
//https://social.msdn.microsoft.com/Forums/vstudio/en-US/2dbdca4d-c0c0-40a3-993b-dc78817be26e/branch-hints?forum=vclanguage
#define unlikely(x) x
#elif defined(__GNUC__)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VERY LIKELY
#if defined(_MSC_VER)
//No implementation provided for MSVC in any version :
//https://social.msdn.microsoft.com/Forums/vstudio/en-US/2dbdca4d-c0c0-40a3-993b-dc78817be26e/branch-hints?forum=vclanguage
#define very_likely(x) x
#elif defined(__GNUC__)
#define very_likely(x) __builtin_expect_with_probability(!!(x),1,0.99)
#endif

#endif