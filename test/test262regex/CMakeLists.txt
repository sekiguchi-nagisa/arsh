#====================#
#     re262_test     #
#====================#

set(TEST_NAME re262_test)

add_executable(${TEST_NAME}
        re262_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common re262_static)
add_test(${TEST_NAME} ${TEST_NAME})