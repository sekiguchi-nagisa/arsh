# Support for benchmarking the Hermes regex engine with rust-leipzig/regex-performance.
#
# This file is included from src/CMakeLists.txt when INCLUDE_HERMES is not "disabled".
# Hermes does not ship an installable regex library, so this links the static libraries
# from an existing Hermes build tree instead.

# The Hermes source directory (the directory that contains include/hermes/Regex/Regex.h).
set(HERMES_SOURCE_DIR "" CACHE PATH "path to the Hermes source directory")

# The Hermes CMake build directory (the one that contains lib/Regex/libhermesRegex.a).
set(HERMES_BUILD_DIR "" CACHE PATH "path to the Hermes CMake build directory")

if(NOT HERMES_SOURCE_DIR)
    message(FATAL_ERROR
            "INCLUDE_HERMES requires -DHERMES_SOURCE_DIR=<Hermes source directory>.")
endif()

if(NOT HERMES_BUILD_DIR)
    message(FATAL_ERROR
            "INCLUDE_HERMES requires -DHERMES_BUILD_DIR=<Hermes CMake build directory>.")
endif()

foreach(_hermes_lib Regex/libhermesRegex Platform/Unicode/libhermesPlatformUnicode
        Support/libhermesSupport)
    if(NOT EXISTS "${HERMES_BUILD_DIR}/lib/${_hermes_lib}.a")
        message(FATAL_ERROR
                "${_hermes_lib}.a not found in '${HERMES_BUILD_DIR}/lib'.\n"
                "Get Hermes from https://github.com/facebook/hermes.git and build the\n"
                "hermesRegex target first, e.g.:\n"
                "  cmake -S <Hermes source dir> -B ${HERMES_BUILD_DIR}"
                " -DCMAKE_BUILD_TYPE=Release\n"
                "  cmake --build ${HERMES_BUILD_DIR} --target hermesRegex hermesSupport")
    endif()
endforeach()

# the Hermes regex headers require C++17 (the benchmark defaults to an older standard)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

# the Hermes headers must be compiled with the same options as the libraries they are
# linked against, otherwise the inlined executor / stack guard would not match the ABI.
add_definitions(-DHERMES_CHECK_NATIVE_STACK)
add_definitions(-DHERMES_ENABLE_UNICODE_REGEXP_PROPERTY_ESCAPES)
add_definitions(-DHERMES_USE_BOOST_CONTEXT=1)

# the Hermes headers produce a lot of unused-parameter warnings under the benchmark's
# warning flags, so silence those (it does not affect the build itself).
if(NOT CMAKE_CXX_FLAGS MATCHES "-Wno-unused-parameter")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
endif()

include_directories("${HERMES_SOURCE_DIR}/include")
include_directories("${HERMES_SOURCE_DIR}/external/llvh/include")
include_directories("${HERMES_SOURCE_DIR}/external/llvh/gen/include")
include_directories("${HERMES_BUILD_DIR}/include")
include_directories("${HERMES_BUILD_DIR}/lib/config")
include_directories("${HERMES_BUILD_DIR}/external/llvh/include")

# Hermes splits its support code into several static libraries; keep the link order intact.
set(_hermes_libs
        "${HERMES_BUILD_DIR}/lib/Regex/libhermesRegex.a"
        "${HERMES_BUILD_DIR}/lib/Platform/Unicode/libhermesPlatformUnicode.a"
        "${HERMES_BUILD_DIR}/lib/Support/libhermesSupport.a"
        "${HERMES_BUILD_DIR}/external/llvh/lib/Support/libLLVHSupport.a"
        "${HERMES_BUILD_DIR}/external/dtoa/libdtoa.a")

if(NOT EXISTS "${HERMES_BUILD_DIR}/external/llvh/lib/Support/libLLVHSupport.a")
    message(FATAL_ERROR
            "libLLVHSupport.a not found in '${HERMES_BUILD_DIR}/external/llvh/lib/Support'.")
endif()

if(NOT EXISTS "${HERMES_BUILD_DIR}/external/dtoa/libdtoa.a")
    message(FATAL_ERROR
            "libdtoa.a not found in '${HERMES_BUILD_DIR}/external/dtoa'.")
endif()

if(APPLE)
    # the CF-based Unicode backend used on Apple platforms links against CoreFoundation.
    find_library(CORE_FOUNDATION CoreFoundation)
    if(CORE_FOUNDATION)
        list(APPEND _hermes_libs "${CORE_FOUNDATION}")
    endif()
endif()

list(APPEND REGEX_ENGINES ${_hermes_libs})
