
#====================#
#     regex_test     #
#====================#

set(TEST_NAME regex_test)
add_executable(${TEST_NAME}
        regex_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main regex test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#========================#
#     regex_e2e_test     #
#========================#

add_executable(redump
        dump.cpp
)
target_link_libraries(redump regex json)

set(TEST_NAME regex_e2e_test)
add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DREDUMP_PATH=\"${CMAKE_BINARY_DIR}/test/regex/redump\"")
add_definitions("-DREGEX_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/cases\"")
add_executable(${TEST_NAME}
        regex_e2e_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main regex test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME} redump)
add_test(${TEST_NAME} ${TEST_NAME})


#====================#
#     rematch        #
#====================#

# A standalone version of `redump -m` which embeds the JavaScript-compatible engines used by
# tools/regex-bench (Hermes / QuickJS / SRELL) and selects one with `-e`. All engines are
# enabled by default; their sources are taken from the `.regex-bench` work directory that
# `tools/regex-bench/run.arsh` populates. An engine whose sources are not found there is
# skipped with a warning, so a plain configure still succeeds. Override the location (or turn
# an engine off) with:
#
#   cmake -S . -B build -DREMATCH_ENABLE_SRELL=OFF \
#         -DQUICKJS_SOURCE_DIR=<quickjs> -DSRELL_SOURCE_DIR=<srell/single-header> \
#         -DHERMES_SOURCE_DIR=<hermes> -DHERMES_BUILD_DIR=<hermes-build>

option(REMATCH_ENABLE_HERMES "build rematch with the Hermes engine" ON)
option(REMATCH_ENABLE_QUICKJS "build rematch with the QuickJS engine" ON)
option(REMATCH_ENABLE_SRELL "build rematch with the SRELL engine" ON)

# default the engine source locations to the tools/regex-bench work directory
if (NOT SRELL_SOURCE_DIR AND
        EXISTS "${CMAKE_SOURCE_DIR}/.regex-bench/srell/srell-src/single-header/srell.hpp")
    set(SRELL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.regex-bench/srell/srell-src/single-header"
            CACHE PATH "path to the SRELL source directory")
endif ()
if (NOT QUICKJS_SOURCE_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/.regex-bench/quickjs/libregexp.c")
    set(QUICKJS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.regex-bench/quickjs"
            CACHE PATH "path to the QuickJS source directory")
endif ()
if (NOT HERMES_SOURCE_DIR AND
        EXISTS "${CMAKE_SOURCE_DIR}/.regex-bench/hermes/include/hermes/Regex/Regex.h")
    set(HERMES_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.regex-bench/hermes"
            CACHE PATH "path to the Hermes source directory")
endif ()
if (NOT HERMES_BUILD_DIR AND
        EXISTS "${CMAKE_SOURCE_DIR}/.regex-bench/hermes-build/lib/Regex/libhermesRegex.a")
    set(HERMES_BUILD_DIR "${CMAKE_SOURCE_DIR}/.regex-bench/hermes-build"
            CACHE PATH "path to the Hermes CMake build directory")
endif ()

# the list of engine names actually enabled, used by the integration test
set(REMATCH_ENGINE_NAMES "")
set(REMATCH_SOURCES rematch.cpp)
set(REMATCH_ENGINES "")

if (REMATCH_ENABLE_HERMES)
    if (HERMES_SOURCE_DIR AND HERMES_BUILD_DIR)
        set(REGEX_ENGINES "")
        include(${CMAKE_SOURCE_DIR}/tools/regex-bench/hermes.cmake)
        list(APPEND REMATCH_ENGINES ${REGEX_ENGINES})
        list(APPEND REMATCH_SOURCES rematch_hermes.cpp)
        list(APPEND REMATCH_ENGINE_NAMES hermes)
    else ()
        message(WARNING
                "rematch: Hermes is enabled but HERMES_SOURCE_DIR/HERMES_BUILD_DIR is not set;"
                " skipping the Hermes engine.")
    endif ()
endif ()

if (REMATCH_ENABLE_QUICKJS)
    if (QUICKJS_SOURCE_DIR)
        set(REGEX_ENGINES "")
        include(${CMAKE_SOURCE_DIR}/tools/regex-bench/quickjs.cmake)
        list(APPEND REMATCH_ENGINES ${REGEX_ENGINES})
        list(APPEND REMATCH_SOURCES ${QUICKJS_SOURCE_DIR}/libregexp.c)
        list(APPEND REMATCH_SOURCES ${QUICKJS_SOURCE_DIR}/libunicode.c)
        list(APPEND REMATCH_SOURCES rematch_quickjs.c)
        list(APPEND REMATCH_ENGINE_NAMES quickjs)
    else ()
        message(WARNING
                "rematch: QuickJS is enabled but QUICKJS_SOURCE_DIR is not set;"
                " skipping the QuickJS engine.")
    endif ()
endif ()

if (REMATCH_ENABLE_SRELL)
    if (SRELL_SOURCE_DIR)
        include(${CMAKE_SOURCE_DIR}/tools/regex-bench/srell.cmake)
        list(APPEND REMATCH_SOURCES rematch_srell.cpp)
        list(APPEND REMATCH_ENGINE_NAMES srell)
    else ()
        message(WARNING
                "rematch: SRELL is enabled but SRELL_SOURCE_DIR is not set;"
                " skipping the SRELL engine.")
    endif ()
endif ()

add_executable(rematch ${REMATCH_SOURCES})
target_link_libraries(rematch regex ${REMATCH_ENGINES} Threads::Threads)
foreach (_engine IN LISTS REMATCH_ENGINE_NAMES)
    string(TOUPPER "${_engine}" _engine_upper)
    target_compile_definitions(rematch PRIVATE "REMATCH_ENABLE_${_engine_upper}")
endforeach ()

# integration test for rematch. it runs one parameterized case per enabled engine, so it is
# only added when at least one engine is enabled.
if (REMATCH_ENGINE_NAMES)
    string(REPLACE ";" " " REMATCH_ENGINE_NAMES_STR "${REMATCH_ENGINE_NAMES}")
    set(TEST_NAME rematch_test)
    add_executable(${TEST_NAME}
            rematch_test.cpp
    )
    target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
    target_compile_definitions(${TEST_NAME} PRIVATE
            "REMATCH_PATH=\"${CMAKE_BINARY_DIR}/test/regex/rematch\""
            "REMATCH_ENGINES=\"${REMATCH_ENGINE_NAMES_STR}\"")
    add_dependencies(${TEST_NAME} rematch)
    add_test(${TEST_NAME} ${TEST_NAME})
endif ()
