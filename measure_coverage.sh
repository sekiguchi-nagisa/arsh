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
check_cmd gcov
check_cmd genhtml

# create build directory

mkdir build-coverage
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
         -DUSE_LOGGING=ON
make clean
make -j2

# run test
lcov --directory . --zerocounters
ctest

# generate coverage report
OUTPUT="coverage_report"
lcov --directory . --capture --output-file ${OUTPUT}.info
lcov --remove ${OUTPUT}.info 'test/*' 'tools/*' '/usr/include/*' --output-file ${OUTPUT}-cleaned.info
genhtml -o ${OUTPUT} ${OUTPUT}-cleaned.info
