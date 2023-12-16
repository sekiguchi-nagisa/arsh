#===================#
#     glob_test     #
#===================#

set(TEST_NAME glob_test)

add_definitions("-DGLOB_TEST_WORK_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/dir\"")
add_executable(${TEST_NAME}
        glob_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_test(${TEST_NAME} ${TEST_NAME})
