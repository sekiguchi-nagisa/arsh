#!/bin/sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
ROOT=$SCRIPT_DIR/..

mkdir -p build
cd build || exit 1
cmake "$ROOT" -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/clang++ "$@"
