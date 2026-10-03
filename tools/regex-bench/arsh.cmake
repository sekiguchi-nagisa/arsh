# Support for benchmarking the arsh regex engine with rust-leipzig/regex-performance.
#
# This file is included from src/CMakeLists.txt when INCLUDE_ARSH is not "disabled".
# It requires a previously built arsh tree and links the resulting static libraries
# into the benchmark binary.

# The arsh source directory (the directory that contains src/regex/regex.h).
set(ARSH_SOURCE_DIR "" CACHE PATH "path to the arsh source directory")

# The arsh build directory (the one that contains libregex.a, libunicode.a, ...).
set(ARSH_BUILD_DIR "" CACHE PATH "path to the arsh CMake build directory")

if(NOT ARSH_SOURCE_DIR)
    message(FATAL_ERROR
            "INCLUDE_ARSH requires -DARSH_SOURCE_DIR=<arsh source directory>.")
endif()

if(NOT ARSH_BUILD_DIR)
    message(FATAL_ERROR
            "INCLUDE_ARSH requires -DARSH_BUILD_DIR=<arsh build directory>.")
endif()

if(NOT EXISTS "${ARSH_BUILD_DIR}/libregex.a")
    message(FATAL_ERROR
            "libregex.a not found in '${ARSH_BUILD_DIR}'.\n"
            "Build the arsh regex libraries first, e.g.:\n"
            "  cmake -S <arsh source dir> -B ${ARSH_BUILD_DIR} -DCMAKE_BUILD_TYPE=Release\n"
            "  cmake --build ${ARSH_BUILD_DIR} --target regex")
endif()

# the arsh regex adapter includes the internal headers from the arsh source tree
include_directories("${ARSH_SOURCE_DIR}/src")
include_directories("${ARSH_BUILD_DIR}/src")

# the arsh regex headers require C++17; the benchmark defaults to an older standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

foreach(_arsh_lib regex unicode casefold radix)
    add_library(arsh_${_arsh_lib} STATIC IMPORTED)
    set_target_properties(arsh_${_arsh_lib} PROPERTIES
            IMPORTED_LOCATION "${ARSH_BUILD_DIR}/lib${_arsh_lib}.a")
endforeach()

# libregex depends on unicode/casefold/radix, so keep the link order intact.
add_library(arsh_regex_engine INTERFACE)
target_link_libraries(arsh_regex_engine INTERFACE
        arsh_regex arsh_unicode arsh_casefold arsh_radix)
