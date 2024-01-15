#!/bin/bash
LD_PRELOAD=./libtbbmalloc_proxy.so.2 ./benchmark
