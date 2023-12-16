#===================#
#     node_test     #
#===================#

set(TEST_NAME node_test)

add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_executable(${TEST_NAME}
        node_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})
