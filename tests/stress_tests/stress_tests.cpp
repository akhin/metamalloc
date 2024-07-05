#define UNIT_TEST
//#define ENABLE_STATS
#include <cstdlib>

#include "../../examples/integration-as_a_library/metamalloc_simple_heap_pow2.h"
using namespace metamalloc;
#include "../../benchmarks/benchmark_utilities.h"

#include <cstdint>
#include <cstddef>
#include <vector>
#include <thread>
#include <memory>
#include <sstream>
#include <iomanip>

struct Allocation
{
    void* ptr = nullptr;
    bool allocated = false;
    bool deallocated = false;
};

//////////////////////////////////////////////////
// TEST VARIABLES
constexpr std::size_t ITERATION_COUNT = 5;
constexpr std::size_t THREAD_COUNT = 24;
constexpr std::size_t ALLOCATION_COUNT = 4096;
constexpr std::size_t THREAD_RANDOM_SLEEPS_IN_USECS = 100; // To simulate real environments
//////////////////////////////////////////////////
void run_multithreaded_benchmark();
bool validate_buffer(void* buffer, std::size_t buffer_size);
void sleep_randomly_usecs(int max_duration_in_microsecs);

#define ENABLE_ERROR_CHECKING

int main()
{
    metamalloc_initialise();

    run_multithreaded_benchmark();

    #if _WIN32
    system("pause");
    #endif

    return 0;
}

void run_multithreaded_benchmark()
{
    BENCHMARK_BEGIN(ITERATION_COUNT)
    {
        //////////////////////////////////////////////////////////////////
        // We are in a loop , so we need to initialise buckets
        using AllocationBucket = std::vector<Allocation>;
        std::vector<AllocationBucket> allocation_buckets;
        for (auto& bucket : allocation_buckets)
        {
            bucket.clear();
        }

        allocation_buckets.clear();
        allocation_buckets.reserve(THREAD_COUNT);
        for (std::size_t i = 0; i < THREAD_COUNT; i++)
        {
            AllocationBucket bucket;
            bucket.reserve(ALLOCATION_COUNT);
            for (std::size_t j = 0; j < ALLOCATION_COUNT; j++)
            {
                Allocation allocation;
                bucket.push_back(allocation);
            }
            allocation_buckets.push_back(bucket);
        }

        auto thread_function = [&](std::size_t allocation_bucket_index, std::size_t deallocation_bucket_index)
        {
            // ALLOCATIONS
            for (std::size_t i = 0; i < ALLOCATION_COUNT; i++)
            {
                void* ptr = nullptr;
                std::size_t allocation_size = i;
                ptr = malloc(allocation_size);

                #ifdef ENABLE_ERROR_CHECKING
                if (!validate_buffer(ptr, allocation_size)) {  fprintf(stderr, "ALLOCATION FAILED !!!\n"); }
                #endif

                sleep_randomly_usecs(THREAD_RANDOM_SLEEPS_IN_USECS);

                allocation_buckets[allocation_bucket_index][i].ptr = ptr;
                allocation_buckets[allocation_bucket_index][i].allocated = true;
            }
            // DEALLOCATIONS
            auto remaining_deallocations = ALLOCATION_COUNT;

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

        std::vector<std::unique_ptr<std::thread>> threads;
        threads.reserve(THREAD_COUNT);

        for (std::size_t i{ 0 }; i < THREAD_COUNT; i++)
        {
            threads.emplace_back(new std::thread(thread_function, i, THREAD_COUNT - 1 - i));
        }

        for (auto& thread : threads)
        {
            thread->join();
        }

        #ifdef ENABLE_ERROR_CHECKING
        for (auto& bucket : allocation_buckets)
        {
            for (auto& allocation : bucket)
            {
                if (allocation.allocated == false || allocation.deallocated == false) { fprintf(stderr, "TEST FAILED !!!\n"); }
            }
        }
        #endif
        //////////////////////////////////////////////////////////////////
    }
    BENCHMARK_END()
    report.print("title");
}

bool validate_buffer(void* buffer, std::size_t buffer_size)
{
    if (buffer == nullptr)
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

void sleep_randomly_usecs(int max_duration_in_microsecs )
{
    unsigned long microseconds = static_cast<unsigned long>(RandomNumberGenerator::get_random_integer(max_duration_in_microsecs));
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