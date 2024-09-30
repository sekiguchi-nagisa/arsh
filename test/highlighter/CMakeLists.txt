#==========================#
#     highlighter_test     #
#==========================#

set(TEST_NAME highlighter_test)

add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DHIGHLIGHTER_PATH=\"${CMAKE_BINARY_DIR}/tools/highlighter/arcolorize\"")
add_executable(${TEST_NAME}
        highlighter_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main arsh_static highlighter test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME} arcolorize)
add_test(${TEST_NAME} ${TEST_NAME})
