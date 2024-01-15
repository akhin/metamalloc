/////////////////////////////////////////////////////////////////
// https://oneapi-src.github.io/oneTBB/main/tbb_userguide/Windows_C_Dynamic_Memory_Interface_Replacement.html
#include <oneapi/tbb/tbbmalloc_proxy.h>
/////////////////////////////////////////////////////////////////
#include "../benchmark.h"

int main (int argc, char* argv[])
{
    run_benchmark(argc, argv);
    return 0;
}