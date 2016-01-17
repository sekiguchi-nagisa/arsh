#===================#
#     time_test     #
#===================#

set(TEST_NAME time_test)

add_executable(${TEST_NAME}
    time_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main)
add_test(${TEST_NAME} ${TEST_NAME})
