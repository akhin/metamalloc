/*
    - To work with 2MB huge pages on Linux and  2MB or 1 GB huge pages on Windows , you may need to configure your system :

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


    - There is NUMA functionality however it is only experimental and not compiled by default. To try NUMA :

            You need : #define ENABLE_NUMA

            Also if on Linux , you need libnuma ( For ex : on Ubuntu -> sudo apt install libnuma-dev ) and -lnuma for GCC
*/

#ifndef __VIRTUAL_MEMORY_H__
#define __VIRTUAL_MEMORY_H__

//#define ENABLE_NUMA // VOLTRON_EXCLUDE

#include <cstddef>

#ifdef __linux__ // VOLTRON_EXCLUDE
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <sys/personality.h>
#ifdef ENABLE_NUMA // VOLTRON_EXCLUDE
#include <numa.h>
#include <numaif.h>
#include <sched.h>
#endif // VOLTRON_EXCLUDE
#endif // VOLTRON_EXCLUDE

#ifdef _WIN32
#include <windows.h>
#pragma warning(disable:6250)
#endif

#include "../compiler/builtin_functions.h"

class VirtualMemory
{
    public:

        constexpr static std::size_t NO_NUMA = -1;

        #ifdef __linux__
        constexpr static std::size_t PAGE_ALLOCATION_GRANULARITY = 4096;    // In bytes
        #elif _WIN32
        constexpr static std::size_t PAGE_ALLOCATION_GRANULARITY = 65536;    // In bytes , https://devblogs.microsoft.com/oldnewthing/20031008-00/?p=42223
        #endif

        static std::size_t get_page_size()
        {
            std::size_t ret{ 0 };

            #ifdef __linux__
            ret = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));        // TYPICALLY 4096, 2^ 12
            #elif _WIN32
            // https://learn.microsoft.com/en-gb/windows/win32/api/sysinfoapi/ns-sysinfoapi-system_info
            SYSTEM_INFO system_info;
            GetSystemInfo(&system_info);
            ret = system_info.dwPageSize; // TYPICALLY 4096, 2^ 12
            #endif
            return ret;
        }

        static bool is_huge_page_available()
        {
            bool ret{ false };
            #ifdef __linux__
            if (get_minimum_huge_page_size() <= 0)
            {
                ret = false;
            }
            else
            {
                if ( get_huge_page_total_count_2mb() > 0 )
                {
                    ret = true;
                }
            }
            #elif _WIN32
            auto huge_page_size = get_minimum_huge_page_size();

            if (huge_page_size)
            {
                HANDLE token = 0;
                OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);

                if (token)
                {
                    LUID luid;

                    if (LookupPrivilegeValue(0, SE_LOCK_MEMORY_NAME, &luid))
                    {
                        TOKEN_PRIVILEGES token_privileges;
                        memset(&token_privileges, 0, sizeof(token_privileges));
                        token_privileges.PrivilegeCount = 1;
                        token_privileges.Privileges[0].Luid = luid;
                        token_privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

                        if (AdjustTokenPrivileges(token, FALSE, &token_privileges, 0, 0, 0))
                        {
                            auto last_error = GetLastError();

                            if (last_error  == ERROR_SUCCESS)
                            {
                                ret = true;
                            }
                        }
                    }
                }
            }

            #endif
            return ret;
        }

        static std::size_t get_minimum_huge_page_size()
        {
            std::size_t ret{ 0 };
            #ifdef __linux__
            ret = get_proc_mem_info("Hugepagesize", 13) * 1024; // It is in KBs
            #elif _WIN32
            ret = static_cast<std::size_t>(GetLargePageMinimum());
            #endif
            return ret;
        }

        #ifdef __linux__
        // Equivalent of /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
        static std::size_t get_huge_page_total_count_2mb()
        {
            auto ret = get_proc_mem_info("HugePages_Total", 16);
            if(ret == 0 )
            {
                ret = get_proc_mem_info("HugePages_Free", 15);
            }
            return ret;
        }

        static std::size_t get_proc_mem_info(const char* attribute, std::size_t attribute_len)
        {
            // Using syscalls to avoid memory allocations
            std::size_t ret = 0;
            const char* mem_info_file = "/proc/meminfo";

            int fd = open(mem_info_file, O_RDONLY);
            if (fd < 0) {
            return ret;
            }

            char buffer[256];
            size_t read_bytes;

            while ((read_bytes = read(fd, buffer, sizeof(buffer))) > 0)
            {
            char* pos = strstr(buffer, attribute);

            if (pos != nullptr)
            {
                ret = std::strtoul(pos + attribute_len, nullptr, 10);
                break;
            }
            }

            close(fd);

            return ret;
        }

        // THP stands for "transparent huge page". A Linux mechanism
        // It affects how we handle allocation of huge pages on Linux
        static bool is_thp_enabled()
        {
            const char* thp_enabled_file = "/sys/kernel/mm/transparent_hugepage/enabled";
            
            if (access(thp_enabled_file, F_OK) != 0)
            {
                return false;
            }

            int fd = open(thp_enabled_file, O_RDONLY);
            if (fd < 0)
            {
                return false;
            }

            char buffer[256] = {0};
            ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
            close(fd);

            if (bytes_read <= 0)
            {
                return false;
            }

            if (strstr(buffer, "[always]") != nullptr || strstr(buffer, "[madvise]") != nullptr)
            {
                return true;
            }

            return false;
        }
        #endif

        #ifdef ENABLE_NUMA
        static std::size_t get_numa_node_count()
        {
            std::size_t ret{ 0 };

            #ifdef __linux__
            // Requires -lnuma
            ret = static_cast<std::size_t>(numa_num_configured_nodes());
            #elif _WIN32
            // GetNumaHighestNodeNumber is not guaranteed to be equal to NUMA node count so we need to iterate backwards
            ULONG current_numa_node = 0;
            GetNumaHighestNodeNumber(&current_numa_node);

            while (current_numa_node > 0)
            {
                GROUP_AFFINITY affinity;
                if ((GetNumaNodeProcessorMaskEx)(static_cast<USHORT>(current_numa_node), &affinity))
                {
                    //If the specified node has no processors configured, the Mask member is zero
                    if (affinity.Mask != 0)
                    {
                        ret++;
                    }
                }
                // max node was invalid or had no processor assigned, try again
                current_numa_node--;
            }
            #endif

            return ret;
        }

        static std::size_t get_numa_node_of_caller()
        {
            std::size_t numa_node = -1;
            #ifdef __linux__
            // Requires -lnuma
            numa_node = static_cast<std::size_t>(numa_node_of_cpu(sched_getcpu()));
            #elif _WIN32
            USHORT node_number{ 0 };
            PROCESSOR_NUMBER processor_number{ 0 };
            GetCurrentProcessorNumberEx(&processor_number);
            if (GetNumaProcessorNodeEx(&processor_number, &node_number))
            {
                numa_node = static_cast<std::size_t>(node_number);
            }
            #endif
            return numa_node;
        }
        #endif

        // Note about alignments : Windows always returns page ( typically 4KB ) or huge page ( typially 2MB ) aligned addresses
        //                           On Linux , page sized ( again 4KB) allocations are aligned to 4KB, but the same does not apply to huge page allocations : They are aligned to 4KB but never to 2MB
        //                           Therefore in case of huge page use, there is no guarantee that the allocated address will be huge-page-aligned , so alignment requirements have to be handled by the caller
        //
        // Note about huge page failures : If huge page allocation fails, for the time being not doing a fallback for a subsequent non huge page allocation
        //                                    So library users have to check return values
        //
        template <bool use_hugepage, std::size_t numa_node=NO_NUMA, bool zero_buffer=false>
        static void* allocate(std::size_t size, void* hint_address = nullptr)
        {
            void* ret = nullptr;
            #ifdef __linux__
            static bool thp_enabled = is_thp_enabled();
            // MAP_ANONYMOUS rather than going thru a file (memory mapped file)
            // MAP_PRIVATE rather than shared memory
            int flags = MAP_PRIVATE | MAP_ANONYMOUS;

            #ifndef ENABLE_NUMA
            // MAP_POPULATE forces system to access the  just-allocated memory. That helps by creating TLB entries
            flags |= MAP_POPULATE;
            #endif

            if constexpr (use_hugepage)
            {
                if (!thp_enabled)
                {
                    flags |= MAP_HUGETLB;
                }
            }

            ret = mmap(hint_address, size, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (ret == nullptr || ret == MAP_FAILED)
            {
                return nullptr;
            }

            if constexpr (use_hugepage)
            {
                if (thp_enabled)
                {
                    madvise(ret, size, MADV_HUGEPAGE);
                }
            }

            #ifdef ENABLE_NUMA
            auto numa_node_count = get_numa_node_count();

            if (numa_node_count > 0 && numa_node != static_cast<std::size_t>(-1))
            {
                unsigned long nodemask = 1UL << numa_node;
                int result = mbind(ret, size, MPOL_BIND, &nodemask, sizeof(nodemask), MPOL_MF_MOVE);

                if (result != 0)
                {
                    munmap(ret, size);
                    ret = nullptr;
                }
            }
            #endif

            #elif _WIN32
            int flags = MEM_RESERVE | MEM_COMMIT;

            if constexpr (use_hugepage)
            {
                flags |= MEM_LARGE_PAGES;
            }

            #ifndef ENABLE_NUMA
            ret = VirtualAlloc(hint_address, size, flags, PAGE_READWRITE);
            #else
            auto numa_node_count = get_numa_node_count();

            if (numa_node_count > 0 && numa_node != static_cast<std::size_t>(-1))
            {
                ret = VirtualAllocExNuma(GetCurrentProcess(), hint_address, size, flags, PAGE_READWRITE, static_cast<DWORD>(numa_node));
            }
            else
            {
                ret = VirtualAlloc(hint_address, size, flags, PAGE_READWRITE);
            }
            #endif
            #endif

            if constexpr (zero_buffer)
            {
                builtin_memset(ret, 0, size);
            }

            return ret;
        }

        static bool deallocate(void* address, std::size_t size)
        {
            bool ret{ false };
            #ifdef __linux__
            ret = munmap(address, size) == 0 ? true : false;
            #elif _WIN32
            ret = VirtualFree(address, size, MEM_RELEASE) ? true : false;
            #endif
            return ret;
        }
        
        static bool is_aslr_disabled()
        {
            #ifdef __linux__
            return  personality(ADDR_NO_RANDOMIZE) & ADDR_NO_RANDOMIZE;
            #elif _WIN32
            // No implementation for Windows
            return false;
            #endif
        }
        
        static bool disable_aslr()
        {
            #ifdef __linux__
            const int old_personality = personality(ADDR_NO_RANDOMIZE);
            
            if (!(old_personality & ADDR_NO_RANDOMIZE)) 
            {
                const int new_personality = personality(ADDR_NO_RANDOMIZE);
                
                if (! (new_personality & ADDR_NO_RANDOMIZE))
                {
                    return false;
                }
            }
            #elif _WIN32
            // Linux only
            return true;
            #endif
            return true;
        }

        static bool lock_all_pages()
        {
            bool ret{ false };
            #ifdef __linux__
            ret = mlockall(MCL_CURRENT | MCL_FUTURE) == 0 ? true : false;
            #elif _WIN32
            ret = false; // No equivalent on Windows , you have to use VirtualLock per individual page
            #endif
            return ret;
        }

        // To prevent the system from swapping the pages out to the paging file
        static bool lock(void* address, std::size_t size)
        {
            bool ret{ false };
            #ifdef __linux__
            ret = mlock(address, size) == 0 ? true : false;
            #elif _WIN32
            ret = VirtualLock(address, size) ? true : false;
            #endif
            return ret;
        }

        static bool unlock(void* address, std::size_t size)
        {
            bool ret{ false };
            #ifdef __linux__
            ret = munlock(address, size) == 0 ? true : false;
            #elif _WIN32
            ret = VirtualUnlock(address, size) ? true : false;
            #endif
            return ret;
        }

    private :
};

#endif