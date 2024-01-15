/*  
    Standard C++ thread_local keyword does not allow you to specify thread specific destructors
    and also can't be applied to class members
*/
#ifndef _THREAD_LOCAL_STORAGE_
#define _THREAD_LOCAL_STORAGE_

#include <cstdint>

#if __linux__ // VOLTRON_EXCLUDE
#include <pthread.h>
#elif _WIN32 // VOLTRON_EXCLUDE
#include <windows.h>
#include <fibersapi.h>
#endif // VOLTRON_EXCLUDE

class ThreadLocalStorage
{
    public:

        static ThreadLocalStorage& get_instance()
        {
            static ThreadLocalStorage instance;
            return instance;
        }

        // Call it only once for a process
        bool create(void(*thread_destructor)(void*) = nullptr)
        {
            #if __linux__
            return pthread_key_create(&m_tls_index, thread_destructor) == 0;
            #elif _WIN32
            // Using FLSs rather TLS as it is identical + TLSAlloc doesn't support dtor
            m_tls_index = FlsAlloc(thread_destructor);
            return m_tls_index == FLS_OUT_OF_INDEXES ? false : true;
            #endif
        }

        // Same as create
        void destroy()
        {
            if (m_tls_index)
            {
                #if __linux__
                pthread_key_delete(m_tls_index);
                #elif _WIN32
                FlsFree(m_tls_index);
                #endif

                m_tls_index = 0;
            }
        }

        // GUARANTEED TO BE THREAD-SAFE/LOCAL
        void* get()
        {
            #if __linux__
            return pthread_getspecific(m_tls_index);
            #elif _WIN32
            return FlsGetValue(m_tls_index);
            #endif
        }

        void set(void* data_address)
        {
            #if __linux__
            pthread_setspecific(m_tls_index, data_address);
            #elif _WIN32
            FlsSetValue(m_tls_index, data_address);
            #endif
        }

        // DOES NOT INVOKE SYSCALLS , JUST ACCESSES SEGMENT REGISTERS
        // SO IDEAL FOR IDENTIFIYING THREADS THAT USE TLS
        static inline uint64_t get_thread_local_storage_id(void)
        {
            uint64_t result;
            #ifdef _WIN32
            // GS
            result = reinterpret_cast<uint64_t>(NtCurrentTeb());
            #elif __linux__
            // FS
            asm volatile("mov %%fs:0, %0" : "=r"(result));
            #endif

            return result;
        }

    private:
            #if __linux__
            pthread_key_t m_tls_index = 0;
            #elif _WIN32
            unsigned long m_tls_index = 0;
            #endif

            ThreadLocalStorage() = default;
            ~ThreadLocalStorage() = default;

            ThreadLocalStorage(const ThreadLocalStorage& other) = delete;
            ThreadLocalStorage& operator= (const ThreadLocalStorage& other) = delete;
            ThreadLocalStorage(ThreadLocalStorage&& other) = delete;
            ThreadLocalStorage& operator=(ThreadLocalStorage&& other) = delete;
};

#endif