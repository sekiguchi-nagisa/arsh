#!/bin/sh

SCRIPT_DIR="$(cd $(dirname $0) && pwd -P)"
ROOT=$SCRIPT_DIR/../..

mkdir -p build-fuzz
cd build-fuzz
cmake $ROOT -DCMAKE_C_COMPILER=/usr/bin/clang \
            -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
            -DFUZZING_BUILD_MODE=on
make -j2
