#!/bin/bash
rm -f sample_app
# SAMPLE APP
g++ -I../../ -I../../examples/ -O3 -fno-rtti -std=c++2a  -o sample_app sample_app.cpp -lstdc++ -pthread