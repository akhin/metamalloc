#!/bin/bash
rm -f benchmark
g++ -DNDEBUG -O3 -fno-rtti -std=c++2a -o benchmark benchmark.cpp -pthread