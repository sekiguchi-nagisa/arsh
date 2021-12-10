#!/bin/sh

CC=$1
if [ -z "$CC" ]; then
  CC=clang++
fi

mkdir -p build-all
cd build-all || exit 1
echo using compiler: $CC
cmake .. -G Ninja -DCMAKE_CXX_COMPILER=$CC -DUSE_EXTRA_TEST=on

ninja || exit 1
sudo ninja install

../tools/scripts/copy_mod4extra.ds

exec ctest -j4 --output-on-failure
