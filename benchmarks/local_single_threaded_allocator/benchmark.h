#ifndef __BENCHMARK_H__
#define __BENCHMARK_H__

#include <cstdint>
#include <cstddef>
#include <string>

#include "../benchmark_utilities.h"

static constexpr int ISOLATED_CPU_CORE_INDEX = 0;

static constexpr std::size_t    ITERATION_COUNT = 1000;
static constexpr std::size_t     ALLOCATION_SIZE_COUNT = 37;
static constexpr std::size_t    ALLOCATION_SIZES[ALLOCATION_SIZE_COUNT] = {32, 16, 32, 16, 16, 16, 32, 64, 32, 12, 14, 8, 20, 22, 23, 27, 35, 96, 128, 100, 256, 512, 1024, 40, 50, 43, 74, 2048, 1500, 29, 7, 41, 77, 60, 80, 84, 106};
static std::uintptr_t             g_addresses[ALLOCATION_SIZE_COUNT*ITERATION_COUNT] = {};

template <typename AllocatorType>
inline void run_allocator_benchmark()
{
    ProcessorUtilities::pin_calling_thread_to_cpu_core(ISOLATED_CPU_CORE_INDEX);
    AllocatorType allocator;

    auto cpu_frequency = ProcessorUtilities::get_current_cpu_frequency_hertz();

    Stopwatch<StopwatchType::STOPWATCH_WITH_RDTSCP> stopwatch;

    stopwatch.start();
    //////////////////////////////////////////////////////////////////////////////////////////////////////
    std::size_t counter = 0;
    for(std::size_t i =0; i<ITERATION_COUNT; i++)
    {
        for(std::size_t j {0}; j<ALLOCATION_SIZE_COUNT; j++)
        {
            g_addresses[counter] =  reinterpret_cast<std::uintptr_t>(allocator.allocate(ALLOCATION_SIZES[j]));
            counter++;
        }
    }

    stopwatch.stop();
    auto allocations_duration_in_microseconds = stopwatch.get_elapsed_microseconds(cpu_frequency);

    // WRITE TO ALLOCATED ADDRESSES
    for(std::size_t i =0; i <ALLOCATION_SIZE_COUNT*ITERATION_COUNT;i++)
    {
        *reinterpret_cast<std::size_t*>(g_addresses[i]) = i;
    }

    // READ FROM ALLOCATED ADDRESSES
    for(std::size_t i =0; i <ALLOCATION_SIZE_COUNT*ITERATION_COUNT;i++)
    {
        std::size_t current =0;
        current = *reinterpret_cast<std::size_t*>(g_addresses[i]);
        if ( i != current )
        {
            std::cout << "ERROR !!!" << std::endl;
            return;
        }
    }

    stopwatch.start();

    counter = 0;
    std::size_t half_size = ALLOCATION_SIZE_COUNT*ITERATION_COUNT/2;

    // DEALLOCATE FIRST HALF IN FIFO ORDER
    for(std::size_t i =0; i<half_size ;i++)
    {
        allocator.deallocate(  reinterpret_cast<void*>(g_addresses[i]));
    }

    // DEALLOCATE SECOND HALF IN LIFO ORDER
    for(std::size_t i =ALLOCATION_SIZE_COUNT*ITERATION_COUNT-1; i >= half_size ;i--)
    {
        allocator.deallocate(  reinterpret_cast<void*>(g_addresses[i]));
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////
    stopwatch.stop();
    auto deallocations_duration_in_microseconds = stopwatch.get_elapsed_microseconds(cpu_frequency);

    auto allocation_count_per_microsecond = ALLOCATION_SIZE_COUNT*ITERATION_COUNT / allocations_duration_in_microseconds;
    auto deallocation_count_per_microsecond = ALLOCATION_SIZE_COUNT*ITERATION_COUNT / deallocations_duration_in_microseconds;

    Console::console_output_with_colour(ConsoleColour::FG_BLUE, allocator.title());
    Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "\nCpu frequency : " + std::to_string(cpu_frequency) + " Hertz \n\n");

    Console::console_output_with_colour(ConsoleColour::FG_RED, "Allocation time : " );
    std::cout << allocations_duration_in_microseconds << " microseconds" << std::endl;

    Console::console_output_with_colour(ConsoleColour::FG_RED, "Dellocation time : " );
    std::cout << deallocations_duration_in_microseconds << " microseconds" << std::endl;

    Console::console_output_with_colour(ConsoleColour::FG_RED, "Allocation throughput : " );
    std::cout << allocation_count_per_microsecond << " allocations per microsecond" << std::endl;

    Console::console_output_with_colour(ConsoleColour::FG_RED, "Dellocation throughput : " );
    std::cout << deallocation_count_per_microsecond << " deallocations per microsecond" << std::endl;
}


#endif