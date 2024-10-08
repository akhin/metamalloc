/*
    Provides :

                static unsigned int get_number_of_logical_cores()
                static unsigned int get_number_of_physical_cores()
                static bool is_hyper_threading()

                static inline void yield()
                static inline void sleep_in_nanoseconds(unsigned long nanoseconds)

                static int get_current_core_id()
                static unsigned long get_current_thread_id()

                static int pin_calling_thread_to_cpu_core(int core_id)
                static void set_thread_name(unsigned long thread_id, const std::string_view name)
                static bool set_priority(ThreadPriority priority)

*/
#ifndef _THREAD_UTILITIES_
#define _THREAD_UTILITIES_

#ifdef __linux__        // VOLTRON_EXCLUDE
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#elif _WIN32            // VOLTRON_EXCLUDE
#include <windows.h>
#include <chrono>
#include <thread>
#endif                    // VOLTRON_EXCLUDE

#include <array>
#include <string_view>
#include <type_traits>

enum class ThreadPriority
{
    IDLE,
    BELOW_NORMAL,
    NORMAL,
    ABOVE_NORMAL,
    CRITICAL
};

struct ThreadPriorityNode
{
    ThreadPriority priority;
    int value;
};

const static std::array<ThreadPriorityNode, 5> NATIVE_THREAD_PRIORITIES =
{
    //DO POD INITIALISATION
    {
        #ifdef __linux__
        ThreadPriority::IDLE, 19,
        ThreadPriority::BELOW_NORMAL, 1,
        ThreadPriority::NORMAL, 0,
        ThreadPriority::ABOVE_NORMAL, -1,
        ThreadPriority::CRITICAL, -20
        #elif _WIN32
        ThreadPriority::IDLE, THREAD_PRIORITY_IDLE,    //-15
        ThreadPriority::BELOW_NORMAL, THREAD_PRIORITY_BELOW_NORMAL, // -1
        ThreadPriority::NORMAL, THREAD_PRIORITY_NORMAL, // 0
        ThreadPriority::ABOVE_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, // 1
        ThreadPriority::CRITICAL, THREAD_PRIORITY_TIME_CRITICAL // 15
        #endif
    }
};

/*
    Currently this module is not hybrid-architecture-aware
    Ex: P-cores and E-cores starting from Alder Lake
    That means all methods assume that all CPU cores are identical
*/

class ThreadUtilities
{
    public:

        static constexpr inline int MAX_THREAD_NAME_LENGTH = 16; // Limitation comes from Linux

        static unsigned int get_current_pid()
        {
            unsigned int ret{ 0 };
            #ifdef __linux__
            ret = static_cast<unsigned int>(getpid());
            #elif _WIN32
            ret = static_cast<unsigned int>(GetCurrentProcessId());
            #endif
            return ret;
        }

        static unsigned int get_number_of_logical_cores()
        {
            unsigned int num_cores{0};
            #ifdef __linux__
            num_cores = sysconf(_SC_NPROCESSORS_ONLN);
            #elif _WIN32
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            num_cores = sysinfo.dwNumberOfProcessors;
            #endif
            return num_cores;
        }

        static unsigned int get_number_of_physical_cores()
        {
            auto num_logical_cores = get_number_of_logical_cores();
            bool cpu_hyperthreading = is_hyper_threading();
            return cpu_hyperthreading ? num_logical_cores / 2 : num_logical_cores;
        }

        static bool is_hyper_threading()
        {
            bool ret = false;

            #ifdef __linux__
            // Using syscalls to avoid dynamic memory allocation
            int file_descriptor = open("/sys/devices/system/cpu/smt/active", O_RDONLY);

            if (file_descriptor != -1)
            {
                char value;
                if (read(file_descriptor, &value, sizeof(value)) > 0)
                {
                    int smt_active = value - '0';
                    ret = (smt_active > 0);
                }

                close(file_descriptor);
            }
            #elif _WIN32
            SYSTEM_INFO sys_info;
            GetSystemInfo(&sys_info);
            char buffer[2048]; // It may be insufficient however even if one logical processor has SMT flag , it means we are hyperthreading
            DWORD buffer_size = sizeof(buffer);

            GetLogicalProcessorInformation(reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer), &buffer_size);

            DWORD num_system_logical_processors = buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            for (DWORD i = 0; i < num_system_logical_processors; ++i)
            {
                if (reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer[i])->Relationship == RelationProcessorCore)
                {
                    if (reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer[i])->ProcessorCore.Flags == LTP_PC_SMT)
                    {
                        ret = true;
                        break;
                    }
                }
            }
            #endif
            return ret;
        }

        static inline void yield()
        {
            #ifdef __linux__
            sched_yield();
            #elif _WIN32
            SwitchToThread();
            #endif
        }
        
        static inline void sleep_in_nanoseconds(unsigned long nanoseconds)
        {
            #ifdef __linux__
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = nanoseconds;
            nanosleep(&ts, nullptr);
            #elif _WIN32
            std::this_thread::sleep_for(std::chrono::nanoseconds(nanoseconds));
            #endif
        }

        static int pin_calling_thread_to_cpu_core(int core_id)
        {
            int ret{ -1 };
            #ifdef __linux__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(core_id, &cpuset);
            ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            #elif _WIN32
            unsigned long mask = 1 << (core_id);

            if (SetThreadAffinityMask(GetCurrentThread(), mask))
            {
                ret = 0;
            }
            #endif
            return ret;
        }

        static int get_current_core_id()
        {
            int current_core_id{ -1 };
            #ifdef __linux__
            current_core_id = ::sched_getcpu();
            #elif _WIN32
            current_core_id = ::GetCurrentProcessorNumber();
            #endif
            return current_core_id;
        }

        static unsigned long get_current_thread_id()
        {
            unsigned long thread_id{ 0 };
            #ifdef __linux__
            thread_id = pthread_self();
            #elif _WIN32
            thread_id = ::GetCurrentThreadId();
            #endif
            return thread_id;
        }

        static void set_thread_name(unsigned long thread_id, const std::string_view name)
        {
            auto name_length = name.length();

            if (name_length > 0  && name_length <= MAX_THREAD_NAME_LENGTH )
            {
                #ifdef __linux__
                pthread_setname_np(thread_id, name.data());
                #elif _WIN32
                // As documented on MSDN
                // https://msdn.microsoft.com/en-us/library/xcb2z8hs(v=vs.120).aspx
                const DWORD MS_VC_EXCEPTION = 0x406D1388;

                #pragma pack(push,8)
                typedef struct tagTHREADNAME_INFO
                {
                    DWORD dwType; // Must be 0x1000.
                    LPCSTR szName; // Pointer to name (in user addr space).
                    DWORD dwThreadID; // Thread ID (-1=caller thread).
                    DWORD dwFlags; // Reserved for future use, must be zero.
                } THREADNAME_INFO;
                #pragma pack(pop)

                THREADNAME_INFO info;
                info.dwType = 0x1000;
                info.szName = name.data();
                info.dwThreadID = thread_id;
                info.dwFlags = 0;

                __try
                {
                    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
                #endif
            }
        }

        static bool set_priority(ThreadPriority priority)
        {
            bool success{ false };
            int index = static_cast<std::underlying_type<ThreadPriority>::type>(priority);
            auto native_priority_value = NATIVE_THREAD_PRIORITIES[index].value;
            #ifdef __linux__
            struct sched_param param;
            param.__sched_priority = native_priority_value;
            int policy = sched_getscheduler(getpid());
            pthread_setschedparam(pthread_self(), policy, &param);
            #elif _WIN32
            if (SetThreadPriority(GetCurrentThread(), native_priority_value) != 0)
            {
                success = true;
            }
            #endif
            return success;
        }

    private:
};

#endif