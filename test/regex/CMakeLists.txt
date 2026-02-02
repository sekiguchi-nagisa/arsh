
#====================#
#     regex_test     #
#====================#

set(TEST_NAME regex_syntax_test)
add_executable(${TEST_NAME}
        regex_syntax_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main regex test_common)
add_test(${TEST_NAME} ${TEST_NAME})

add_executable(redump
        dump.cpp
)
target_link_libraries(redump regex)