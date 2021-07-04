#==================#
#     lsp_test     #
#==================#

set(TEST_NAME lsp_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        lsp_test.cpp
        )
target_link_libraries(${TEST_NAME} gtest gtest_main ydshd_static jsonrpc test_common)
add_test(${TEST_NAME} ${TEST_NAME})
