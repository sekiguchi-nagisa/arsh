#==================#
#     arg_test     #
#==================#

set(TEST_NAME arg_test)

add_executable(${TEST_NAME}
        arg_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main)
add_test(${TEST_NAME} ${TEST_NAME})


#=========================#
#     arg_parser_test     #
#=========================#

set(TEST_NAME arg_parser_test)

add_executable(${TEST_NAME}
        arg_parser_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${ARSH_STATIC})
add_test(${TEST_NAME} ${TEST_NAME})