#!/bin/sh

CC=$1
if [ -z "$CC" ]; then
  CC=/usr/bin/clang++
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
ROOT=$SCRIPT_DIR/..

mkdir -p build-ubsan/app
cd build-ubsan || exit 1
cmake "$ROOT" -G Ninja \
            -DCMAKE_CXX_COMPILER=$CC \
            -DCMAKE_BUILD_TYPE=debug \
            -DSANITIZER=undefined \
            -DCMAKE_INSTALL_PREFIX=./app

ninja && ninja install

ctest -j4
