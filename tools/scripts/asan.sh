#!/bin/sh

SCRIPT_DIR="$(cd $(dirname $0) && pwd -P)"
ROOT=$SCRIPT_DIR/../..

mkdir -p build-asan
cd build-asan
cmake $ROOT -DCMAKE_C_COMPILER=/usr/bin/clang \
            -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
            -DCMAKE_BUILD_TYPE=debug \
            -DSANITIZER=address

make -j2

ctest
