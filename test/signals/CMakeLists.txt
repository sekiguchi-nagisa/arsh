#======================#
#     signals_test     #
#======================#

set(TEST_NAME signals_test)

add_executable(${TEST_NAME}
        signals_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${ARSH_STATIC} test_common)
add_test(${TEST_NAME} ${TEST_NAME})