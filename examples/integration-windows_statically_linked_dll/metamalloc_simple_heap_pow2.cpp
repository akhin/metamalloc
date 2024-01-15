/*
    NOTE THAT DOESN'T COVER ALL FUNCTIONS. YOU WILL NEED TO ADD MISSING REDIRECTIONS.

    LIST OF FUNCTIONS THAT ARE REPLACED :

                                    malloc
                                    free
                                    realloc
                                    calloc
                                    _aligned_malloc        https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc?view=msvc-170
                                    _aligned_free        https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-free?view=msvc-170
                                    _msize                https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/msize?view=msvc-170

                                    void* operator new(std::size_t size, std::align_val_t alignment)
                                    void operator delete(void* ptr, std::align_val_t alignment) noexcept
                                    void* operator new[](std::size_t size, std::align_val_t alignment);
                                    void operator delete[](void* ptr, std::align_val_t alignment) noexcept;

                                    void* operator new(std::size_t size, const std::nothrow_t&) noexcept;
                                    void operator delete(void* ptr, const std::nothrow_t&) noexcept;
                                    void* operator new[](std::size_t size, const std::nothrow_t&) noexcept;
                                    void operator delete[](void* ptr, const std::nothrow_t&) noexcept;

                                    void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t& tag) noexcept
                                    void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t& tag) noexcept

                                    void operator delete(void* p, std::size_t size) noexcept
                                    void operator delete[](void* p, std::size_t size) noexcept

                                    void operator delete(void* p, std::size_t size, std::align_val_t align)
                                    void operator delete[](void* p, std::size_t size, std::align_val_t align)

*/

#include <cstddef>
#include <exception>
#include <cstdio>
#include <metamalloc.h>
#include <simple_heap_pow2.h>

using namespace metamalloc;

#include "metamalloc_simple_heap_pow2.h"

#define TARGET_CRT_DLL "ucrtbase.dll" // CHANGE DLL NAME ACCORDINGLY FOR DIFFERENT CRT VERSIONS

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ALLOCATOR TYPE
using CentralHeapType = SimpleHeapPow2<ConcurrencyPolicy::CENTRAL>;
using LocalHeapType = SimpleHeapPow2<ConcurrencyPolicy::THREAD_LOCAL>;

using ScalableAllocatorType = ScalableAllocator<
    CentralHeapType,
    LocalHeapType
>;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TRAMPOLINES FOR REDIRECTION
struct TrampolinePatch
{
    char name[32] = {(char)0};
    void* original_address = nullptr;
    void* replacement_addres = nullptr;
    Trampoline::Bytes original_bytes;
};

static constexpr std::size_t TRAMPOLINE_COUNT = 7;
TrampolinePatch trampoline_patches[TRAMPOLINE_COUNT] = {
                                            {"malloc",                               nullptr,    metamalloc_malloc,             {0}},
                                            {"free",                             nullptr,      metamalloc_free,             {0}},
                                            {"calloc",                               nullptr,    metamalloc_calloc,             {0}},
                                            {"realloc",                         nullptr,     metamalloc_realloc,         {0}},
                                            {"_aligned_malloc",                 nullptr,    metamalloc_aligned_malloc,     {0}},
                                            {"_aligned_free",                     nullptr,    metamalloc_aligned_free,     {0}},
                                            {"_msize",                            nullptr,    metamalloc_usable_size,        {0}},
                                        };

bool install_trampolines()
{
    HMODULE crt_library = GetModuleHandle(TARGET_CRT_DLL);

    if(crt_library == nullptr)
    {
        return false;
    }

    for(std::size_t i=0; i<TRAMPOLINE_COUNT; i++)
    {
        trampoline_patches[i].original_address = GetProcAddress(crt_library, trampoline_patches[i].name);

        if( trampoline_patches[i].original_address == nullptr)
        {
            //fprintf(stderr, "failed to find %s\n", trampoline_patches[i].name);
            return false;
        }

        bool success = Trampoline::install(trampoline_patches[i].original_address, trampoline_patches[i].replacement_addres, trampoline_patches[i].original_bytes);

        if(success == false )
        {
            return false;
        }
    }

    return true;
}

void uninstall_trampolines()
{
    for(std::size_t i=0; i<TRAMPOLINE_COUNT; i++)
    {
        Trampoline::uninstall(trampoline_patches[i].original_address, trampoline_patches[i].original_bytes);
    }
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool initialised = false;

void initialise()
{
    /////////////////////////////////////////////////////////////////////////////////////////////
    std::size_t default_arena_capacity = 268435456;

    std::size_t ARENA_CAPACITY = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_arena_capacity", default_arena_capacity);
    std::size_t thread_local_heap_cache_count = EnvironmentVariable::get_variable("metamalloc_thread_local_heap_cache_count", 8);

    CentralHeapType::HeapCreationParams params_central;
    params_central.m_small_object_logical_page_size         = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_small_object_logical_page_size", 65536);
    params_central.m_big_object_logical_page_size           = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_big_object_logical_page_size", 655360);    // THIS SHOULD BE 16 BYTES MORE THAN THE LARGEST EXPECTED ALLOCATION SIZE
    params_central.m_segment_grow_coefficient              = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_grow_coefficient", 0.0);
    params_central.m_small_object_page_recycling_threshold= EnvironmentVariable::get_variable("metamalloc_simple_heappow2_small_object_page_recycling_threshold", 1000);
    params_central.m_big_object_page_recycling_threshold = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_big_object_page_recycling_threshold", 1000);

    LocalHeapType::HeapCreationParams params_local;
    params_local.m_small_object_logical_page_size            = params_central.m_small_object_logical_page_size;
    params_local.m_big_object_logical_page_size           = params_central.m_big_object_logical_page_size;
    params_local.m_small_object_page_recycling_threshold  = params_central.m_small_object_page_recycling_threshold;
    params_local.m_segment_deallocation_queue_initial_capacity      = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_deallocation_queue_initial_capacity", 3276800);
    params_local.m_big_object_page_recycling_threshold = params_central.m_big_object_page_recycling_threshold;

    EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(params_central.m_small_object_bin_page_counts, EnvironmentVariable::get_variable("metamalloc_simple_heappow2_central_page_counts", "1,1,1,1,1,1,1,1"));
    EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(params_local.m_small_object_bin_page_counts, EnvironmentVariable::get_variable("metamalloc_simple_heappow2_local_page_counts", "50,50,50,50,50,50,50,50"));

    trace_integer_value("metamalloc_simple_heappow2_arena_capacity", ARENA_CAPACITY);
    trace_integer_value("metamalloc_thread_local_heap_cache_count", thread_local_heap_cache_count);
    trace_string_value("metamalloc_simple_heappow2_central_page_counts", EnvironmentVariable::get_variable("metamalloc_simple_heappow2_central_page_counts", "1,1,1,1,1,1,1,1"));
    trace_string_value("metamalloc_simple_heappow2_local_page_counts", EnvironmentVariable::get_variable("metamalloc_simple_heappow2_local_page_counts", "50,50,50,50,50,50,50,50"));
    trace_integer_value("metamalloc_simple_heappow2_small_object_logical_page_size", params_central.m_small_object_logical_page_size);
    trace_integer_value("metamalloc_simple_heappow2_big_object_logical_page_size", params_central.m_big_object_logical_page_size);
    trace_double_value("metamalloc_simple_heappow2_grow_coefficient", params_local.m_segment_grow_coefficient);
    trace_integer_value("metamalloc_simple_heappow2_deallocation_queue_initial_capacity", params_local.m_segment_deallocation_queue_initial_capacity);
    trace_integer_value("metamalloc_simple_heappow2_small_object_page_recycling_threshold", params_local.m_small_object_page_recycling_threshold);
    trace_integer_value("metamalloc_simple_heappow2_big_object_page_recycling_threshold", params_local.m_big_object_page_recycling_threshold);
    /////////////////////////////////////////////////////////////////////////////////////////////

    if ( install_trampolines() == false )
    {
        fprintf(stderr, "Metamalloc trampoline installation failed\n");
        std::terminate();
    }

    ScalableAllocatorType::get_instance().set_thread_local_heap_cache_count(thread_local_heap_cache_count);
    initialised = ScalableAllocatorType::get_instance().create(params_central, params_local, ARENA_CAPACITY);

    if(initialised) { trace_message("initialised successfully"); }
    else
    {
        fprintf(stderr, "Metamalloc initialisation failed\n");
        std::terminate();
    }

}

extern "C"
{
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DLL_FUNCTION void *metamalloc_malloc(std::size_t size)
{
    return ScalableAllocatorType::get_instance().allocate(size);
}

DLL_FUNCTION void* metamalloc_aligned_malloc(std::size_t size, std::size_t alignment)
{
    return ScalableAllocatorType::get_instance().allocate_aligned(size, alignment);
}

DLL_FUNCTION void metamalloc_free(void* ptr)
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

DLL_FUNCTION void metamalloc_aligned_free(void* ptr)
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

DLL_FUNCTION void *metamalloc_calloc(std::size_t num, std::size_t size)
{
    return  ScalableAllocatorType::get_instance().allocate_and_zero_memory(num, size);
}

DLL_FUNCTION void *metamalloc_realloc(void *ptr, std::size_t size)
{
    return  ScalableAllocatorType::get_instance().reallocate(ptr, size);
}

DLL_FUNCTION void *metamalloc_operator_new(std::size_t size)
{
    return ScalableAllocatorType::get_instance().operator_new(size);
}

DLL_FUNCTION void *metamalloc_operator_new_aligned(std::size_t size, std::size_t alignment)
{
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, alignment);
}

DLL_FUNCTION std::size_t metamalloc_usable_size(void* ptr)
{
    return ScalableAllocatorType::get_instance().get_usable_size(ptr);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            trace_message("DLL_PROCESS_ATTACH EVENT");
            initialise();

            break;

        case DLL_PROCESS_DETACH:
            trace_message("DLL_PROCESS_DETACH EVENT");
            uninstall_trampolines();

            break;

        case DLL_THREAD_ATTACH:
            trace_message("DLL_THREAD_ATTACH EVENT");
            break;

        case DLL_THREAD_DETACH:
            trace_message("DLL_THREAD_DETACH EVENT");
            break;
    }
    return TRUE; // Return TRUE on success.
}

#pragma comment(linker, "/EXPORT:malloc=metamalloc_malloc")
#pragma comment(linker, "/EXPORT:free=metamalloc_free")
#pragma comment(linker, "/EXPORT:calloc=metamalloc_calloc")
#pragma comment(linker, "/EXPORT:realloc=metamalloc_realloc")
#pragma comment(linker, "/EXPORT:_aligned_malloc=metamalloc_aligned_malloc")
#pragma comment(linker, "/EXPORT:_aligned_free=metamalloc_aligned_free")
#pragma comment(linker, "/EXPORT:_msize=metamalloc_usable_size")
#pragma comment(linker, "/EXPORT:metamalloc_operator_new")
#pragma comment(linker, "/EXPORT:metamalloc_operator_new_aligned")