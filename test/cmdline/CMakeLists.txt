#======================#
#     cmdline_test     #
#======================#

set(TEST_NAME cmdline_test)

add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DPID_CHECK_PATH=\"${CMAKE_BINARY_DIR}/tools/pid_check/pid_check\"")
add_executable(${TEST_NAME}
        cmdline_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common platform)
add_dependencies(${TEST_NAME} ${BIN_NAME} pid_check)
add_test(${TEST_NAME} ${TEST_NAME})