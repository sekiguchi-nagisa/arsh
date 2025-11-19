#!/bin/sh

CC=$1
if [ -z "$CC" ]; then
  CC=/usr/bin/clang++
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
ROOT=$SCRIPT_DIR/..

mkdir -p build-typesan/app
cd build-typesan || exit 1
cmake "$ROOT" -G Ninja \
            -DCMAKE_CXX_COMPILER=$CC \
            -DCMAKE_BUILD_TYPE=debug \
            -DSANITIZER=type \
            -DCMAKE_INSTALL_PREFIX=./app

ninja && ninja install

ctest -j4
