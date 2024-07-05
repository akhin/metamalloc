#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#elif _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>

#include <memory>
#include <atomic>
#include <thread>
#include <vector>

using namespace std;

bool is_hyper_threading();
void sleep_in_microseconds(unsigned long microseconds);
unsigned int get_number_of_logical_cores();
unsigned int get_number_of_physical_cores();
int pin_calling_thread_to_cpu_core(int core_id);
const std::string get_system_jitter_report(unsigned long duration_microseconds);

int main()
{
    unsigned long duration_in_microsecs = 1000000;
    string user_input;
    
    while (true)
    {
        cout << "Specify measurement duration in microseconds ( or press enter for default value " << duration_in_microsecs << " ) :" << endl;
        getline(cin, user_input);
        
        if(user_input.empty())
        {
            break;
        }
        
        try
        {
            duration_in_microsecs = std::stol(user_input);
            break;
        }
        catch(...)
        {
            continue;
        }
    }
    
    cout << endl << "Starting..." << endl << endl;
    cout << get_system_jitter_report(duration_in_microsecs) << endl;

    #ifdef __linux__
    int result = system("bash -c 'read -p \"Press Enter to continue...\"'");
    (void)result;
    #elif _WIN32
    std::system("pause");
    #endif

    return 0;
}

struct Counter
{
    unsigned long value=0;
    std::size_t core_index=0;
};

bool compare_counters(const Counter &a, const Counter &b)
{
    return a.value < b.value;
}

const std::string get_system_jitter_report(unsigned long duration_microseconds)
{
    auto num_cores = get_number_of_physical_cores();

    std::vector<std::thread> threads;
    std::vector<Counter> counters;

    std::atomic_bool is_ending{ false };

    auto jitter_measurement_function = [&counters, &is_ending](std::size_t thread_index)
    {
        // We are pinning threads to each physical CPU core.
        // In case of hyperthreading , we use even indices to avoid hyperthreading effects
        auto target_cpu_core = is_hyper_threading() ? thread_index * 2 : thread_index;
        pin_calling_thread_to_cpu_core(target_cpu_core);

        unsigned long counter_val = 0;
        while (is_ending.load() == false)
        {
            counter_val++;
        }
        
        counters[thread_index].value = counter_val;
    };

    for (std::size_t i{ 0 }; i < num_cores; i++)
    {
        counters.push_back(Counter{0, i});
        threads.emplace_back(std::thread(jitter_measurement_function, i));
    }

    sleep_in_microseconds(duration_microseconds);
    is_ending.store(true);

    for (auto& measurement_thread : threads)
    {
        measurement_thread.join();
    }

    std::stringstream report;
    report << "Ran " << num_cores << " threads , each running a counter & pinned to corresponding physical core, for a duration of " << duration_microseconds << " microseconds" << std::endl << std::endl;

    std::sort(counters.begin(), counters.end(), compare_counters);
    
    std::size_t i =0;
    for(auto& counter : counters)
    {
        report << "CPU core " << counter.core_index << " counter value : " << counter.value;
        
        if(i==0)
        {
            report << " <= The most interrupted";
        }
        else if(i== num_cores-1)
        {
            report << " <= The least interrupted";
        }
        
        report << std::endl;
        
        i++;
    }

    return report.str();
}

void sleep_in_microseconds(unsigned long microseconds)
{
    #ifdef __linux__
    usleep(microseconds);
    #elif _WIN32
    // In Windows , the sleep granularity is 1 millisecond , therefore min wait will be 1000 microsecs
    if (microseconds < 1000)
    {
        Sleep(1);
        return;
    }
    else
    {
        auto iterations = microseconds / 1000;
        for (unsigned long i{ 0 }; i < iterations; i++)
        {
            Sleep(1);
        }
    }
    #endif
}

bool is_hyper_threading()
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

unsigned int get_number_of_logical_cores()
{
    unsigned int numCores(0);
    #ifdef __linux__
    numCores = sysconf(_SC_NPROCESSORS_ONLN);
    #elif _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    numCores = sysinfo.dwNumberOfProcessors;
    #endif
    return numCores;
}

unsigned int get_number_of_physical_cores()
{
    auto num_logical_cores = get_number_of_logical_cores();
    bool cpu_hyperthreading = is_hyper_threading();
    return cpu_hyperthreading ? num_logical_cores / 2 : num_logical_cores;
}

int pin_calling_thread_to_cpu_core(int core_id)
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
