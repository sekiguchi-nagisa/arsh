
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

find_program(NODE_PROGRAM NAMES node nodejs)
if (NOT NODE_PROGRAM)
    message(WARNING
            "regex_difftest: no `node` executable found; the differential test is not built."
            " Install Node.js or pass -DNODE_PROGRAM=<path to node>.")
    return()
endif ()
message(STATUS "found command - node in ${NODE_PROGRAM}")

include_directories(${CMAKE_SOURCE_DIR}/tools/json)

set(TEST_NAME regex_difftest)
add_definitions("-DNODE_PATH=\"${NODE_PROGRAM}\"")
add_definitions("-DORACLE_SCRIPT_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/oracle.js\"")
add_definitions("-DDIFFTEST_CASE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/cases\"")

add_executable(${TEST_NAME}
        difftest_test.cpp
        difftest.cpp
        oracle.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main regex json test_common)
add_test(${TEST_NAME} ${TEST_NAME})
