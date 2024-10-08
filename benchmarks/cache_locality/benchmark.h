#ifndef __BENCHMARK_H__
#define __BENCHMARK_H__

#include <cstdint>
#include <cstddef>
#include <array>
#include <stdexcept>

#include "../benchmark_utilities.h"

struct Foo
{
    uint64_t m1;
    uint64_t m2;
    uint64_t m3;
    uint64_t m4;
};


static constexpr std::size_t OBJECT_COUNT = 1024000; // Don't increase further as can cause stack overflow
static constexpr std::size_t ITERATION_COUNT = 100;

template <typename AllocatorType>
inline void run_allocator_benchmark()
{
    AllocatorType allocator;

    Foo* array_foo[OBJECT_COUNT];

    for(std::size_t i =0; i < OBJECT_COUNT; i++)
    {
        array_foo[i] = static_cast<Foo*>(allocator.allocate(sizeof(Foo)));
        if(!array_foo) { throw std::runtime_error("Allocation failed"); }
    }

    PMCUtilities::start_reading_pmc();
    std::size_t result = 0;

    ProcessorUtilities::cache_flush(array_foo);

    BENCHMARK_BEGIN(ITERATION_COUNT)
    //////////////////////////////////////////////////////////////////////////////////////////////////////
    // ACCESS ALL OBJECTS
        for(std::size_t i =0; i<OBJECT_COUNT ;i++)
        {
            array_foo[i]->m1 = i;
            if( i == array_foo[i]->m1 ) result++;
            array_foo[i]->m2 = i+1;
            if( i+1 == array_foo[i]->m2 ) result++;
            array_foo[i]->m3 = i+2;
            if( i+2 == array_foo[i]->m3 ) result++;
            array_foo[i]->m4 = i+3;
            if( i+3 == array_foo[i]->m4 ) result++;
        }

        for(std::size_t i =0; i<OBJECT_COUNT ;i++)
        {
            if( array_foo[i]->m1 + array_foo[i]->m3  > 4 ||  array_foo[i]->m2 + array_foo[i]->m4  > 5 )
            result += array_foo[i]->m1;
            if( array_foo[i]->m1 + array_foo[i]->m3  > 4 ||  array_foo[i]->m2 + array_foo[i]->m4  > 5 )
            result += array_foo[i]->m2;
            if( array_foo[i]->m1 + array_foo[i]->m3  > 4 ||  array_foo[i]->m2 + array_foo[i]->m4  > 5 )
            result += array_foo[i]->m3;
            if( array_foo[i]->m1 + array_foo[i]->m3  > 4 ||  array_foo[i]->m2 + array_foo[i]->m4  > 5 )
            result += array_foo[i]->m4;
        }
    //////////////////////////////////////////////////////////////////////////////////////////////////////
    BENCHMARK_END()

    PMCUtilities::stop_reading_pmc();

    std::cout << "result = " << result << std::endl;

    for(std::size_t i =0; i < OBJECT_COUNT; i++)
    {
        allocator.deallocate(array_foo[i]);
    }

    report.print(allocator.title());

    // perf stat -e LLC-load-misses actually shows combination of read and write
    std::cout << "LLC misses (read): " << PMCUtilities::get_pmc_values().cache_misses_count_read << std::endl;
    std::cout << "LLC misses (write): " << PMCUtilities::get_pmc_values().cache_misses_count_write << std::endl;
}


#endif