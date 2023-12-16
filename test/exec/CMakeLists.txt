#===================#
#     exec_test     #
#===================#

add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DEXEC_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/cases\"")
add_definitions("-DLITECHECK_PATH=\"${CMAKE_SOURCE_DIR}/tools/litecheck/litecheck\"")

# test1
set(TEST_NAME exec_test1)
add_executable(${TEST_NAME}
        exec_test1.cpp
)
add_dependencies(${TEST_NAME} ${BIN_NAME})
target_link_libraries(${TEST_NAME} gtest gtest_main directive ${YDSH_STATIC} test_common)
add_test(${TEST_NAME} ${TEST_NAME})

# test2
set(TEST_NAME exec_test2)
add_executable(${TEST_NAME}
        exec_test2.cpp
)
add_dependencies(${TEST_NAME} ${BIN_NAME})
target_link_libraries(${TEST_NAME} gtest gtest_main directive ${YDSH_STATIC} test_common)
add_test(${TEST_NAME} ${TEST_NAME})


# test3
set(TEST_NAME exec_test3)
add_executable(${TEST_NAME}
        exec_test3.cpp
)
add_dependencies(${TEST_NAME} ${BIN_NAME})
target_link_libraries(${TEST_NAME} gtest gtest_main directive ${YDSH_STATIC} test_common)
add_test(${TEST_NAME} ${TEST_NAME})