#include <cstdlib>
#include <iostream>
#include "benchmark.h"

class StandardMallocAllocator
{
    public:
        void* allocate(std::size_t size)
        {
            return malloc(size);
        }

        void deallocate(void* ptr)
        {
            free(ptr);
        }

        const char* title() const{ return "Malloc";}
};

int main ()
{
    run_allocator_benchmark<StandardMallocAllocator>();

    #if _WIN32
    std::system("pause");
    #endif

    return 0;
}