#=====================#
#     option_test     #
#=====================#

set(TEST_NAME option_test)

add_definitions(-DBIN_PATH="${CMAKE_BINARY_DIR}/${BIN_NAME}")
add_definitions(-DOPTION_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cases")
add_executable(${TEST_NAME}
    option_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})