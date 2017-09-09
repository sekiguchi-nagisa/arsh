#======================#
#     history_test     #
#======================#

set(TEST_NAME history_test)

add_executable(${TEST_NAME}
    history_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${YDSH_STATIC} test_common)