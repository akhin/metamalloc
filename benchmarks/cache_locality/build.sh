#!/bin/bash
rm -f benchmark_gnulibcmalloc benchmark_metamalloc
g++ -O3 -fno-rtti  -std=c++2a -o benchmark_gnulibcmalloc benchmark_gnulibcmalloc.cpp
g++ -O3 -fno-rtti  -I../../ -I../../examples  -std=c++2a -o benchmark_metamalloc benchmark_metamalloc.cpp
