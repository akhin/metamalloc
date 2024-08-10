/*

CLASSES:
        Stopwatch                                   based on RDTSCP ( or CPUID+RDTSC )

        ProcessorUtilities                          rdtscp rdtsc cpuid
                                                    get_current_cpu_frequency_hertz ( You need it to calc microseconds from cpuid+RDTSC or RDTSCP tick counts from Stopwatch )
                                                    get_current_core_id
                                                    pin_calling_thread_to_cpu_core
                                                    cache_flush
													
		ASLR										disable_aslr_for_this_process , Linux only

        MemoryStats                                 get_stats() & get_human_readible_size

        Statistics                                  Gives you min,max,average and percentiles : P50 P75 P90 P95 P99

        Console                                     console_output_with_colour

        LinuxInfo                                   Linux kernel version & CPU Isolation info
        
        PMCUtilities                                Intel Performance Counter Monitor values
                                                    Only on Linux , see "GETTING INTEL PMC VALUES" below

        RandomNumberGenerator                       Use it to get random integers

BENCHMARK MACROS & DO_NOT_OPTIMISE MACRO :

            BENCHMARK_BEGIN(iteration_count)
            // YOUR CODE WHICH WILL BE BENCHMARKED
            BENCHMARK_END()
            // THEN AFTER IT TO SEE THE RESULTS YOU DO :
            report.print("title");

            // ADDITIONAL THINGS YOU CAN DO BETWEEN BENCHMARK_START & BENCHMARK_END :

            DO_NOT_OPTIMISE(var);


            //  BENCHMARK REPORT OUTPUT ( IT WILL BE COLOURED ) :

                Current CPU frequency ( not min or max ) : 1797000000 Hz

                Title : system malloc
                Iteration count : 100
                Minimum time : 34 microseconds
                Maximum time : 137 microseconds
                Average time : 46.780000 microseconds
                P50 : 35.000000 microseconds
                P75 : 56.000000 microseconds
                P90 : 76.000000 microseconds
                P95 : 87.000000 microseconds
                P99 : 137.000000 microseconds


            By default it uses RDTSCP. If you work on a system without RDTSCP ,then  change the line below :

            from         #define STOPWATCH_TYPE StopwatchType::STOPWATCH_WITH_RDTSCP

            to            #define STOPWATCH_TYPE StopwatchType::STOPWATCH_WITH_CPUID_AND_RDTSC

GETTING INTEL PMC VALUES ( ONLY ON LINUX : PMC ACCESS NEED KERNEL MODE PRIVILEGE. UNLIKE WINDOWS , LINUX PROVIDES A DRIVER SO THAT YOU CAN USE IT VIA IOCTLs FROM USER SPACE ):

        Note : You need to sudo / run as admin. start_reading_pmc will display a red error message if the process permisions don't suffice.

        PMCUtilities::start_reading_pmc();
        ////////////////////////////////////////////////////////////////////
        // Generate cache misses
        int array[40960];
        int step = 64; // Adjust this value as needed
        int sum = 0;

        for (int i = 0; i < 40960; i += step)
        {
            sum += array[i];
        }
        ////////////////////////////////////////////////////////////////////
        PMCUtilities::stop_reading_pmc();
        std::cout << "Context switches: " << PMCUtilities::get_pmc_values().context_switch_count << std::endl;
        std::cout << "Page faults: " << PMCUtilities::get_pmc_values().page_fault_count << std::endl;
        std::cout << "Branch mispredictions: " << PMCUtilities::get_pmc_values().branch_misprediction_count << std::endl;
        std::cout << "CPU cycles: " << PMCUtilities::get_pmc_values().cycle_count << std::endl;
        std::cout << "LLC misses (read): " << PMCUtilities::get_pmc_values().cache_misses_count_read << std::endl;
        std::cout << "LLC misses (write): " << PMCUtilities::get_pmc_values().cache_misses_count_write << std::endl; // perf stat -e LLC-load-misses actually shows combination of read and write

*/
#ifndef _BENCHMARK_UTILITIES_H_
#define _BENCHMARK_UTILITIES_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <intrin.h>
#if _DEBUG
#pragma message("Warning: Building in Debug mode !!!")
#endif
#elif __linux__
#include <x86intrin.h>
#include <string.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <linux/version.h>
#include <sys/personality.h>
#endif

///////////////////////////////////////////////////////////////////
// ProcessorUtilities
class ProcessorUtilities
{
    public:

        static void cpuid(int function, unsigned int* eax, unsigned int* ebx, unsigned int* ecx, unsigned int* edx)
        {
            #ifdef __linux__
            // Not using cpuid.h on Linux as that header doesn`t have include protection
            asm volatile(
                "cpuid"
                : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
                : "a" (function), "c" (0)
            );
            #elif _WIN32
            int info[4];
            __cpuid(info, function);
            *eax = info[0];
            *ebx = info[1];
            *ecx = info[2];
            *edx = info[3];
            #endif
        }

        static void cache_flush(void* address)
        {
            _mm_clflush(address);
        }

        // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=rdtsc&ig_expand=5802
        static unsigned long long rdtsc()
        {
            return __rdtsc();
        }

        // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=rdtscp&ig_expand=5802,5803
        static unsigned long long rdtscp()
        {
            unsigned int model_specific_register_contents;
            return __rdtscp(&model_specific_register_contents);
        }

        static unsigned long long get_current_cpu_frequency_hertz()
        {
            unsigned long long ret{ 0 };
            #ifdef _WIN32
            DWORD data, data_size = sizeof(data);
            SHGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "~MHz", nullptr, &data, &data_size);
            ret = ((unsigned long long)data * (unsigned long long)(1000 * 1000));
            #elif __linux__
            std::ifstream cpuinfo("/proc/cpuinfo");
            std::string line;
            while (std::getline(cpuinfo, line))
            {
                if (line.find("cpu MHz") != std::string::npos)
                {
                    std::size_t colon_pos = line.find(':');

                    if (colon_pos != std::string::npos)
                    {
                        std::string freq_str = line.substr(colon_pos + 2);
                        ret = std::stol(freq_str) * 1000000; // Convert to hertz
                        break;
                    }
                }
            }
            cpuinfo.close();
            #endif
            return ret;
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
};

///////////////////////////////////////////////////////////////////
// Console
enum class ConsoleColour
{
    FG_DEFAULT,
    FG_RED,
    FG_GREEN,
    FG_BLUE,
    FG_YELLOW
};

class Console
{
    public:
        struct ConsoleColourNode
        {
            ConsoleColour colour;
            int value;
        };

        /*
            Currently not using C++20`s std::format since GCC support started from only v13.
            After starting using it , can templatise console_output_with_colour method as below:

                #include <format>
                #include <string_view>

                template <typename... Args>
                inline void console_output_with_colour(ConsoleColour foreground_colour, std::string_view message, Args&&... args)
                {
                    std::string buffer = std::vformat(message, std::make_format_args(args...));
                    ...
        */
        static void console_output_with_colour(ConsoleColour foreground_colour, const std::string_view& buffer)
        {
            auto fg_index = static_cast<std::underlying_type<ConsoleColour>::type>(foreground_colour);
            auto foreground_colour_code = NATIVE_CONSOLE_COLOURS[fg_index].value;
            #ifdef _WIN32
            HANDLE handle_console = GetStdHandle(STD_OUTPUT_HANDLE);
            auto set_console_attribute = [&handle_console](int code) { SetConsoleTextAttribute(handle_console, code);  };
            set_console_attribute(foreground_colour_code | FOREGROUND_INTENSITY);
            FlushConsoleInputBuffer(handle_console);
            std::cout << buffer;
            SetConsoleTextAttribute(handle_console, 15); //set back to black background and white text
            #elif __linux__
            std::string ansi_colour_code = "\033[0;" + std::to_string(foreground_colour_code) + "m";
            std::cout << ansi_colour_code << buffer << "\033[0m";
            #endif
        }

    private:
        static inline constexpr std::array<ConsoleColourNode, 5> NATIVE_CONSOLE_COLOURS =
        {
            //DO POD INITIALISATION
            {
                #ifdef __linux__
                // https://en.wikipedia.org/wiki/ANSI_escape_code#graphics
                ConsoleColour::FG_DEFAULT, 0,
                ConsoleColour::FG_RED, 31,
                ConsoleColour::FG_GREEN, 32,
                ConsoleColour::FG_BLUE, 34,
                ConsoleColour::FG_YELLOW, 33,
                #elif _WIN32
                ConsoleColour::FG_DEFAULT, 0,
                ConsoleColour::FG_RED, FOREGROUND_RED,
                ConsoleColour::FG_GREEN, FOREGROUND_GREEN,
                ConsoleColour::FG_BLUE, FOREGROUND_BLUE,
                ConsoleColour::FG_YELLOW, (FOREGROUND_RED | FOREGROUND_GREEN),
                #endif
            }
        };
};

///////////////////////////////////////////////////////////////////
// LinuxInfo
#ifdef __linux__

class LinuxInfo
{
    public:
    
        static const std::string get_linux_kernel_version()
        {
            std::string ret;
            
            unsigned int linux_version = LINUX_VERSION_CODE;
            unsigned int major = (linux_version >> 16) & 0xFF;
            unsigned int minor = (linux_version >> 8) & 0xFF;
            unsigned int revision = linux_version & 0xFF;
            
            ret = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision);
            
            return ret;
        }
        
        static const std::string get_cpu_isolation_info()
        {
            std::string result;
            std::array<char, 128> buffer;
            
            FILE* pipe = popen("sudo cat /proc/cmdline | grep --color=auto isolcpus=", "r");
            if (!pipe) 
            {
                throw std::runtime_error("popen() failed!");
            }
            
            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) 
            {
                result += buffer.data();
            }

            pclose(pipe);

            return result;
        }
};

#endif

///////////////////////////////////////////////////////////////////
// PMC Utilities

#ifdef __linux__

struct PMCValues
{
    // OS
    uint64_t context_switch_count = 0;
    uint64_t page_fault_count = 0;
    // CPU
    uint64_t branch_misprediction_count = 0;
    uint64_t cycle_count = 0;
    // CPU Cache
    uint64_t cache_misses_count_read = 0;
    uint64_t cache_misses_count_write = 0;
};

class PMCUtilities
{
    public:
        static void start_reading_pmc()
        {
            if(am_i_admin()==false)
            {
                Console::console_output_with_colour(ConsoleColour::FG_RED, "You need admin rights to read PMC values. Please re-run as admin.\n");
            }

            pmc_fd.fd_page_faults = setup_perf_event(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS);
            ioctl(pmc_fd.fd_page_faults, PERF_EVENT_IOC_RESET, 0);
            ioctl(pmc_fd.fd_page_faults, PERF_EVENT_IOC_ENABLE, 0);

            pmc_fd.fd_context_switches = setup_perf_event(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES);
            ioctl(pmc_fd.fd_context_switches, PERF_EVENT_IOC_RESET, 0);
            ioctl(pmc_fd.fd_context_switches, PERF_EVENT_IOC_ENABLE, 0);

            pmc_fd.fd_branch_mispredictions = setup_perf_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES);
            ioctl(pmc_fd.fd_branch_mispredictions, PERF_EVENT_IOC_RESET, 0);
            ioctl(pmc_fd.fd_branch_mispredictions, PERF_EVENT_IOC_ENABLE, 0);

            pmc_fd.fd_cycles = setup_perf_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
            ioctl(pmc_fd.fd_cycles, PERF_EVENT_IOC_RESET, 0);
            ioctl(pmc_fd.fd_cycles, PERF_EVENT_IOC_ENABLE, 0);

            pmc_fd.fd_cache_misses_read = setup_perf_event(PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_MISSES | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16) | (PERF_COUNT_HW_CACHE_OP_READ << 8) );
            ioctl(pmc_fd.fd_cache_misses_read, PERF_EVENT_IOC_RESET, 0);
            ioctl(pmc_fd.fd_cache_misses_read, PERF_EVENT_IOC_ENABLE, 0);

            pmc_fd.fd_cache_misses_write = setup_perf_event(PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_MISSES | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16) | (PERF_COUNT_HW_CACHE_OP_WRITE << 8) );
            ioctl(pmc_fd.fd_cache_misses_write, PERF_EVENT_IOC_RESET, 0);
            ioctl(pmc_fd.fd_cache_misses_write, PERF_EVENT_IOC_ENABLE, 0);
        }

        static void stop_reading_pmc()
        {
            ioctl(pmc_fd.fd_page_faults, PERF_EVENT_IOC_DISABLE, 0);
            auto val_pagefaults = read(pmc_fd.fd_page_faults, &(pmc_values.page_fault_count), sizeof(pmc_values.page_fault_count));
            (void)(val_pagefaults);
            close(pmc_fd.fd_page_faults);

            ioctl(pmc_fd.fd_context_switches, PERF_EVENT_IOC_DISABLE, 0);
            auto val_context_switches = read(pmc_fd.fd_context_switches, &(pmc_values.context_switch_count), sizeof(pmc_values.context_switch_count));
            (void)(val_context_switches);
            close(pmc_fd.fd_context_switches);

            ioctl(pmc_fd.fd_branch_mispredictions, PERF_EVENT_IOC_DISABLE, 0);
            auto val_mispredictions = read(pmc_fd.fd_branch_mispredictions, &(pmc_values.branch_misprediction_count), sizeof(pmc_values.branch_misprediction_count));
            (void)(val_mispredictions);
            close(pmc_fd.fd_branch_mispredictions);

            ioctl(pmc_fd.fd_cycles, PERF_EVENT_IOC_DISABLE, 0);
            auto val_cycles = read(pmc_fd.fd_cycles, &(pmc_values.cycle_count), sizeof(pmc_values.cycle_count));
            (void)(val_cycles);
            close(pmc_fd.fd_cycles);

            ioctl(pmc_fd.fd_cache_misses_read, PERF_EVENT_IOC_DISABLE, 0);
            auto val_cache_misses_read = read(pmc_fd.fd_cache_misses_read, &(pmc_values.cache_misses_count_read), sizeof(pmc_values.cache_misses_count_read));
            (void)(val_cache_misses_read);
            close(pmc_fd.fd_cache_misses_read);

            ioctl(pmc_fd.fd_cache_misses_write, PERF_EVENT_IOC_DISABLE, 0);
            auto val_cache_misses_write = read(pmc_fd.fd_cache_misses_write, &(pmc_values.cache_misses_count_write), sizeof(pmc_values.cache_misses_count_write));
            (void)(val_cache_misses_write);
            close(pmc_fd.fd_cache_misses_write);
        }

        static PMCValues get_pmc_values()
        {
            return pmc_values;
        }

    private:
        static inline PMCValues pmc_values;

        struct PMCFileDescriptors
        {
            int fd_page_faults;
            int fd_context_switches;
            int fd_branch_mispredictions;
            int fd_cycles;
            int fd_cache_misses_read;
            int fd_cache_misses_write;
        };

        static inline PMCFileDescriptors pmc_fd;

        static int setup_perf_event(int type, int config)
        {
            struct perf_event_attr pe;
            int fd;

            // Initialize the perf_event_attr structure
            memset(&pe, 0, sizeof(struct perf_event_attr));
            pe.type = type;
            pe.size = sizeof(struct perf_event_attr);
            pe.config = config;
            pe.disabled = 1;
            pe.exclude_kernel = 1;
            pe.exclude_hv = 1;

            // Open the performance counter event
            fd = syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
            if (fd == -1)
            {
                return -1;
            }

            return fd;
        }

        static bool am_i_admin()
        {
            uid_t euid = geteuid();
            return euid == 0;
        }
};

#endif

///////////////////////////////////////////////////////////////////
// STOPWATCH
enum class StopwatchType
{
    STOPWATCH_WITH_CPUID_AND_RDTSC,
    STOPWATCH_WITH_RDTSCP
};

template <StopwatchType type = StopwatchType::STOPWATCH_WITH_RDTSCP>
class Stopwatch
{
public:

    static unsigned long long cpu_cycles_to_microseconds(unsigned long long cycle_count, unsigned long long cpu_frequency_hertz)
    {
        // cpu_frequency is number of cycles per 1 sec/1000000 microseconds
        // So each cycle takes 1000000/cpu_frequency_hertz microseconds
        double time_per_cycle = 1000000.0 / static_cast<double>(cpu_frequency_hertz);
        return  static_cast<unsigned long long>(static_cast<double>(cycle_count) * time_per_cycle);
    }

    void start()
    {
        m_start_cycles = get_cycles();
    }

    void stop()
    {
        m_end_cycles = get_cycles();
    }

    unsigned long long get_elapsed_cycles()
    {

        return m_end_cycles - m_start_cycles;
    }

    unsigned long long get_elapsed_microseconds(unsigned long long cpu_frequency_hertz)
    {
        return  cpu_cycles_to_microseconds(get_elapsed_cycles(), cpu_frequency_hertz);
    }

private:
    unsigned long long m_start_cycles = 0;
    unsigned long long m_end_cycles = 0;

    unsigned long long get_cycles()
    {
        if constexpr (type == StopwatchType::STOPWATCH_WITH_RDTSCP)
        {
            return ProcessorUtilities::rdtscp();
        }
        else if constexpr (type == StopwatchType::STOPWATCH_WITH_CPUID_AND_RDTSC)
        {
            // Executes serialising cpuid instruction
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            ProcessorUtilities::cpuid(0, &eax, &ebx, &ecx, &edx);
            return ProcessorUtilities::rdtsc();
        }
    }
};

///////////////////////////////////////////////////////////////////
// Statistics
template <typename T = double>
class Statistics
{
public:

    void reset()
    {
        m_samples.clear();
    }

    void add_sample(double n)
    {
        m_samples.push_back(n);
    }

    T get_average() const
    {
        auto sample_number = m_samples.size();
        if (!sample_number)
        {
            return -1.0;
        }
        return std::accumulate(m_samples.begin(), m_samples.end(), 0.0) / sample_number;
    }

    T get_minimum() const
    {
        if (!m_samples.size())
        {
            return -1.0;
        }
        return *std::min_element(std::begin(m_samples), std::end(m_samples));
    }

    T get_maximum() const
    {
        if (!m_samples.size())
        {
            return -1.0;
        }
        return *std::max_element(std::begin(m_samples), std::end(m_samples));
    }

    // Note : It sorts the samples
    T get_percentile(int percentile)
    {
        auto sample_number = m_samples.size();

        if (!sample_number)
        {
            return -1.0;
        }

        std::sort(m_samples.begin(), m_samples.end());

        std::size_t index = sample_number * percentile / 100;

        return m_samples[index];
    }

    void print(const std::string& title, const std::string& unit = "microseconds")
    {
        std::cout << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_BLUE, "Title : " + title);
        std::cout << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_GREEN, "Iteration count : " + std::to_string(get_sample_count()));
        std::cout << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "Minimum time : ");
        std::cout << get_minimum() << " " << unit <<  std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "Maximum time : ");
        std::cout << get_maximum() << " " << unit << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "Average time : ");
        Console::console_output_with_colour(ConsoleColour::FG_RED, std::to_string(get_average()) + " " + unit);
        std::cout << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "P50 : ");
        std::cout << std::to_string(get_percentile(50)) << " " << unit << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "P75 : ");
        std::cout << std::to_string(get_percentile(75)) << " " << unit << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "P90 : ");
        std::cout << std::to_string(get_percentile(90)) << " " << unit << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "P95 : ");
        std::cout << std::to_string(get_percentile(95)) << " " << unit << std::endl;

        Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "P99 : ");
        std::cout << std::to_string(get_percentile(99)) << " " << unit << std::endl;

        std::cout << std::endl;
    }

    std::size_t get_sample_count() const
    {
        return m_samples.size();
    }

private:
    std::vector<T> m_samples;
};

///////////////////////////////////////////////////////////////////
// RandomNumberGenerator
class RandomNumberGenerator
{
    public:
        static int get_random_integer(int max_random_number=100)
        {
            int ret{ 0 };
            std::random_device device;
            std::mt19937 rng(device());
            std::uniform_int_distribution<std::mt19937::result_type> distance_random(1, max_random_number);
            ret = distance_random(device);
            return ret;
        }
};

///////////////////////////////////////////////////////////////////
// ASLR
class ASLR
{
	public:
			
			static void disable_aslr_for_this_process(int argc, char* argv[])
			{
				#ifdef __linux__
				auto aslr_disabled = is_aslr_disabled();

				if(aslr_disabled==false)
				{
					auto success_disable_aslr = disable_aslr();

					if(success_disable_aslr)
					{
						// RESTARTING
						execv(argv[0], argv);
					}
				}
				#endif
			}
	
	        static bool is_aslr_disabled()
			{
				#ifdef __linux
				return  personality(ADDR_NO_RANDOMIZE) & ADDR_NO_RANDOMIZE;
				#else
				return true;
				#endif
			}
        
			static bool disable_aslr()
			{
				#ifdef __linux__
				const int old_personality = personality(ADDR_NO_RANDOMIZE);
            
				if (!(old_personality & ADDR_NO_RANDOMIZE)) 
				{
					const int new_personality = personality(ADDR_NO_RANDOMIZE);
					
					if (! (new_personality & ADDR_NO_RANDOMIZE))
					{
						return false;
					}
				}
				#else
				#endif
				
				return true;
			}
};

///////////////////////////////////////////////////////////////////
// Memory stats

// Sizes are in bytes
struct MemoryStatsCollection
{
    // Physical memory
    std::size_t total_physical_memory_size = 0;
    std::size_t physical_memory_current_usage = 0;
    std::size_t physical_memory_peak_usage = 0;
    // Virtual memory
    std::size_t total_virtual_memory_size = 0;
    std::size_t virtual_memory_current_usage = 0;
    // Page faults
    std::size_t hard_page_fault_count = 0;
};

class MemoryStats
{
    public:

        static MemoryStatsCollection get_stats()
        {
            MemoryStatsCollection stats;
            #ifdef __linux__
            struct sysinfo mem_info;
            sysinfo(&mem_info);
            struct rusage rusage;
            getrusage(RUSAGE_SELF, &rusage);

            stats.total_physical_memory_size = static_cast<std::size_t>(mem_info.totalram);
            stats.physical_memory_current_usage = static_cast<std::size_t>(get_physical_memory_current_usage());
            stats.physical_memory_peak_usage = static_cast<std::size_t>(rusage.ru_maxrss * 1024);

            stats.total_virtual_memory_size = static_cast<std::size_t>(mem_info.totalram+ mem_info.totalswap + mem_info.mem_unit);
            stats.virtual_memory_current_usage = static_cast<std::size_t>(get_virtual_memory_current_usage());
            stats.hard_page_fault_count = static_cast<std::size_t>(rusage.ru_majflt);

            #elif _WIN32
            MEMORYSTATUSEX mem_info;
            mem_info.dwLength = sizeof(MEMORYSTATUSEX);
            GlobalMemoryStatusEx(&mem_info);
            PROCESS_MEMORY_COUNTERS_EX pmc;
            GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

            stats.total_physical_memory_size = static_cast<std::size_t>(mem_info.ullTotalPhys);
            stats.physical_memory_current_usage = static_cast<std::size_t>(pmc.WorkingSetSize);
            stats.physical_memory_peak_usage = static_cast<std::size_t>(pmc.PeakWorkingSetSize);

            stats.total_virtual_memory_size = static_cast<std::size_t>(mem_info.ullTotalPageFile);
            stats.virtual_memory_current_usage = static_cast<std::size_t>(pmc.PrivateUsage);
            stats.hard_page_fault_count = static_cast<std::size_t>(pmc.PageFaultCount);
            #endif
            return stats;
        }

        static const std::string get_human_readible_size(std::size_t size_in_bytes)
        {
            const char* suffixes[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
            constexpr int suffix_count = sizeof(suffixes) / sizeof(suffixes[0]);

            std::ostringstream oss;

            double size = static_cast<double>(size_in_bytes);
            constexpr int multiplier = 1024;
            int suffix_index = 0;

            while (size >= multiplier && suffix_index < suffix_count - 1)
            {
                size /= multiplier;
                suffix_index++;
            }

            oss << std::fixed << std::setprecision(2) << size << ' ' << suffixes[suffix_index];
            return oss.str();
        }

    private :

        #ifdef __linux__

        static std::size_t get_physical_memory_current_usage()
        {
            std::size_t result = -1;
            std::ifstream file("/proc/self/status");

            if (!file.is_open())
            {
                return result;
            }

            std::string line;

            while (std::getline(file, line))
            {
                if (line.compare(0, 6, "VmRSS:") == 0)
                {
                    // Example line = VmRSS:\t    2040 kB
                    try // stoi may throw
                    {
                        result = std::stoi(line.substr(6));
                        break;
                    }
                    catch (...)
                    {
                        return -1;
                    }

                }
            }

            file.close();
            return result * 1024;
        }

        static std::size_t get_virtual_memory_current_usage()
        {
            std::size_t result = -1;
            std::ifstream file("/proc/self/status");
            if (!file.is_open())
            {
                return result;
            }

            std::string line;

            while (std::getline(file, line))
            {
                if (line.compare(0, 7, "VmSize:") == 0)
                {
                    // Example line = VmSize:\t    2040 kB
                    result = extract_number(line);
                    break;
                }
            }

            file.close();
            return result * 1024;
        }

        static std::size_t extract_number(const std::string& line)
        {
            std::size_t number = -1;

            std::size_t start = line.find_first_of("0123456789");

            if (start != std::string::npos)
            {
                std::size_t end = line.find_first_not_of("0123456789", start);

                if (end != std::string::npos)
                {
                    std::string number_str = line.substr(start, end - start);
                    try // stoi may throw
                    {
                        number = std::stoi(number_str);
                    }
                    catch (...)
                    {
                        return -1;
                    }
                }
            }

            return number;
        }

        #endif
};


///////////////////////////////////////////////////////////////////
// DO_NOT_OPTIMISE
#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline))
#endif

void dummy(char const volatile*){}

template <typename T>
FORCE_INLINE void DO_NOT_OPTIMISE(T const& value)
{
    // Disables compiler reordering of read and writes ( though it does not prevent CPU reordering )
    #if defined(_MSC_VER)
    dummy(&reinterpret_cast<char const volatile&>(value));
    _ReadWriteBarrier();
    #elif defined(__GNUC__)
    asm volatile("" : : "r,m"(value) : "memory");
    #endif
}

///////////////////////////////////////////////////////////////////
// BENCHMARK_BEGIN & BENCHMARK_END
#define STOPWATCH_TYPE StopwatchType::STOPWATCH_WITH_RDTSCP

#define BENCHMARK_BEGIN(iteration_count)  \
                            Statistics<double> report; Stopwatch<STOPWATCH_TYPE> stopwatch; auto cpu_frequency = ProcessorUtilities::get_current_cpu_frequency_hertz(); \
                            Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "Current CPU frequency ( not min or max ) : " + std::to_string(cpu_frequency) + " Hz" ); std::cout << std::endl; \
                            for (std::size_t iteration{ 0 }; iteration < iteration_count; iteration++){ \
                            stopwatch.start(); \

#define BENCHMARK_END()  \
                            stopwatch.stop();     \
                            report.add_sample(static_cast<double>(stopwatch.get_elapsed_microseconds(cpu_frequency))); \
                            } \

#endif