#!/bin/sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
ROOT=$SCRIPT_DIR/../..

mkdir -p build-analyze
cd build-analyze || exit 1
cmake "$ROOT"
rm -rf CMakeCache.txt
rm -rf CMakeFiles

export CCC_CXX=clang++
cmake "$ROOT" -DCMAKE_BUILD_TYPE=debug \
            -DCMAKE_CXX_COMPILER=/usr/bin/c++-analyzer

scan-build -o tmp make -j2
