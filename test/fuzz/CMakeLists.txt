#===================#
#     fuzz_test     #
#===================#

set(TEST_NAME fuzz_test)

add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/fuzzing/ds_fuzzer\"")
add_definitions("-DRE_BIN_PATH=\"${CMAKE_BINARY_DIR}/fuzzing/regex_fuzzer\"")
add_definitions("-DFUZZ_TEST_DIR=\"${CMAKE_SOURCE_DIR}/fuzzing/interest_input\"")
add_definitions("-DRE_FUZZ_TEST_DIR=\"${CMAKE_SOURCE_DIR}/fuzzing/interest_input_regex\"")
add_executable(${TEST_NAME}
        fuzz_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME} ds_fuzzer regex_fuzzer)
add_test(${TEST_NAME} ${TEST_NAME})