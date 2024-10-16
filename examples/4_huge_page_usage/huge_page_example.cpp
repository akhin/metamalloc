/*
    - To work with huge pages , you may need to configure your system :

        - Linux : /proc/meminfo should have non-zero "Hugepagesize" & "HugePages_Total/HugePages_Free" attributes
                  ( If HugePages_Total or HugePages_Free  is 0
                  then run "echo 20 | sudo tee /proc/sys/vm/nr_hugepages" ( Allocates 20 x 2MB huge pages )
                  Reference : https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt )

                  ( If THP is enabled , we will use madvise. Otherwise we will use HUGE_TLB flag for mmap.
                  To check if THP enabled : cat /sys/kernel/mm/transparent_hugepage/enabled
                  To disable THP :  echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
                  )

        - Windows : SeLockMemoryPrivilege is required.
                    It can be acquired using gpedit.msc :
                    Local Computer Policy -> Computer Configuration -> Windows Settings -> Security Settings -> Local Policies -> User Rights Managements -> Lock pages in memory
*/
#include "../../metamalloc.h"
#include "../simple_heap_pow2.h"
using namespace metamalloc;

#include <iostream>
using namespace std;


int main()
{
    if (VirtualMemory::is_huge_page_available() == false)
    {
        #ifdef __linux__
        std::cout << "Huge page not available. Try to run \"echo 20 | sudo tee /proc/sys/vm/nr_hugepages\" ( Allocates 20 x 2mb huge pages ) \n";
        #else
        std::cout << "Huge page not available. You need to enable it using gpedit.msc\n";
        #endif
        return -1;
    }

    auto min_huge_page_size = VirtualMemory::get_minimum_huge_page_size();
    std::cout << "minimum huge page size = " <<  min_huge_page_size << " bytes" << std::endl;

    using HugePageArenaType = Arena<LockPolicy::NO_LOCK, VirtualMemoryPolicy::HUGE_PAGE>;
    HugePageArenaType arena;

    bool success = arena.create(min_huge_page_size*10, min_huge_page_size); // Size and alignment

    if(!success) {std::cout << "arena creation failed\n"; return -1;};

    using HeapType = SimpleHeapPow2<
                    ConcurrencyPolicy::SINGLE_THREAD,
                    HugePageArenaType>;

    HeapType allocator;

    HeapType::HeapCreationParams params;
    params.m_logical_page_size = min_huge_page_size;

    success = allocator.create(params, &arena);

    if(!success) {std::cout << "allocator creation failed\n"; return -1;};

    void * ptr = allocator.allocate(42);
    if(!ptr) {std::cout << "allocation failed\n"; return -1;};
    allocator.deallocate(ptr);

    void * ptr2 = allocator.allocate_aligned(512, AlignmentConstants::SIMD_AVX512_WIDTH);
    if(!ptr2) {std::cout << "allocation failed\n"; return -1;};
    allocator.deallocate(ptr2);

    return 0;
}