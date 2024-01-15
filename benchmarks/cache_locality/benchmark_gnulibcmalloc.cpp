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
    if(geteuid() != 0)
    {
        std::cout << "You need to run this benchmark with sudo as we are accessing PMC to measure cache misses." << std::endl;
        return -1;
    }
    run_allocator_benchmark<StandardMallocAllocator>();
    return 0;
}