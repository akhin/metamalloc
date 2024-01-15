@echo off

REM Our initial VM allocation -> 268435456 = 256 MB
set metamalloc_simple_heappow2_arena_capacity=268435456
set metamalloc_thread_local_heap_cache_count=8
set metamalloc_simple_heappow2_small_object_logical_page_size=65536
set metamalloc_simple_heappow2_big_object_logical_page_size=196608
REM 8 VALUES ARE FOR SIZECLASSES = 16 32 64 128 256 512 1024 2048
set metamalloc_simple_heappow2_central_page_counts=1,1,1,1,1,1,1,1
set metamalloc_simple_heappow2_local_page_counts=50,50,50,50,50,50,50,50
set metamalloc_simple_heappow2_deallocation_queue_initial_capacity=3276800
REM grow coefficient = 0 means we will be growing in virtual memory at minimum level
set metamalloc_simple_heappow2_grow_coefficient=0
set metamalloc_simple_heappow2_small_object_page_recycling_threshold=1000
set metamalloc_simple_heappow2_big_object_page_recycling_threshold=1000
.\benchmark_metamalloc.exe