#!/bin/bash
# Our initial VM allocation -> 54525952 = 52 MB
export metamalloc_simple_heappow2_arena_capacity=54525952
export metamalloc_thread_local_heap_cache_count=0
export metamalloc_simple_heappow2_small_object_logical_page_size=65536
export metamalloc_simple_heappow2_big_object_logical_page_size=65536
# 8 VALUES ARE FOR SIZECLASSES = 16 32 64 128 256 512 1024 2048
export metamalloc_simple_heappow2_central_page_counts=25,25,25,25,25,25,25,25
export metamalloc_simple_heappow2_local_page_counts=0,0,0,0,0,0,0,0
export metamalloc_simple_heappow2_deallocation_initial_queue_capacity=0
# grow coefficient = 0 means we will be growing in virtual memory at minimum level
export metamalloc_simple_heappow2_grow_coefficient=0
export metamalloc_simple_heappow2_small_object_page_recycling_threshold=1
export metamalloc_simple_heappow2_big_object_page_recycling_threshold=1
LD_PRELOAD=../global_thread_caching_allocator/linux/metamalloc_simple_heap_pow2.so ./benchmark
