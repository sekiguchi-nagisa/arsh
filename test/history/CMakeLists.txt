#======================#
#     history_test     #
#======================#

set(TEST_NAME history_test)

add_definitions(-DHISTORY_MOD_PATH="${PROJECT_SOURCE_DIR}/etc/ydsh/module/history")
add_executable(${TEST_NAME}
    history_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${YDSH_STATIC} test_common)
add_test(${TEST_NAME} ${TEST_NAME})