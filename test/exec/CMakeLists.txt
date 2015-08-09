#===================#
#     exec_test     #
#===================#

set(TEST_NAME exec_test)

add_definitions(-DEXEC_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cases")
add_executable(${TEST_NAME}
    exec_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main directive ${YDSH_STATIC})
add_test(${TEST_NAME} ${TEST_NAME})