/*
    MULTITHREADED BENCHMARK CREATES MULTIPLE THREADS WHERE EACH THREAD MAKES ALLOCATIONS
    ONCE THREADS COMPLETE THEIR ALLOCATIONS , THEY START TO DEALLOCATE POINTERS WHICH WERE ALLOCATED BY DIFFERENT THREADS.
*/

#ifndef __BENCHMARK_H__
#define __BENCHMARK_H__

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <array>
#include <thread>
#include <memory>
#include <string>
#include "../benchmark_utilities.h"

struct Allocation
{
    void* ptr = nullptr;
    bool allocated = false;
    bool deallocated = false;
};

//////////////////////////////////////////////////
// CHOOSE BENCHMARK SUITE YOU WANT TO RUN
//#define RUN_BENCHMARK_SINGLETHREADED
#define RUN_BENCHMARK_MULTITHREADED
//////////////////////////////////////////////////
// BENCHMARK VARIABLES
static constexpr std::size_t SIZE_CLASS_COUNT = 8;
static constexpr std::size_t SCALE = 50;
static constexpr std::size_t SIZE_CLASSES[SIZE_CLASS_COUNT] = { 16,32,64,128,256,512,1024,2048 };
static constexpr std::size_t ALLOCATION_COUNTS_FOR_SIZE_CLASSES[SIZE_CLASS_COUNT] = { 4089*SCALE,2044*SCALE,1022*SCALE,511*SCALE,255*SCALE,127*SCALE,63*SCALE,31*SCALE};
static constexpr std::size_t TOTAL_ALLOCATIONS_PER_THREAD = 8142*SCALE;
// THREAD COUNTS ARE BASED ON MY CURRENT TARGET DEVICES PHYSICAL CORE COUNTS. CHANGE ACCORDINGLY IN YOUR CASE.
#if __linux__
static constexpr std::size_t THREAD_COUNT = 4;
#elif _WIN32
static constexpr std::size_t THREAD_COUNT = 8;
#endif
//////////////////////////////////////////////////
void run_multithreaded_benchmark(const char* samples_output_file);
bool do_reads_writes_on_buffer(void* buffer, std::size_t buffer_size);

#define ENABLE_READS_AND_WRITES

using AllocationBucket = std::array<Allocation, TOTAL_ALLOCATIONS_PER_THREAD>;
std::array<AllocationBucket, THREAD_COUNT> allocation_buckets;

void run_multithreaded_benchmark(const char* samples_output_file="samples.txt")
{
    Stopwatch<StopwatchType::STOPWATCH_WITH_RDTSCP> stopwatch;

    auto cpu_frequency = ProcessorUtilities::get_current_cpu_frequency_hertz();
    Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "Current CPU frequency ( not min or max ) : " + std::to_string(cpu_frequency) + " Hz\n" );

    stopwatch.start();
    //////////////////////////////////////////////////////////////////
    auto thread_function = [&](std::size_t allocation_bucket_index, std::size_t deallocation_bucket_index)
    {
        std::size_t counter = 0;
        // ALLOCATIONS
        for (std::size_t i = 0; i < SIZE_CLASS_COUNT; i++)
        {
            std::size_t current_size_class_allocation_count = ALLOCATION_COUNTS_FOR_SIZE_CLASSES[i];

            for (std::size_t j = 0; j < current_size_class_allocation_count; j++)
            {
                void* ptr = nullptr;
                std::size_t allocation_size = SIZE_CLASSES[i];

                ptr = malloc(allocation_size);

                #ifdef ENABLE_READS_AND_WRITES
                if( ! do_reads_writes_on_buffer(ptr, allocation_size) ) { Console::console_output_with_colour(ConsoleColour::FG_RED, "ALLOCATION FAILED !!!\n");}
                #endif

                allocation_buckets[allocation_bucket_index][counter].ptr = ptr;
                allocation_buckets[allocation_bucket_index][counter].allocated = true;

                counter++;
            }
        }

        // DEALLOCATIONS
        auto remaining_deallocations = TOTAL_ALLOCATIONS_PER_THREAD;

        while (true)
        {
            for (auto& allocation : allocation_buckets[deallocation_bucket_index])
            {
                if (allocation.allocated == true && allocation.deallocated == false)
                {
                        free(allocation.ptr);
                        allocation.deallocated = true; // To avoid double-frees
                        remaining_deallocations--;
                }
            }

            if (remaining_deallocations == 0)
            {
                break;
            }
        };
    };

    std::array<std::unique_ptr<std::thread>, THREAD_COUNT> threads;

    for (auto i{ 0 }; i < THREAD_COUNT; i++)
    {
        threads[i] = std::unique_ptr<std::thread>(new std::thread(thread_function, i, THREAD_COUNT-1-i));
    }

    for (auto& thread : threads)
    {
        thread->join();
    }


    for(auto& bucket : allocation_buckets)
    {
        for(auto& allocation : bucket)
        {
            if(allocation.allocated == false || allocation.deallocated == false) { Console::console_output_with_colour(ConsoleColour::FG_RED, "TEST FAILED !!!\n");}
        }
    }
    //////////////////////////////////////////////////////////////////
    stopwatch.stop();
    auto duration_in_microseconds = stopwatch.get_elapsed_microseconds(cpu_frequency);
    Console::console_output_with_colour(ConsoleColour::FG_RED, "Duration : " + std::to_string(duration_in_microseconds) + " microseconds\n" );

    std::ofstream outfile(samples_output_file, std::ios_base::app);
    outfile << duration_in_microseconds << std::endl;
    outfile.close();
}

bool do_reads_writes_on_buffer(void* buffer, std::size_t buffer_size)
{
    if(buffer == nullptr)
    {
        return false;
    }

    char* char_buffer = static_cast<char*>(buffer);

    // TRY WRITING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        char* dest = char_buffer + i;
        *dest = static_cast<char>(i);
    }

    // NOW CHECK READING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        auto test = char_buffer[i];
        if (test != static_cast<char>(i))
        {
            return false;
        }
    }

    return true;
}

void run_benchmark (int argc, char* argv[])
{
    run_multithreaded_benchmark();

    #ifdef _WIN32
    std::system("pause");
    #endif

}

#endif