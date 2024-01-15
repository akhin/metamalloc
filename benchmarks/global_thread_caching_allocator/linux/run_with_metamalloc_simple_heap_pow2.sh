#!/bin/bash
# Our initial VM allocation -> 149946368 = 143 MB
export metamalloc_simple_heappow2_arena_capacity=149946368
export metamalloc_thread_local_heap_cache_count=4
export metamalloc_simple_heappow2_small_object_logical_page_size=65536
export metamalloc_simple_heappow2_big_object_logical_page_size=655360
# 8 VALUES ARE FOR SIZECLASSES = 16 32 64 128 256 512 1024 2048
export metamalloc_simple_heappow2_central_page_counts=1,1,1,1,1,1,1,1
export metamalloc_simple_heappow2_local_page_counts=50,50,50,50,50,50,50,50
export metamalloc_simple_heappow2_deallocation_queue_initial_capacity=3276800
# grow coefficient = 0 means we will be growing in virtual memory at minimum level
export metamalloc_simple_heappow2_grow_coefficient=0
export metamalloc_simple_heappow2_small_object_page_recycling_threshold=1000
export metamalloc_simple_heappow2_big_object_page_recycling_threshold=1000
LD_PRELOAD=./metamalloc_simple_heap_pow2.so ./benchmark
