#==================#
#     api_test     #
#==================#

set(TEST_NAME api_test)

add_executable(${TEST_NAME}
    api_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${YDSH_STATIC})
add_test(${TEST_NAME} ${TEST_NAME})