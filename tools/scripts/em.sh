#!/bin/sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
ROOT=$SCRIPT_DIR/../..

mkdir -p build-em
cd build-em || exit 1

# first normal build
cmake "$ROOT" -G Ninja -DUSE_PCRE=off
ninja

# second build by emscripten
rm -rf CMake*
rm -rf .ninja_*

emcmake cmake "$ROOT" -G Ninja -DUSE_PCRE=off
ninja
