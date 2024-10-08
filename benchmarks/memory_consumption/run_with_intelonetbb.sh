#!/bin/bash
LD_PRELOAD=../global_thread_caching_allocator/linux/libtbbmalloc_proxy.so.2 ./benchmark
