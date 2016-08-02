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

# check arg size
if [ $# == 0 ]; then
    echo "require at least 1 argument"
    echo "[usage] $0 [build dir] ([gcov tool path])"
    exit 1
fi



# check command
check_cmd lcov

# check gcov tool
TOOL=""
if [ "$2" != "" ]; then
    abspath=$(cd $(dirname $2) && pwd)/$(basename $2)
    TOOL="--gcov-tool $abspath"
fi


# change to build dir
if [ $1 != "" ]; then
    cd $1
fi


# generate coverage report
OUTPUT="coverage_report"
lcov $TOOL --directory . --capture --output-file ${OUTPUT}.info
lcov --remove ${OUTPUT}.info 'test/*' 'tools/*' 'ext/*' 'fuzzing/*' '/usr/include/*' 'nextToken.re2c.cpp' --output-file ${OUTPUT}-cleaned.info
genhtml -o ${OUTPUT} ${OUTPUT}-cleaned.info
