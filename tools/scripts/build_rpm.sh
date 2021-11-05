#!/bin/sh

CC=$1
if [ -z "$CC" ]; then
  CC=/usr/bin/clang++
fi

error() {
  echo 1>&2 "$@"
  exit 1
}


command -v rpmbuild || error require rpmbuild

SCRIPT_DIR="$(cd $(dirname "$0") && pwd -P)"
ROOT="$SCRIPT_DIR/../.."

mkdir -p build-rpm
cd build-rpm

cmake "$ROOT" -G Ninja \
      -DCMAKE_CXX_COMPILER=$CC \
      -DCMAKE_BUILD_TYPE=RELWITHDEBINFO \
      -DCMAKE_INSTALL_PREFIX=/usr

ninja || error build failed

ctest -j4 --output-on-failure || error test failed

cpack -G RPM
