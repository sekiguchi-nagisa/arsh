#==================#
#     api_test     #
#==================#

set(TEST_NAME api_test)

add_definitions("-DPID_CHECK_PATH=\"${CMAKE_BINARY_DIR}/tools/pid_check/pid_check\"")
add_definitions("-DAPI_TEST_WORK_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
add_executable(${TEST_NAME}
        api_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${ARSH_LIB} test_common platform)
add_dependencies(${TEST_NAME} pid_check)
add_test(${TEST_NAME} ${TEST_NAME})