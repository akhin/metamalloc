/*
    A CAS ( compare-and-swap ) based POD ( https://en.cppreference.com/w/cpp/language/classes#POD_class ) spinlock
    As it is POD , it can be used inside packed declarations.

    To keep it as POD :
                1. Not using standard C++ std::atomic
                2. Member variables should be public

    Otherwise GCC will generate : warning: ignoring packed attribute because of unpacked non-POD field

    PAHOLE OUTPUT :

                size: 4, cachelines: 1, members: 1
                last cacheline: 4 bytes

    Can be faster than os/lock or std::mutex
    However should be picked carefully as it will affect all processes on a CPU core
    even though they are not doing the same computation so misuse may lead to starvation for others

    Doesn`t check against uniprocessors. Prefer "Lock" in os/lock.h for old systems
*/
#ifndef _USERSPACE_SPINLOCK_
#define _USERSPACE_SPINLOCK_

#include <cstddef>
#include <cstdint>

#include "../compiler/hints_hot_code.h"
#include "../compiler/builtin_functions.h"
#include "../cpu/pause.h"
#include "../os/thread_utilities.h"

// Pass alignment = AlignmentConstants::CACHE_LINE_SIZE to make the lock cacheline aligned

template<std::size_t alignment=sizeof(uint32_t), std::size_t spin_count = 1024, std::size_t pause_count = 64, bool extra_system_friendly = false>
struct UserspaceSpinlock
{
    // No privates, ctors or dtors to stay as PACKED+POD
    ALIGN_DATA(alignment) uint32_t m_flag=0;

    void initialise()
    {
        m_flag = 0;
    }

    void lock()
    {
        while (true)
        {
            for (std::size_t i(0); i < spin_count; i++)
            {
                if (try_lock() == true)
                {
                    return;
                }

                pause(pause_count);
            }

            if constexpr (extra_system_friendly)
            {
                ThreadUtilities::yield();
            }
        }
    }

    FORCE_INLINE bool try_lock()
    {
        if (builtin_cas(&m_flag, 0, 1) == 1)
        {
            return false;
        }

        return true;
    }

    FORCE_INLINE void unlock()
    {
        m_flag = 0;
    }
};


#endif