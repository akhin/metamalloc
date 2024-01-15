#ifndef _HINTS_HOT_CODE_
#define _HINTS_HOT_CODE_

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FORCE_INLINE
#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline))
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HOT
#if defined(_MSC_VER)
//No implementation provided for MSVC :
#define HOT
#elif defined(__GNUC__)
#define HOT __attribute__((hot))
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ALIGN_DATA , some GCC versions gives warnings about standard C++ 'alignas' when applied to data
#ifdef __GNUC__
#define ALIGN_DATA( _alignment_ ) __attribute__((aligned( (_alignment_) )))
#elif _MSC_VER
#define ALIGN_DATA( _alignment_ ) alignas( _alignment_ )
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ALIGN_CODE, using alignas(64) or __attribute__(aligned(alignment)) for a function will work in GCC but MSVC won't compile
#ifdef __GNUC__
#define ALIGN_CODE( _alignment_ ) __attribute__((aligned( (_alignment_) )))
#elif _MSC_VER
//No implementation provided for MSVC :
#define ALIGN_CODE( _alignment_ )
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FORCE_LOOP_VECTORISATION
/*
    Usage :

        FORCE_LOOP_VECTORISATION
        for (int j = 0; j < M; j++)
        {
            // loop body
        }

    _Pragma ( since C++11 ) allows to use pragma directives in macros.
    In MSVC, it is not supported but instead MSVC provides __pragma : https://learn.microsoft.com/en-us/cpp/preprocessor/pragma-directives-and-the-pragma-keyword?view=msvc-170

    In MSVC, if you pass "-openmp:experimental" option to the compiler , it will inform you if vectorization failed :

                 info C5002: Omp simd loop not vectorized due to reason '1303' -> means that there was too few iterations
*/
#ifdef _MSC_VER
#define FORCE_LOOP_VECTORISATION __pragma(omp simd)
/*
    MSVC also supports #pragma ivdep
    But they recommend pragma omp simd :
    https://learn.microsoft.com/en-us/cpp/parallel/openmp/openmp-simd?view=msvc-170
*/
#elif defined(__GNUC__)
#define FORCE_LOOP_VECTORISATION _Pragma("GCC ivdep")
#endif

#endif