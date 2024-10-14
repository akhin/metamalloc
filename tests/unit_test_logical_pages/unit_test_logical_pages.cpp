#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include <iostream>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <thread>
#include <mutex>
using namespace std;

static UnitTest unit_test;

#include "logical_page_common_tests.h"

int main(int argc, char* argv[])
{
    // LOGICAL PAGE HEADER SIZE
    unit_test.test_equals(sizeof(LogicalPageHeader), 64, "Logical page header size" , "Should be 64 bytes");

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // LOGICAL PAGE TESTS
    {
        // CREATION CHECKS
        test_incorrect_creation<LogicalPage<>>(6);

        std::size_t allocation_size = 128;
        // EXHAUSTION TEST
        test_exhaustion<LogicalPage<>, LogicalPageNode>(allocation_size * 32, 32, allocation_size); // 4KB page , Doesnt use any allocation header,
        test_exhaustion<LogicalPage<>, LogicalPageNode>(allocation_size * 512, 512, allocation_size); //64kb page Doesnt use any allocation header,

        // ERRORS
        test_errors<LogicalPage<>>();

        // GENERAL TESTS
        test_general<LogicalPage<>, LogicalPageNode>(65536);
    }



    //// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("Logical pages");
    std::cout.flush();
    
    #if _WIN32
    bool pause = true;
    if(argc > 1)
    {
        if (std::strcmp(argv[1], "no_pause") == 0)
            pause = false;
    }
    if(pause)
        std::system("pause");
    #endif

    return unit_test.did_all_pass();
}