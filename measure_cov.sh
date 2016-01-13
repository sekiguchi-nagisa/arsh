#!/bin/sh

check_cmd() {
    which $1
    if [ $? != 0 ]; then
        echo not found: $1
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
cd build-coverage
clear_cmake_cache

# setup
cmake ..

# build with coverage
clear_cmake_cache
cmake .. -DCMAKE_C_COMPILER=clang \
         -DCMAKE_CXX_COMPILER=clang++ \
         -DCMAKE_BUILD_TYPE=coverage \
         -DUSE_SAFE_CAST=ON \
         -DUSE_LOGGING=ON \
         -DUSE_FIXED_TIME=ON
make clean
make -j2

# run test
lcov --directory . --zerocounters
ctest
