#========================#
#     directive_test     #
#========================#

set(TEST_NAME directive_test)

add_executable(${TEST_NAME}
        directive_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main directive ${ARSH_STATIC})
add_test(${TEST_NAME} ${TEST_NAME})
