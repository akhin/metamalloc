/*
    USAGE :

        #include "unit_test.h"

        ...

        UnitTest unit_test;

        unit_test.test_equals(ACTUAL, EXPECTED, "test category", "test case");

        ...

        cout << unit_test.get_summary_report("Tests for X");

        return unit_test.did_all_pass();

    OTHERS :

        ConcurrencyTestUtilities                        sleep_randomly_usecs  pin_calling_thread_randomly thread_safe_print set_calling_thread_name
        RandomNumberGenerator::get_random_integer       use it to get random integers
        Console::console_output_with_colour             gives coloured output in consoles on Linux and Windows

*/
#ifndef _UNIT_TEST_H_
#define _UNIT_TEST_H_

#include <cstdlib>
#include <cmath>
#include <limits>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <sstream>
#include <random>
#include <mutex>
#include <type_traits>
#include <iostream>

#if __linux__
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#elif _WIN32
#include <windows.h>
#include <chrono>
#include <thread>
#endif

#define UNIT_TEST // Useful to distinguish unit-test-only code pieces in actual sources

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
// ConcurrencyTestUtilities
class ConcurrencyTestUtilities
{
    public:

        static void sleep_randomly_usecs(int max_duration_in_microsecs=0)
        {
            unsigned long microseconds = static_cast<unsigned long>(RandomNumberGenerator::get_random_integer(max_duration_in_microsecs));
            #ifdef __linux__
            usleep(microseconds);
            #elif _WIN32
            std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
            #endif
        }

        static int pin_calling_thread_randomly(int max_core_id)
        {
            int core_id = RandomNumberGenerator::get_random_integer(max_core_id);
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

        static void thread_safe_print(const std::string_view& buffer, ConsoleColour foreground_colour = ConsoleColour::FG_YELLOW)
        {
            print_lock.lock();
            Console::console_output_with_colour(foreground_colour, buffer);
            print_lock.unlock();
        }

        static void set_calling_thread_name(const std::string_view name)
        {
            unsigned long thread_id{ 0 };
            auto name_length = name.length();

            if (name_length > 0 && name_length <= MAX_THREAD_NAME_LENGTH)
            {
                #ifdef __linux__
                thread_id = pthread_self();
                pthread_setname_np(thread_id, name.data());
                #elif _WIN32
                thread_id = ::GetCurrentThreadId();
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

    private:

        static std::mutex print_lock;
        static constexpr inline int MAX_THREAD_NAME_LENGTH = 16; // Limitation comes from Linux
};


///////////////////////////////////////////////////////////////////
// UnitTest
class UnitTest
{
    public:

        struct UnitTestResult
        {
            std::string test_category = "";
            std::string test_case = "";
            std::string actual_text = "";
            std::string expected_text = "";
            bool success = false;

            const std::string get_as_text() const
            {
                std::stringstream str;
                str << "Category=" << test_category << " , Case=" << test_case << " , Actual=" << actual_text << " , Expected=" << expected_text;
                return str.str();
            }
        };

        template <class T, class U>
        bool test_equals(T actual, U expected, const std::string& test_category, const std::string& test_case)
        {
            bool evaluation = false;

            if constexpr (std::is_same_v<T, double>)
            {
                evaluation = are_doubles_same(actual, expected);
            }
            else
            {
                evaluation = (actual == static_cast<T>(expected));
            }

            Console::console_output_with_colour(ConsoleColour::FG_YELLOW, "RUNNING: Category = " + test_category + " , Test case = " + test_case + '\n');

            std::stringstream stream_actual;
            stream_actual << actual;
            std::stringstream stream_expected;
            stream_expected << expected;
            m_results.push_back({ test_category, test_case, stream_actual.str(), stream_expected.str(), evaluation });

            std::stringstream test_case_display;
            test_case_display << "actual=" << actual << " expected=" << expected;

            if (evaluation)
            {
                Console::console_output_with_colour(ConsoleColour::FG_GREEN, "SUCCESS : " + test_case_display.str() + '\n');
            }
            else
            {
                Console::console_output_with_colour(ConsoleColour::FG_RED, "FAILURE : " + test_case_display.str() + '\n');
            }

            return evaluation;
        }

        void reset()
        {
            m_results.clear();
        }

        bool did_all_pass() const
        {
            for (const auto& iter : m_results)
            {
                if (iter.success == false)
                {
                    return false;
                }
            }
            return true;
        }

        const std::string get_summary_report(const std::string& report_name) const
        {
            std::stringstream result;

            // FIND OUT THE FAILED ONES
            std::vector<UnitTestResult> failed_test_cases;

            for (const auto& iter : m_results)
            {
                if (iter.success == false)
                {
                    failed_test_cases.push_back(iter);
                }
            }

            // BUILD THE REPORT
            auto test_case_number = m_results.size();
            auto failure_number = failed_test_cases.size();

            result << report_name << " > Total test case number : " << test_case_number << " , Failed test case number : " << failure_number;
            result << std::endl << std::endl;

            if (failure_number > 0)
            {
                result << "Failed test cases :" << std::endl ;
                auto counter = 1;

                for (const auto& iter : failed_test_cases)
                {
                    result << counter << ". " << iter.get_as_text() << std::endl;
                    counter++;
                }
            }

            return result.str();
        }

    private:
        std::vector<UnitTestResult> m_results ={};

        bool are_doubles_same (double a, double b)
        {
            return std::fabs(a - b) < std::numeric_limits<double>::epsilon();
        }
};

template <>
bool UnitTest::test_equals(const char* actual, const char* expected, const std::string& test_category, const std::string& test_case)
{
    bool evaluation = false;

    if (actual && expected)
    {
        evaluation = strcmp(actual, expected) == 0;
    }

    std::stringstream stream_actual;

    if (actual)
    {
        stream_actual << actual;
    }

    std::stringstream stream_expected;

    if (expected)
    {
        stream_expected << expected;
    }

    m_results.push_back({ test_category, test_case, stream_actual.str(), stream_expected.str(), evaluation });

    std::stringstream test_case_display;
    test_case_display << "actual=";

    if (actual)
    {
        test_case_display << " " << actual;
    }

    test_case_display << " expected=";

    if (expected)
    {
        test_case_display << " " << expected;
    }

    if (evaluation)
    {
        Console::console_output_with_colour(ConsoleColour::FG_GREEN, "SUCCESS : " + test_case_display.str() + '\n');
    }
    else
    {
        Console::console_output_with_colour(ConsoleColour::FG_RED, "FAILURE : " + test_case_display.str() + '\n');
    }

    return evaluation;
}

#endif