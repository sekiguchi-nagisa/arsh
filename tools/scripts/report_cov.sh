#!/bin/bash

check_cmd() {
    which "$1"
    if [ $? != 0 ]; then
        echo not found: "$1"
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
    abspath=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
    TOOL="--gcov-tool $abspath"
fi


# change to build dir
if [ "$1" != "" ]; then
    cd "$1" || exit 1
fi


# generate coverage report
OUTPUT="coverage_report"
# shellcheck disable=SC2086
lcov --rc lcov_branch_coverage=1 $TOOL --directory . --capture --output-file ${OUTPUT}.info
lcov --rc lcov_branch_coverage=1 --remove ${OUTPUT}.info \
    '*test/*' '*fuzzing/*' \
    '*-src/*' '/usr/include/*' '*v1/*' \
    '*.re2c.cpp' '*nextToken.cpp' '*tools/json/lexer.cpp' '*tools/uri/uri_parser.cpp' \
    '*tools/process/ansi.cpp' \
    --output-file ${OUTPUT}-cleaned.info
genhtml --rc lcov_branch_coverage=1 -o ${OUTPUT} ${OUTPUT}-cleaned.info
