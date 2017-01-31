#!/bin/sh

mkdir -p build-asan
cd build-asan
cmake .. -DCMAKE_C_COMPILER=/usr/bin/clang \
         -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
         -DCMAKE_BUILD_TYPE=debug \
         -DSANITIZER=address

make -j2

ctest
