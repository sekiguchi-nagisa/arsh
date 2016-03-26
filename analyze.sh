#!/bin/sh

rm -rf build-analyze
mkdir build-analyze
cd build-analyze
cmake ..
rm -rf CMakeCache.txt
rm -rf CMakeFiles

export CCC_CC=clang
export CCC_CXX=clang++
cmake .. -DCMAKE_BUILD_TYPE=debug \
         -DCMAKE_C_COMPILER=/usr/bin/ccc-analyzer \
         -DCMAKE_CXX_COMPILER=/usr/bin/c++-analyzer

scan-build -o tmp make -j2
