#=====================#
#     buffer_test     #
#=====================#

set(TEST_NAME buffer_test)

add_executable(${TEST_NAME}
        buffer_test.cpp
        )
target_link_libraries(${TEST_NAME} gtest gtest_main)
add_test(${TEST_NAME} ${TEST_NAME})
