#=====================#
#     common_test     #
#=====================#

set(TEST_NAME process_test)

add_executable(${TEST_NAME}
    process_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_test(${TEST_NAME} ${TEST_NAME})