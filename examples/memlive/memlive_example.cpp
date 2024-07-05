#include "../../memlive.h"

#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <string>

#if __linux__
#include <unistd.h> // geteuid for sudo check
#endif

using namespace memlive;
using namespace std;

int main()
{
    #if __linux__
    if(geteuid() != 0)
    {
        cout << "You need to run this example with sudo." << endl;
        return -1;
    }
    #endif

    int port = 4242;

    //////////////////////////////////////////////////////////////////////
    // START

    memlive::start("127.0.0.1", port);

    cout << "In your browser , navigate to localhost:" << port << endl;

    //////////////////////////////////////////////////////////////////////
    // SETUP AND RUN THREADS
    std::atomic<bool> is_finishing = false;
    std::size_t thread_wait_in_usecs = 5000;
    std::size_t size_class_count = 20;
    std::size_t allocation_per_size_class = 100;
    std::size_t number_of_threads = 4;
    std::vector<std::unique_ptr<std::thread>> threads;

    auto thread_function = [&]()
    {
        std::vector<uint64_t> pointers;

        while (true)
        {
            if (is_finishing.load() == true) { break; }

            for (std::size_t i = 0; i < size_class_count; i++)
            {
                for (std::size_t j = 0; j < allocation_per_size_class; j++)
                {

                    auto ptr = malloc(pow(2, i));

                    pointers.push_back((uint64_t)ptr);

                    if (is_finishing.load() == true) { break; }

                    memlive::ThreadUtilities::sleep_in_microseconds(thread_wait_in_usecs);
                }
            }

            memlive::ThreadUtilities::sleep_in_microseconds(thread_wait_in_usecs);
        }

        for (auto ptr : pointers)
        {
            free((void*)ptr);
        }
    };

    for (std::size_t i{ 0 }; i < number_of_threads; i++)
    {
        threads.emplace_back(new std::thread(thread_function));
    }

    //////////////////////////////////////////////////////////////////////
    // STOP
    std::string user_input;
    while (true)
    {
        cout << "Press q to quit" << endl;

        cin >> (user_input);

        if (user_input[0] == 'q')
        {
            is_finishing.store(true);
            break;
        }
    }

    for (auto& thread : threads)
    {
        thread->join();
    }

    return 0;
}