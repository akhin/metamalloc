#ifndef _PAUSE_H_
#define _PAUSE_H_

#include <cstdint>

#include <immintrin.h>

#if defined(_MSC_VER) // VOLTRON_EXCLUDE
#include <intrin.h>
#elif defined(__GNUC__) // VOLTRON_EXCLUDE
#include <emmintrin.h>
#endif // VOLTRON_EXCLUDE

/*
    Intel initially advised using _mm_pause in spin-wait loops in case of hyperthreading
    Before Skylake it was about 10 cycles, but with Skylake it becomes 140 cycles and that applies to successor architectures
    -> Intel opt manual 2.5.4 "Pause Latency in Skylake Client Microarchitecture"

    Later _tpause  / _umonitor / _umwait instructions were introduced however not using them for the time being as they are not widespread yet

    Pause implementation is instead using nop
*/

inline void pause(uint16_t repeat_count=100)
{
    #if defined(__GNUC__)
    // rep is for repeating by the no provided in 16 bit cx register
    __asm__ __volatile__("mov %0, %%cx\n\trep; nop" : : "r" (repeat_count) : "cx");
    #elif defined(_WIN32)
    for (uint16_t i = 0; i < repeat_count; ++i)
    {
        _mm_lfence();
        __nop();
        _mm_lfence();
    }
    #endif
}

#endif