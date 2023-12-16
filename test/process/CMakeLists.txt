#=====================#
#     common_test     #
#=====================#

set(TEST_NAME process_test)

add_definitions("-DINSPECT_PATH=\"${CMAKE_BINARY_DIR}/tools/inspect/inspect\"")
add_executable(${TEST_NAME}
        process_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common platform)
add_dependencies(${TEST_NAME} ${BIN_NAME} inspect)
add_test(${TEST_NAME} ${TEST_NAME})