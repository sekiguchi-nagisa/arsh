cmake_minimum_required(VERSION 3.15)

# Standalone build of the `rematch` tool and its integration test.
#
# `rematch` is a standalone version of the arsh `redump -m` dump tool which embeds the
# JavaScript-compatible regex engines used by tools/regex-bench (Hermes / QuickJS / SRELL) and
# selects one with `-e`. It is built separately from the top-level arsh build: the engines and
# the arsh regex libraries it links against come from the regex-bench work directory.
#
# tools/regex-bench/build_rematch.arsh drives this project: it fetches/builds the engines (via
# run.arsh --prepare) and builds the arsh libraries the tool needs, then configures and builds
# `rematch` here. The same cmake files that drive the benchmark (arsh.cmake / srell.cmake /
# quickjs.cmake / hermes.cmake) are reused so both builds stay in sync.
#
#   cmake -S tools/regex-bench -B build-rematch \
#         -DARSH_SOURCE_DIR=<arsh repo> -DARSH_BUILD_DIR=<arsh build> \
#         -DSRELL_SOURCE_DIR=<srell/single-header> -DQUICKJS_SOURCE_DIR=<quickjs> \
#         -DHERMES_SOURCE_DIR=<hermes> -DHERMES_BUILD_DIR=<hermes-build>
#   cmake --build build-rematch --target rematch
#
# All three engines are enabled by default; an engine whose sources are not found is skipped
# with a warning, so a plain configure still succeeds.

project(rematch LANGUAGES C CXX)

if (POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
endif ()

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif ()

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the arsh internal headers are compiled with these namespace macros (see the top-level
# CMakeLists.txt); rematch.cpp includes them directly.
add_definitions(-DBEGIN_MISC_LIB_NAMESPACE_DECL=namespace\ arsh\ {)
add_definitions(-DEND_MISC_LIB_NAMESPACE_DECL=})
add_definitions(-D_GNU_SOURCE)

# mirror the warning flags used by the top-level build so the tool is compiled the same way
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-array-bounds")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-use-after-free")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-warning-option")

# the arsh regex libraries (regex / unicode / casefold / radix) are taken from an existing arsh
# build. This sets ARSH_SOURCE_DIR / ARSH_BUILD_DIR and imports those libraries.
include(${CMAKE_CURRENT_SOURCE_DIR}/arsh.cmake)

option(REMATCH_ENABLE_HERMES "build rematch with the Hermes engine" ON)
option(REMATCH_ENABLE_QUICKJS "build rematch with the QuickJS engine" ON)
option(REMATCH_ENABLE_SRELL "build rematch with the SRELL engine" ON)

# default the engine source locations to the regex-bench work directory
if (NOT SRELL_SOURCE_DIR AND
        EXISTS "${ARSH_SOURCE_DIR}/.regex-bench/srell/srell-src/single-header/srell.hpp")
    set(SRELL_SOURCE_DIR "${ARSH_SOURCE_DIR}/.regex-bench/srell/srell-src/single-header"
            CACHE PATH "path to the SRELL source directory")
endif ()
if (NOT QUICKJS_SOURCE_DIR AND
        EXISTS "${ARSH_SOURCE_DIR}/.regex-bench/quickjs/libregexp.c")
    set(QUICKJS_SOURCE_DIR "${ARSH_SOURCE_DIR}/.regex-bench/quickjs"
            CACHE PATH "path to the QuickJS source directory")
endif ()
if (NOT HERMES_SOURCE_DIR AND
        EXISTS "${ARSH_SOURCE_DIR}/.regex-bench/hermes/include/hermes/Regex/Regex.h")
    set(HERMES_SOURCE_DIR "${ARSH_SOURCE_DIR}/.regex-bench/hermes"
            CACHE PATH "path to the Hermes source directory")
endif ()
if (NOT HERMES_BUILD_DIR AND
        EXISTS "${ARSH_SOURCE_DIR}/.regex-bench/hermes-build/lib/Regex/libhermesRegex.a")
    set(HERMES_BUILD_DIR "${ARSH_SOURCE_DIR}/.regex-bench/hermes-build"
            CACHE PATH "path to the Hermes CMake build directory")
endif ()

# the list of engine names actually enabled, used by the integration test
set(REMATCH_ENGINE_NAMES "")
set(REMATCH_SOURCES rematch.cpp)
set(REMATCH_ENGINES "")

# an engine whose sources are not found is skipped with a warning, so the tool still builds with
# the engines that are present (the engine cmake files treat a missing path as a hard error).
if (REMATCH_ENABLE_HERMES)
    if (HERMES_SOURCE_DIR AND HERMES_BUILD_DIR AND
            EXISTS "${HERMES_SOURCE_DIR}/include/hermes/Regex/Regex.h" AND
            EXISTS "${HERMES_BUILD_DIR}/lib/Regex/libhermesRegex.a")
        set(REGEX_ENGINES "")
        include(${CMAKE_CURRENT_SOURCE_DIR}/hermes.cmake)
        list(APPEND REMATCH_ENGINES ${REGEX_ENGINES})
        list(APPEND REMATCH_SOURCES rematch_hermes.cpp)
        list(APPEND REMATCH_ENGINE_NAMES hermes)
    else ()
        message(WARNING
                "rematch: Hermes is enabled but its sources were not found under"
                " '${HERMES_SOURCE_DIR}' / '${HERMES_BUILD_DIR}'; skipping the Hermes engine.")
    endif ()
endif ()

if (REMATCH_ENABLE_QUICKJS)
    if (QUICKJS_SOURCE_DIR AND EXISTS "${QUICKJS_SOURCE_DIR}/libregexp.c")
        set(REGEX_ENGINES "")
        include(${CMAKE_CURRENT_SOURCE_DIR}/quickjs.cmake)
        list(APPEND REMATCH_ENGINES ${REGEX_ENGINES})
        list(APPEND REMATCH_SOURCES ${QUICKJS_SOURCE_DIR}/libregexp.c)
        list(APPEND REMATCH_SOURCES ${QUICKJS_SOURCE_DIR}/libunicode.c)
        list(APPEND REMATCH_SOURCES rematch_quickjs.c)
        list(APPEND REMATCH_ENGINE_NAMES quickjs)
    else ()
        message(WARNING
                "rematch: QuickJS is enabled but its sources were not found under"
                " '${QUICKJS_SOURCE_DIR}'; skipping the QuickJS engine.")
    endif ()
endif ()

if (REMATCH_ENABLE_SRELL)
    if (SRELL_SOURCE_DIR AND EXISTS "${SRELL_SOURCE_DIR}/srell.hpp")
        include(${CMAKE_CURRENT_SOURCE_DIR}/srell.cmake)
        list(APPEND REMATCH_SOURCES rematch_srell.cpp)
        list(APPEND REMATCH_ENGINE_NAMES srell)
    else ()
        message(WARNING
                "rematch: SRELL is enabled but its sources were not found under"
                " '${SRELL_SOURCE_DIR}'; skipping the SRELL engine.")
    endif ()
endif ()

add_executable(rematch ${REMATCH_SOURCES})
target_link_libraries(rematch arsh_regex_engine ${REMATCH_ENGINES} Threads::Threads)
foreach (_engine IN LISTS REMATCH_ENGINE_NAMES)
    string(TOUPPER "${_engine}" _engine_upper)
    target_compile_definitions(rematch PRIVATE "REMATCH_ENABLE_${_engine_upper}")
endforeach ()


#====================#
#     rematch_test   #
#====================#

# Integration test for rematch. It runs one parameterized case per enabled engine, so it is
# only added when at least one engine is enabled. The GoogleTest/GoogleMock libraries and the
# arsh test support libraries (test_common / process / platform) are reused from an existing
# arsh build instead of being built here.
option(REMATCH_ENABLE_TEST "build the rematch integration test" ON)

if (REMATCH_ENABLE_TEST)
    if (REMATCH_ENGINE_NAMES)
        find_library(REMATCH_GTEST_LIBRARY NAMES gtest
                HINTS "${ARSH_BUILD_DIR}/lib" "${ARSH_BUILD_DIR}/googletest-build/lib")
        find_library(REMATCH_TEST_COMMON_LIBRARY NAMES test_common
                HINTS "${ARSH_BUILD_DIR}/test")
        find_library(REMATCH_PROCESS_LIBRARY NAMES process
                HINTS "${ARSH_BUILD_DIR}/tools/process")
        find_library(REMATCH_PLATFORM_LIBRARY NAMES platform
                HINTS "${ARSH_BUILD_DIR}/tools/platform")
        find_path(REMATCH_GTEST_INCLUDE_DIR NAMES gtest/gtest.h
                HINTS "${ARSH_BUILD_DIR}/googletest-src/googletest/include")
        find_path(REMATCH_GMOCK_INCLUDE_DIR NAMES gmock/gmock.h
                HINTS "${ARSH_BUILD_DIR}/googletest-src/googlemock/include")

        if (REMATCH_GTEST_LIBRARY AND REMATCH_TEST_COMMON_LIBRARY AND REMATCH_PROCESS_LIBRARY AND
                REMATCH_PLATFORM_LIBRARY AND REMATCH_GTEST_INCLUDE_DIR AND
                REMATCH_GMOCK_INCLUDE_DIR)
            string(REPLACE ";" " " REMATCH_ENGINE_NAMES_STR "${REMATCH_ENGINE_NAMES}")
            set(TEST_NAME rematch_test)
            add_executable(${TEST_NAME} rematch_test.cpp)
            target_include_directories(${TEST_NAME} PRIVATE
                    "${REMATCH_GTEST_INCLUDE_DIR}" "${REMATCH_GMOCK_INCLUDE_DIR}"
                    "${ARSH_SOURCE_DIR}/test" "${ARSH_SOURCE_DIR}/tools/process"
                    "${ARSH_SOURCE_DIR}/tools/platform")
            target_compile_definitions(${TEST_NAME} PRIVATE
                    "REMATCH_PATH=\"$<TARGET_FILE:rematch>\""
                    "REMATCH_ENGINES=\"${REMATCH_ENGINE_NAMES_STR}\"")
            target_link_libraries(${TEST_NAME}
                    ${REMATCH_GTEST_LIBRARY} ${REMATCH_TEST_COMMON_LIBRARY}
                    ${REMATCH_PROCESS_LIBRARY} ${REMATCH_PLATFORM_LIBRARY} Threads::Threads)
            add_dependencies(${TEST_NAME} rematch)
            enable_testing()
            add_test(${TEST_NAME} ${TEST_NAME})
        else ()
            message(WARNING
                    "rematch: the arsh test support libraries / GoogleTest were not found under"
                    " ARSH_BUILD_DIR='${ARSH_BUILD_DIR}'; skipping rematch_test. Build the"
                    " test_common / process / platform / gtest targets in that build first.")
        endif ()
    else ()
        message(WARNING "rematch: no engine is enabled; skipping rematch_test.")
    endif ()
endif ()
