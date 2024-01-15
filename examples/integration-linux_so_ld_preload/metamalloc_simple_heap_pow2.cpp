/*

    NOTE THAT DOESN'T COVER ALL FUNCTIONS. YOU WILL NEED TO ADD MISSING REDIRECTIONS.

    LIST OF FUNCTIONS THAT ARE REPLACED :

                                    malloc                https://linux.die.net/man/3/malloc
                                    free                https://linux.die.net/man/3/free
                                    realloc                https://linux.die.net/man/3/realloc
                                    calloc                https://linux.die.net/man/3/calloc

                                    aligned_alloc        https://linux.die.net/man/3/aligned_alloc
                                    malloc_usable_size    https://linux.die.net/man/3/malloc_usable_size

                                    64bit GNU LibC operator new variants :

                                        void* _Znwm(std::size_t size)
                                        void* _Znam(std::size_t size)
                                        void* _Znwmm(std::size_t size, std::size_t alignment)
                                        void* _Znamm(std::size_t size, std::size_t alignment)
                                        void* _ZnwmSt11align_val_t(std::size_t size, std::align_val_t alignment)
                                        void* _ZnamSt11align_val_t(std::size_t size, std::align_val_t alignment)
                                        void* _ZnwmRKSt9nothrow_t(std::size_t size, std::nothrow_t t)
                                        void* _ZnamRKSt9nothrow_t(std::size_t size, std::nothrow_t t)
                                        void* _ZnwmSt11align_val_tRKSt9nothrow_t(std::size_t size, std::align_val_t alignment, std::nothrow_t t)
                                        void* _ZnamSt11align_val_tRKSt9nothrow_t(std::size_t size, std::align_val_t alignment, std::nothrow_t t)

                                    64bit GNU LibC operator delete variants :

                                        void _ZdlPv(void* ptr)
                                        void _ZdaPv(void* ptr)
                                        void _ZdlPvm(void* ptr, std::size_t size) noexcept
                                        void _ZdlPvSt11nothrow_t(void* ptr, std::nothrow_t t) noexcept
                                        void _ZdlPvmSt11nothrow_t(void* ptr, std::size_t size, std::nothrow_t t) noexcept
                                        void _ZdaPvm(void* ptr, std::size_t size) noexcept
                                        void _ZdaPvSt11nothrow_t(void* ptr, std::nothrow_t) noexcept
                                        void _ZdaPvmSt11nothrow_t(void* ptr, std::size_t size, std::nothrow_t) noexcept
                                        void _ZdlPvRKSt9nothrow_t(void* ptr, std::nothrow_t t) noexcept
                                        void _ZdaPvRKSt9nothrow_t(void* ptr, std::nothrow_t t) noexcept

*/
#include <cstddef>
#include <cstdarg>
#include <exception>
#include <cstdio>
#include <cstring>
#include <new>

//#define ENABLE_TRACER
#include <metamalloc.h>
#include <simple_heap_pow2.h>
using namespace metamalloc;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ALLOCATOR TYPE
using CentralHeapType = SimpleHeapPow2<ConcurrencyPolicy::CENTRAL>;
using LocalHeapType = SimpleHeapPow2<ConcurrencyPolicy::THREAD_LOCAL>;

using ScalableAllocatorType = ScalableAllocator<
    CentralHeapType,
    LocalHeapType
>;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SHARED OBJECT INITIALISATION
void initialise_shared_object() __attribute__((constructor));
void uninit_shared_object() __attribute__((destructor));

static bool shared_object_loaded = false;
static UserspaceSpinlock<> initialisatin_lock;

void initialise_shared_object()
{
    if(shared_object_loaded==true) return;

    initialisatin_lock.lock();
    // double checking

    if(shared_object_loaded==true) { initialisatin_lock.unlock(); return; }

    trace_message( "shared object loading...");
    /////////////////////////////////////////////////////////////////////////////////////////////
    std::size_t default_arena_capacity = 149946368;

    std::size_t ARENA_CAPACITY = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_arena_capacity", default_arena_capacity);
    std::size_t thread_local_heap_cache_count = EnvironmentVariable::get_variable("metamalloc_thread_local_heap_cache_count", 4);

    CentralHeapType::HeapCreationParams params_central;
    params_central.m_small_object_logical_page_size          = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_small_object_logical_page_size", 65536);
    params_central.m_big_object_logical_page_size            = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_big_object_logical_page_size", 655360);    // THIS SHOULD BE 16 BYTES MORE THAN THE LARGEST EXPECTED ALLOCATION SIZE
    params_central.m_segment_grow_coefficient               = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_grow_coefficient", 0.0);
    params_central.m_small_object_page_recycling_threshold = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_small_object_page_recycling_threshold", 1000);
    params_central.m_big_object_page_recycling_threshold   = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_big_object_page_recycling_threshold", 1000);

    LocalHeapType::HeapCreationParams params_local;
    params_local.m_small_object_logical_page_size             = params_central.m_small_object_logical_page_size;
    params_local.m_big_object_logical_page_size            = params_central.m_big_object_logical_page_size;
    params_local.m_small_object_page_recycling_threshold   = params_central.m_small_object_page_recycling_threshold;
    params_local.m_segment_deallocation_queue_initial_capacity       = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_deallocation_queue_initial_capacity", 3276800);
    params_local.m_big_object_page_recycling_threshold     = params_central.m_big_object_page_recycling_threshold;

    EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(params_central.m_small_object_bin_page_counts, EnvironmentVariable::get_variable("metamalloc_simple_heappow2_central_page_counts", "1,1,1,1,1,1,1,1,"));
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
    ScalableAllocatorType::get_instance().set_thread_local_heap_cache_count(thread_local_heap_cache_count);
    bool success = ScalableAllocatorType::get_instance().create(params_central, params_local, ARENA_CAPACITY);

    if(success)
    {
        trace_message("Metamalloc initialisation success");
        shared_object_loaded = true;
    }
    else
    {
        fprintf(stderr, "Metamalloc initialisation failed\n");
        std::terminate();
    }
    initialisatin_lock.unlock();
}

void uninit_shared_object()
{
    trace_message( "shared object unloading...");
}

extern "C"
{
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// malloc
void *malloc(std::size_t size)
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }

    return ScalableAllocatorType::get_instance().allocate(size);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// free
void free(void* ptr)
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// calloc
void *calloc(std::size_t num, std::size_t size)
{
    return  ScalableAllocatorType::get_instance().allocate_and_zero_memory(num, size);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// realloc
void *realloc(void *ptr, std::size_t size)
{
    return  ScalableAllocatorType::get_instance().reallocate(ptr, size);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// aligned_alloc
void *aligned_alloc(std::size_t alignment, std::size_t size)
{
    return ScalableAllocatorType::get_instance().allocate_aligned(size, alignment);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// malloc_usable_size
std::size_t malloc_usable_size(void* ptr)
{
    return ScalableAllocatorType::get_instance().get_usable_size(ptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 64bit Operator new variants
void* _Znwm(std::size_t size)
{
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void* _Znam(std::size_t size)
{
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void* _Znwmm(std::size_t size, std::size_t alignment)
{
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, alignment);
}

void* _Znamm(std::size_t size, std::size_t alignment)
{
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, alignment);
}

void* _ZnwmSt11align_val_t(std::size_t size, std::align_val_t alignment)
{
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void* _ZnamSt11align_val_t(std::size_t size, std::align_val_t alignment)
{
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void* _ZnwmRKSt9nothrow_t(std::size_t size, std::nothrow_t t)
{
    return ScalableAllocatorType::get_instance().allocate(size);
}

void* _ZnamRKSt9nothrow_t(std::size_t size, std::nothrow_t t)
{
    return ScalableAllocatorType::get_instance().allocate(size);
}

void* _ZnwmSt11align_val_tRKSt9nothrow_t(std::size_t size, std::align_val_t alignment, std::nothrow_t t)
{
    return ScalableAllocatorType::get_instance().allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void* _ZnamSt11align_val_tRKSt9nothrow_t(std::size_t size, std::align_val_t alignment, std::nothrow_t t)
{
    return ScalableAllocatorType::get_instance().allocate_aligned(size, static_cast<std::size_t>(alignment));
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 64bit Operator delete variants
void _ZdlPv(void* ptr)
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdaPv(void* ptr)
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdlPvm(void* ptr, std::size_t size) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdlPvSt11nothrow_t(void* ptr, std::nothrow_t t) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdlPvmSt11nothrow_t(void* ptr, std::size_t size, std::nothrow_t t) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdaPvm(void* ptr, std::size_t size) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdaPvSt11nothrow_t(void* ptr, std::nothrow_t) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdaPvmSt11nothrow_t(void* ptr, std::size_t size, std::nothrow_t) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdlPvRKSt9nothrow_t(void* ptr, std::nothrow_t t) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void _ZdaPvRKSt9nothrow_t(void* ptr, std::nothrow_t t) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
} // extern "C"