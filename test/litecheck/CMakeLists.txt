#========================#
#     litecheck_test     #
#========================#

set(TEST_NAME litecheck_test)

add_definitions("-DLITECHECK_PATH=\"${CMAKE_SOURCE_DIR}/tools/litecheck/litecheck\"")
add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DLITECHECK_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/cases\" ")
add_executable(${TEST_NAME}
        litecheck_test.cpp
)
add_dependencies(${TEST_NAME} ${BIN_NAME})
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_test(${TEST_NAME} ${TEST_NAME})