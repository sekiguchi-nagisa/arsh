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


#=======================#
#     analyzer_test     #
#=======================#

set(TEST_NAME analyzer_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_definitions(-DBIN_PATH="${CMAKE_BINARY_DIR}/${BIN_NAME}")
add_definitions(-DEXEC_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../exec/cases")
add_executable(${TEST_NAME}
        analyzer_test.cpp
        )
add_dependencies(${TEST_NAME} ${BIN_NAME})
target_link_libraries(${TEST_NAME} gtest gtest_main ydshd_static test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#======================#
#     archive_test     #
#======================#

set(TEST_NAME archive_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        archive_test.cpp
        )
target_link_libraries(${TEST_NAME} gtest gtest_main ydshd_static)
add_test(${TEST_NAME} ${TEST_NAME})


#====================#
#     index_test     #
#====================#

set(TEST_NAME index_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        index_test.cpp
        )
target_link_libraries(${TEST_NAME} gtest gtest_main ydshd_static)
add_test(${TEST_NAME} ${TEST_NAME})