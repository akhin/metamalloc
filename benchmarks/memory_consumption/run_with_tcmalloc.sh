#!/bin/bash
LD_PRELOAD=../global_thread_caching_allocator/linux/libtcmalloc.so.4 ./benchmark
