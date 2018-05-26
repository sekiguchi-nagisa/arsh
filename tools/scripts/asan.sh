#!/bin/sh

SCRIPT_DIR="$(cd $(dirname $0) && pwd -P)"
ROOT=$SCRIPT_DIR/../..

mkdir -p build-asan
cd build-asan
cmake $ROOT -G Ninja \
            -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
            -DCMAKE_BUILD_TYPE=debug \
            -DSANITIZER=address

ninja

ctest
