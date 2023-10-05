#===================#
#     edit_test     #
#===================#

set(TEST_NAME edit_test)

add_executable(${TEST_NAME}
        edit_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${YDSH_STATIC} test_common platform)
add_test(${TEST_NAME} ${TEST_NAME})


#=====================#
#     render_test     #
#=====================#

set(TEST_NAME render_test)

add_executable(${TEST_NAME}
        render_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${YDSH_STATIC} test_common platform)
add_test(${TEST_NAME} ${TEST_NAME})