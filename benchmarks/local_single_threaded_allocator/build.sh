#!/bin/bash
rm -f benchmark_standard_malloc benchmark_metamalloc benchmark_metamalloc_hugepage
g++ -O3 -fno-rtti -std=c++2a -o benchmark_standard_malloc benchmark_standard_malloc.cpp
g++ -I../../ -I../../examples/ -O3 -fno-rtti -std=c++2a -o benchmark_metamalloc benchmark_metamalloc.cpp
g++ -I../../ -I../../examples/ -O3 -fno-rtti -std=c++2a -o benchmark_metamalloc_hugepage benchmark_metamalloc_hugepage.cpp
