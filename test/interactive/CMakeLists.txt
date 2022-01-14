#==========================#
#     interactive_test     #
#==========================#

add_definitions(-DBIN_PATH="${CMAKE_BINARY_DIR}/${BIN_NAME}")
add_definitions(-DINTERACTIVE_TEST_WORK_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# test1
set(TEST_NAME interactive_test1)
add_executable(${TEST_NAME}
        interactive_test1.cpp
        )
target_link_libraries(${TEST_NAME} gtest gtest_main test_common platform)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})

# test2
set(TEST_NAME interactive_test2)
add_executable(${TEST_NAME}
        interactive_test2.cpp
        )
target_link_libraries(${TEST_NAME} gtest gtest_main test_common platform)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})