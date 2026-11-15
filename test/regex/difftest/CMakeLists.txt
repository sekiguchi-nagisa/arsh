
#========================#
#     regex_difftest     #
#========================#

# Differential test of the arsh regex engine against the regexp engine used by Node.js (V8).
#
# Node.js is the oracle, and it is not embeddable here, so `oracle.js` drives
# `RegExp.prototype.exec` out of process over a JSON-lines protocol (see oracle.js / oracle.cpp).
# Without a `node` executable there is nothing to compare against, so the test target is not
# created at all instead of failing the whole build.
#
# The corpus (cases/*.jsonl) is compared only in the unicode (`u`) and unicode sets (`v`) modes,
# and cases whose outcome depends on the Unicode data version are skipped when the Node.js build
# bundles a different Unicode version than arsh (see difftest.h).
#
# The oracle must also be recent enough: the corpus uses `v` mode set operations, inline modifiers
# and properties of strings, and comparing an engine that does not implement them would either
# skip most cases or report them as divergences. Set NODE_MIN_VERSION to the oldest acceptable
# Node.js; a `node` older than that is treated like a missing one (the target is not built).

set(NODE_MIN_VERSION "26" CACHE STRING
        "minimum Node.js version required to build the regex differential test (major[.minor[.patch]])")

# a malformed requirement would make the VERSION_LESS comparison below meaningless (CMake reads a
# non-numeric component as 0, which silently accepts every Node.js), so reject it up front
if (NOT NODE_MIN_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR
            "regex_difftest: NODE_MIN_VERSION='${NODE_MIN_VERSION}' is not a version;"
            " use form major[.minor[.patch]] (e.g. -DNODE_MIN_VERSION=26).")
endif ()

find_program(NODE_PROGRAM NAMES node nodejs)
if (NOT NODE_PROGRAM)
    message(WARNING
            "regex_difftest: no `node` executable found; the differential test is not built."
            " Install Node.js ${NODE_MIN_VERSION} or newer, or pass -DNODE_PROGRAM=<path to node>.")
    return()
endif ()
message(STATUS "found command - node in ${NODE_PROGRAM}")

execute_process(
        COMMAND ${NODE_PROGRAM} --version
        OUTPUT_VARIABLE NODE_VERSION_OUTPUT
        ERROR_VARIABLE NODE_VERSION_ERROR
        RESULT_VARIABLE NODE_VERSION_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX REPLACE "^v" "" NODE_VERSION "${NODE_VERSION_OUTPUT}")
string(STRIP "${NODE_VERSION}" NODE_VERSION)
if (NOT NODE_VERSION_RESULT EQUAL 0 OR NODE_VERSION STREQUAL "")
    message(WARNING
            "regex_difftest: cannot determine the Node.js version of `${NODE_PROGRAM}`"
            " (${NODE_VERSION_ERROR}); the differential test is not built.")
    return()
endif ()
if (NODE_VERSION VERSION_LESS NODE_MIN_VERSION)
    message(WARNING
            "regex_difftest: Node.js ${NODE_VERSION} at `${NODE_PROGRAM}` is too old,"
            " ${NODE_MIN_VERSION} or newer is required; the differential test is not built."
            " Pass -DNODE_PROGRAM=<path to node> or lower -DNODE_MIN_VERSION=<version>.")
    return()
endif ()
message(STATUS "node version: ${NODE_VERSION} (required: ${NODE_MIN_VERSION} or newer)")

include_directories(${CMAKE_SOURCE_DIR}/tools/json)

set(TEST_NAME regex_difftest)
add_definitions("-DNODE_PATH=\"${NODE_PROGRAM}\"")
add_definitions("-DNODE_MIN_VERSION_STR=\"${NODE_MIN_VERSION}\"")
add_definitions("-DORACLE_SCRIPT_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/oracle.js\"")
add_definitions("-DDIFFTEST_CASE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/cases\"")

add_executable(${TEST_NAME}
        difftest_test.cpp
        difftest.cpp
        oracle.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main regex json test_common)
add_test(${TEST_NAME} ${TEST_NAME})
