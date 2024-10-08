/*
    The advantage of having this compared to just using std::mutex is that you can place breakpoints inside lock and unlock
    additionally you can further fine-tune Windows lock if necessary

    initialise & uninitialise provided to have the same interface with POD locks which can't have ctors & dtors
*/
#ifndef _LOCK_
#define _LOCK_

#include "../compiler/unused.h"

#if __linux__            // VOLTRON_EXCLUDE
#include <pthread.h>
#elif _WIN32            // VOLTRON_EXCLUDE
#include <windows.h>
#endif                    // VOLTRON_EXCLUDE

class Lock
{
    public:

        Lock()
        {
            initialise();
        }

        ~Lock()
        {
            uninitialise();
        }

        void initialise()
        {
            #if __linux__
            pthread_mutex_init(&m_mutex, nullptr);
            #elif _WIN32
            InitializeCriticalSection(&m_critical_section);
            #endif
        }

        void uninitialise()
        {
            #if __linux__
            pthread_mutex_destroy(&m_mutex);
            #elif _WIN32
            DeleteCriticalSection(&m_critical_section);
            #endif
        }

        void lock()
        {
            #if __linux__
            pthread_mutex_lock(&m_mutex);
            #elif _WIN32
            EnterCriticalSection(&m_critical_section);
            #endif
        }

        void unlock()
        {
            #if __linux__
            pthread_mutex_unlock(&m_mutex);
            #elif _WIN32
            LeaveCriticalSection(&m_critical_section);
            #endif
        }

    private:

        #if __linux__
        pthread_mutex_t m_mutex;
        #elif _WIN32
        CRITICAL_SECTION m_critical_section;
        #endif
};
#endif