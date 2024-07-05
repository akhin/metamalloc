#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include "../../src/compiler/unused.h"
#include "../../src/deallocation_queue.h"

#include <cstdlib>
#include <atomic>
#include <vector>
#include <thread>
#include <iostream>
using namespace std;

UnitTest unit_test;

class DeallocationQueueStdAllocator
{
public:
    static void* allocate(std::size_t size)
    {
        return std::malloc(size);
    }

    static void deallocate(void* ptr, std::size_t size)
    {
        UNUSED(size);
        std::free(ptr);
    }
};

int main()
{
    static_assert( sizeof(PointerPage) == 65536);

    // SINGLE THREAD  BASIC OPERATIONS SINGLE PAGE
    {
        DeallocationQueue<DeallocationQueueStdAllocator> q;
        if (q.create(1) == false) { std::cout << "deallocation q creation failed !!!\n"; return -1; }

        std::size_t pointer_count = 20000;

        std::vector<uint64_t> pointers;

        for (std::size_t i = 0; i < pointer_count; i++)
        {
            void* ptr = malloc(8);
            auto ptr_value = reinterpret_cast<uint64_t>(ptr);

            pointers.push_back(ptr_value);
            q.push(ptr);
        }

        std::size_t counter = 0;
        while (true)
        {
            auto pointer = q.pop();

            if (pointer == 0)
            {
                break;
            }

            //unit_test.test_equals(reinterpret_cast<uint64_t>(pointer), pointers[pointer_count-1-counter], "deallocation queue", "single threaded push and pop"); // printing each test result takes too much time

            if (reinterpret_cast<uint64_t>(pointer) != pointers[pointer_count - 1 - counter])
            {
                std::cout << "TEST FAILED !!!" << std::endl;
                return -1;
            }

            counter++;
        }

        for (auto& ptr : pointers) { std::free(reinterpret_cast<void*>(ptr)); }
    }

    // CONCURRENCY TESTS
    {
        DeallocationQueue<DeallocationQueueStdAllocator> q;
        if (q.create(65536) == false) { std::cout << "deallocation q creation failed !!!\n"; return -1; }

        constexpr std::size_t producer_thread_count = 128;
        constexpr std::size_t production_count_per_producer_thread = 640;
        std::vector<std::unique_ptr<std::thread>> producer_threads;
        std::thread* consumer_thread;

        std::size_t consumer_result = 0; // Will only be accessed by consumer thread
        std::atomic<bool> producers_finished;
        producers_finished.store(false);

        auto consumer_job = [&]()
        {
            while (true)
            {
                auto pointer = q.pop();

                if (pointer == 0)
                {
                    break;
                }
                consumer_result++;
            }
        };

        auto thread_function = [&](bool is_consumer = false)
        {
            if (is_consumer)
            {
                // consume
                while (true)
                {
                    if (producers_finished.load() == true)
                    {
                        break;
                    }
                    consumer_job();
                }
                consumer_job();
            }
            else
            {
                // produce
                for (std::size_t i = 0; i < production_count_per_producer_thread; i++)
                {
                    auto ptr = malloc(8);
                    q.push(ptr);
                    ConcurrencyTestUtilities::sleep_randomly_usecs(5000);
                }
            }
        };

        // PRODUCER THREADS
        for (std::size_t i{ 0 }; i < producer_thread_count; i++)
        {
            producer_threads.emplace_back(new std::thread(thread_function));
        }

        // CONSUMER THREAD
        consumer_thread = new std::thread(thread_function, true);

        // WAIT TILL PRODUCERS ARE DONE
        for (auto& thread : producer_threads)
        {
            thread->join();
        }

        // LET CONSUMER KNOW THAT PRODUCERS ARE  DONE
        producers_finished.store(true);
        // WAIT FOR CONSUMER THREAD
        consumer_thread->join();

        unit_test.test_equals(consumer_result, producer_thread_count * production_count_per_producer_thread, "deallocation queue", "thread safety");

        delete consumer_thread;
    }

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("deallocation queue");

    #if _WIN32
    std::system("pause");
    #endif

    return unit_test.did_all_pass();
}