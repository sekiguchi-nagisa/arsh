#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
ROOT=$SCRIPT_DIR/..

check_cmd() {
    which "$1"
    if [ $? != 0 ]; then
        echo not found: "$1"
        exit 1
    fi
}

clear_cmake_cache() {
    rm -rf CMakeCache.txt
    rm -rf CMakeFiles
}


# check command
check_cmd lcov

# create build directory
mkdir -p build-coverage
cd build-coverage || exit 1
clear_cmake_cache

# build with coverage
cmake "$ROOT" -G Ninja \
            -DCMAKE_CXX_COMPILER=g++ \
            -DCMAKE_C_COMPILER=gcc \
            -DCMAKE_BUILD_TYPE=coverage
ninja clean
ninja

# run test
lcov --directory . --zerocounters
ctest
