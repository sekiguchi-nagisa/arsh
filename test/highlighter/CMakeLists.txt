#==========================#
#     highlighter_test     #
#==========================#

set(TEST_NAME highlighter_test)

add_executable(${TEST_NAME}
        highlighter_test.cpp
        )
target_link_libraries(${TEST_NAME} gtest gtest_main ydsh_static highlighter test_common)
add_test(${TEST_NAME} ${TEST_NAME})
