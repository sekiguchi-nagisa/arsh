setup_test(exec_test)

add_definitions(-DEXEC_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cases")
add_definitions(-DEXEC_TEST_BIN="${CMAKE_BINARY_DIR}/ydsh")