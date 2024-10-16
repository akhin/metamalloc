/*

    NOTE THAT DOESN'T COVER ALL FUNCTIONS. YOU WILL NEED TO ADD MISSING REDIRECTIONS.

    LIST OF FUNCTIONS THAT ARE REPLACED :

                                    malloc              https://linux.die.net/man/3/malloc
                                    free                https://linux.die.net/man/3/free
                                    realloc             https://linux.die.net/man/3/realloc
                                    calloc              https://linux.die.net/man/3/calloc

                                    aligned_alloc       https://linux.die.net/man/3/aligned_alloc
                                    malloc_usable_size  https://linux.die.net/man/3/malloc_usable_size

                                    OPERATOR NEW AND DELETE

                                        void* operator new(std::size_t size)
                                        void operator delete(void* ptr)
                                        void* operator new[](std::size_t size)
                                        void operator delete[](void* ptr) noexcept
                                        void* operator new(std::size_t size, const std::nothrow_t&) noexcept
                                        void operator delete(void* ptr, const std::nothrow_t&) noexcept
                                        void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
                                        void operator delete[](void* ptr, const std::nothrow_t&) noexcept
                                        void* operator new(std::size_t size, std::align_val_t alignment)
                                        void operator delete(void* ptr, std::align_val_t alignment) noexcept
                                        void* operator new[](std::size_t size, std::align_val_t alignment)
                                        void operator delete[](void* ptr, std::align_val_t alignment) noexcept
                                        void* operator new(std::size_t size, std::size_t alignment)
                                        void* operator new[](std::size_t size, std::size_t alignment)
                                        void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
                                        void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
                                        void operator delete(void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
                                        void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
                                        void* operator new(std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
                                        void* operator new[](std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
                                        void operator delete(void* ptr, std::size_t, const std::nothrow_t &) noexcept
                                        void operator delete[](void* ptr, std::size_t, const std::nothrow_t &) noexcept
                                        void operator delete(void* ptr, std::size_t size) noexcept
                                        void operator delete[](void* ptr, std::size_t size) noexcept
                                        void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
                                        void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
                                        void operator delete(void* ptr, std::size_t size, std::size_t align) noexcept
                                        void operator delete[](void* ptr, std::size_t size, std::size_t align) noexcept
*/
#include <cstddef>
#include <cstdarg>
#include <exception>
#include <cstdio>
#include <cstring>
#include <new>

//#define ENABLE_REPORT_INVALID_POINTERS
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
    params_central.m_logical_page_size          = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_logical_page_size", 65536);
    params_central.m_segment_grow_coefficient               = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_grow_coefficient", 0.0);
    params_central.m_logical_page_recycling_threshold = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_logical_page_recycling_threshold", 1000);

    LocalHeapType::HeapCreationParams params_local;
    params_local.m_logical_page_size             = params_central.m_logical_page_size;
    params_local.m_logical_page_recycling_threshold   = params_central.m_logical_page_recycling_threshold;
    params_local.m_segment_deallocation_queue_initial_capacity       = EnvironmentVariable::get_variable("metamalloc_simple_heappow2_deallocation_queue_initial_capacity", 3276800);

    EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(params_central.m_bin_logical_page_counts, EnvironmentVariable::get_variable("metamalloc_simple_heappow2_central_page_counts", "1,1,1,1,1,1,1,1,1,1,1,1"));
    EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(params_local.m_bin_logical_page_counts, EnvironmentVariable::get_variable("metamalloc_simple_heappow2_local_page_counts", "50,50,50,50,50,50,50,50,50,50,50,50"));

    trace_integer_value("metamalloc_simple_heappow2_arena_capacity", ARENA_CAPACITY);
    trace_integer_value("metamalloc_thread_local_heap_cache_count", thread_local_heap_cache_count);
    trace_string_value("metamalloc_simple_heappow2_central_page_counts", EnvironmentVariable::get_variable("metamalloc_simple_heappow2_central_page_counts", "1,1,1,1,1,1,1,1,1,1,1,1"));
    trace_string_value("metamalloc_simple_heappow2_local_page_counts", EnvironmentVariable::get_variable("metamalloc_simple_heappow2_local_page_counts", "50,50,50,50,50,50,50,50,50,50,50,50"));
    trace_integer_value("metamalloc_simple_heappow2_logical_page_size", params_central.m_logical_page_size);
    trace_double_value("metamalloc_simple_heappow2_grow_coefficient", params_local.m_segment_grow_coefficient);
    trace_integer_value("metamalloc_simple_heappow2_deallocation_queue_initial_capacity", params_local.m_segment_deallocation_queue_initial_capacity);
    trace_integer_value("metamalloc_simple_heappow2_logical_page_recycling_threshold", params_local.m_logical_page_recycling_threshold);
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
} // extern "C"

///////////////////////////////////////////////////////////////////////
// USUAL OVERLOADS
void* operator new(std::size_t size)
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void operator delete(void* ptr)
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* operator new[](std::size_t size)
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void operator delete[](void* ptr) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH std::nothrow_t
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT
void* operator new(std::size_t size, std::align_val_t alignment)
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    UNUSED(alignment);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    UNUSED(alignment);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT std::size_t
void* operator new(std::size_t size, std::size_t alignment)
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}


void* operator new[](std::size_t size, std::size_t alignment)
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT and std::nothrow_t

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT and std::nothrow_t   STD::SIZE_T not std::align_val_t

void* operator new(std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    if(unlikely(shared_object_loaded == false))
    {
        initialise_shared_object();
    }
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::size_t, const std::nothrow_t &) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t, const std::nothrow_t &) noexcept
{
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// DELETES WITH SIZES
void operator delete(void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete(void* ptr, std::size_t size, std::size_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::size_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}