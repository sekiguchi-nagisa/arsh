# Support for benchmarking the regexp engine used by Node.js (V8) with
# rust-leipzig/regex-performance.
#
# This file is included from src/CMakeLists.txt when INCLUDE_NODEJS is not "disabled".
#
# Node.js does not ship a linkable library for embedding, so the adapter runs the installed
# `node` executable as a child process. Only the path to that executable is needed here.

# The `node` executable to run. Defaults to the one found on PATH.
set(NODEJS_EXECUTABLE "node" CACHE FILEPATH "path to the Node.js executable")

find_program(NODEJS_PROGRAM NAMES ${NODEJS_EXECUTABLE})
if(NOT NODEJS_PROGRAM)
    message(FATAL_ERROR
            "INCLUDE_NODEJS requires the Node.js executable; '${NODEJS_EXECUTABLE}' was not found.\n"
            "Install Node.js or pass -DNODEJS_EXECUTABLE=<path to node>.")
endif()

message("-- Found Node.js: ${NODEJS_PROGRAM}")

add_definitions(-DNODEJS_EXECUTABLE="${NODEJS_PROGRAM}")
