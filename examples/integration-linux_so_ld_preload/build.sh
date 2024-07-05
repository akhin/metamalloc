#!/bin/bash
rm -f metamalloc*so sample_app
# RELEASE VERSION
g++ -DNDEBUG -O3 -fno-rtti -shared -I../../ -I../../examples/ -std=c++2a  -fPIC -o metamalloc_simple_heap_pow2.so metamalloc_simple_heap_pow2.cpp -lstdc++ -pthread -ldl
# DEBUG VERSION
g++ -shared -I../../ -I../../examples/ -std=c++2a -fPIC -g -o metamalloc_simple_heap_pow2_debug.so metamalloc_simple_heap_pow2.cpp -lstdc++ -pthread -ldl
# SAMPLE APP
g++ -DNDEBUG -O3 -fno-rtti -std=c++2a  -o sample_app sample_app.cpp -lstdc++ -pthread