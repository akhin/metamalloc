#!/bin/bash
# Our initial VM allocation -> 268435456 = 256 MB
export metamalloc_simple_heappow2_arena_capacity=268435456
export metamalloc_thread_local_heap_cache_count=4
export metamalloc_simple_heappow2_logical_page_size=65536
# 8 VALUES ARE FOR SIZECLASSES = 16 32 64 128 256 512 1024 2048
export metamalloc_simple_heappow2_central_page_counts=1,1,1,1,1,1,1,1,1,1,1,1
export metamalloc_simple_heappow2_local_page_counts=50,50,50,50,50,50,50,50,50,50,50,50
export metamalloc_simple_heappow2_deallocation_queue_initial_capacity=3276800
# grow coefficient = 0 means we will be growing in virtual memory at minimum level
export metamalloc_simple_heappow2_grow_coefficient=0
export metamalloc_simple_heappow2_logical_page_recycling_threshold=1000
LD_PRELOAD=./metamalloc_simple_heap_pow2.so ./benchmark
