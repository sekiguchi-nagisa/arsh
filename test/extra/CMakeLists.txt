#====================#
#     extra_test     #
#====================#

set(TEST_NAME extra_test)

add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DEXTRA_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/_mod4extra\"")
add_executable(${TEST_NAME}
        extra_test.cpp
)
target_link_libraries(${TEST_NAME} ${ARSH_LIB} gtest gtest_main test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})
