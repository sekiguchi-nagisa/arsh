# Support for benchmarking the QuickJS regexp engine (libregexp) with
# rust-leipzig/regex-performance.
#
# This file is included from src/CMakeLists.txt when INCLUDE_QUICKJS is not "disabled".
# libregexp is a small C library, so it is compiled together with the adapter.

# The QuickJS source directory (the directory that contains libregexp.h).
set(QUICKJS_SOURCE_DIR "" CACHE PATH "path to the QuickJS source directory")

if(NOT QUICKJS_SOURCE_DIR)
    message(FATAL_ERROR
            "INCLUDE_QUICKJS requires -DQUICKJS_SOURCE_DIR=<QuickJS source directory>.")
endif()

if(NOT EXISTS "${QUICKJS_SOURCE_DIR}/libregexp.c")
    message(FATAL_ERROR
            "libregexp.c not found in '${QUICKJS_SOURCE_DIR}'.\n"
            "Get QuickJS from https://github.com/quickjs-ng/quickjs.git.")
endif()

include_directories("${QUICKJS_SOURCE_DIR}")

# add the engine sources. the adapter (quickjs.c) is compiled as C because of its extension.
set(REGEX_SOURCES ${REGEX_SOURCES}
        ${QUICKJS_SOURCE_DIR}/libregexp.c
        ${QUICKJS_SOURCE_DIR}/libunicode.c)

if(NOT CMAKE_C_FLAGS MATCHES "-Wno-unused-parameter")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
endif()
