#==================#
#     lsp_test     #
#==================#

set(TEST_NAME lsp_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        lsp_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main arshd_static jsonrpc highlighter test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#=====================#
#     worker_test     #
#=====================#

set(TEST_NAME worker_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        worker_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main arshd_static test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#=======================#
#     analyzer_test     #
#=======================#

set(TEST_NAME analyzer_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DLITECHECK_PATH=\"${CMAKE_SOURCE_DIR}/tools/litecheck/litecheck\"")
add_definitions("-DANALYZER_PATH=\"${CMAKE_BINARY_DIR}/tools/analyzer/arshd\"")
add_definitions("-DEXEC_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../exec/cases\"")
add_definitions("-DANALYZER_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/cases\"")
add_executable(${TEST_NAME}
        analyzer_test.cpp
)
add_dependencies(${TEST_NAME} ${BIN_NAME} arshd)
target_link_libraries(${TEST_NAME} gtest gtest_main arshd_static highlighter test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#======================#
#     archive_test     #
#======================#

set(TEST_NAME archive_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        archive_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main arshd_static test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#====================#
#     index_test     #
#====================#

set(TEST_NAME index_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        index_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main arshd_static highlighter test_common)
add_test(${TEST_NAME} ${TEST_NAME})

set(TEST_NAME index_ref_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        index_ref_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main arshd_static test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#=====================#
#     rename_test     #
#=====================#

set(TEST_NAME rename_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        rename_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main arshd_static test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#====================#
#     open_test1     #
#====================#

set(TEST_NAME open_test1)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        open_test1.cpp
)
add_dependencies(${TEST_NAME} ${BIN_NAME} arshd)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_test(${TEST_NAME} ${TEST_NAME})

#====================#
#     open_test2     #
#====================#

set(TEST_NAME open_test2)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        open_test2.cpp
)
add_dependencies(${TEST_NAME} ${BIN_NAME} arshd)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_test(${TEST_NAME} ${TEST_NAME})

#====================#
#     open_test3     #
#====================#

set(TEST_NAME open_test3)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
        open_test3.cpp
)
add_dependencies(${TEST_NAME} ${BIN_NAME} arshd)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_test(${TEST_NAME} ${TEST_NAME})
