# Support for benchmarking PCRE2 (interpreted and JIT) with rust-leipzig/regex-performance.
#
# This file is included from src/CMakeLists.txt when INCLUDE_PCRE2 is not "disabled". It links the
# PCRE2 8-bit static library that `run.arsh` builds (with JIT support) instead of relying on the
# harness's own vendored build.

# The PCRE2 build directory (the one that contains libpcre2-8.a and interface/pcre2.h).
set(PCRE2_BUILD_DIR "" CACHE PATH "path to the PCRE2 CMake build directory")

if(NOT PCRE2_BUILD_DIR)
    message(FATAL_ERROR
            "INCLUDE_PCRE2 requires -DPCRE2_BUILD_DIR=<PCRE2 build directory>.")
endif()

if(NOT EXISTS "${PCRE2_BUILD_DIR}/libpcre2-8.a")
    message(FATAL_ERROR
            "libpcre2-8.a not found in '${PCRE2_BUILD_DIR}'.\n"
            "Build PCRE2 first, e.g.:\n"
            "  cmake -S <PCRE2 source dir> -B ${PCRE2_BUILD_DIR} -DCMAKE_BUILD_TYPE=Release"
            " -DPCRE2_SUPPORT_JIT=ON -DPCRE2_STATIC_PIC=ON\n"
            "  cmake --build ${PCRE2_BUILD_DIR} --target pcre2-8-static\n"
            "PCRE2_STATIC_PIC=ON is required: regex_perf is linked as a PIE executable by"
            " default, and a non-PIC static library cannot be linked into it.")
endif()

if(NOT EXISTS "${PCRE2_BUILD_DIR}/interface/pcre2.h")
    message(FATAL_ERROR
            "pcre2.h not found in '${PCRE2_BUILD_DIR}/interface'.\n"
            "Point PCRE2_BUILD_DIR at the PCRE2 CMake build directory.")
endif()

include_directories("${PCRE2_BUILD_DIR}/interface")

list(APPEND REGEX_ENGINES "${PCRE2_BUILD_DIR}/libpcre2-8.a")
