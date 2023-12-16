#===================#
#     type_test     #
#===================#

set(TEST_NAME type_test)

add_executable(${TEST_NAME}
        type_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${ARSH_STATIC})
add_test(${TEST_NAME} ${TEST_NAME})
