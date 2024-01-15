#!/bin/bash
rm -f benchmark
g++ -O3 -fno-rtti -std=c++2a -o benchmark benchmark.cpp -pthread