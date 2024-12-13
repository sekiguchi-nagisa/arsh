#!/bin/sh

CC=$1
if [ -z "$CC" ]; then
  CC=/usr/bin/clang++
fi

error() {
  echo 1>&2 "$@"
  exit 1
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
ROOT="$SCRIPT_DIR/.."

mkdir -p build-profile
cd build-profile || exit 1

cmake "$ROOT" -G Ninja \
      -DCMAKE_CXX_COMPILER=$CC \
      -DCMAKE_BUILD_TYPE=RELWITHDEBINFO

ninja || error build failed

ctest -j4 --output-on-failure || error test failed
