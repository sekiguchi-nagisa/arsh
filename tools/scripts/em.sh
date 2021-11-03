#!/bin/sh

SCRIPT_DIR="$(cd $(dirname "$0") && pwd -P)"
ROOT=$SCRIPT_DIR/../..

mkdir -p build-em
cd build-em

# first normal build
cmake "$ROOT" -G Ninja
ninja

# second build by emscripten
rm -rf CMake*
rm -rf .ninja_*

emcmake cmake "$ROOT" -G Ninja -DUSE_PCRE=off
ninja
