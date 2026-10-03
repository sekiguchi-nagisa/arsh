# Support for benchmarking the SRELL regex engine with rust-leipzig/regex-performance.
#
# This file is included from src/CMakeLists.txt when INCLUDE_SRELL is not "disabled".
# SRELL is a header-only library, so only an include directory is needed.

# The SRELL source directory (the directory that contains srell.hpp).
set(SRELL_SOURCE_DIR "" CACHE PATH "path to the SRELL source directory")

if(NOT SRELL_SOURCE_DIR)
    message(FATAL_ERROR
            "INCLUDE_SRELL requires -DSRELL_SOURCE_DIR=<SRELL source directory>.")
endif()

if(NOT EXISTS "${SRELL_SOURCE_DIR}/srell.hpp")
    message(FATAL_ERROR
            "srell.hpp not found in '${SRELL_SOURCE_DIR}'.\n"
            "Get SRELL from https://github.com/upa-url/srell.git (or the upstream archive).")
endif()

include_directories("${SRELL_SOURCE_DIR}")

# SRELL requires C++11 or later; the benchmark defaults to c++11 which is enough.
