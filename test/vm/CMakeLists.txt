#=================#
#     vm_test     #
#=================#

set(TEST_NAME vm_test)

add_executable(${TEST_NAME}
        vm_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common ${ARSH_STATIC})
add_test(${TEST_NAME} ${TEST_NAME})


#=====================#
#     object_test     #
#=====================#

set(TEST_NAME object_test)

add_executable(${TEST_NAME}
        object_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common ${ARSH_STATIC})
add_test(${TEST_NAME} ${TEST_NAME})