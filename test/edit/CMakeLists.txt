#===================#
#     edit_test     #
#===================#

set(TEST_NAME edit_test)

add_executable(${TEST_NAME}
        edit_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${ARSH_STATIC} test_common platform)
add_test(${TEST_NAME} ${TEST_NAME})


#=====================#
#     render_test     #
#=====================#

set(TEST_NAME render_test)

add_executable(${TEST_NAME}
        render_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${ARSH_STATIC} test_common platform)
add_test(${TEST_NAME} ${TEST_NAME})


#====================#
#     pager_test     #
#====================#

set(TEST_NAME pager_test)

add_executable(${TEST_NAME}
        pager_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${ARSH_STATIC} test_common platform)
add_test(${TEST_NAME} ${TEST_NAME})


#=====================#
#     keycode_test     #
#=====================#

set(TEST_NAME keycode_test)

add_executable(${TEST_NAME}
        keycode_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main ${ARSH_STATIC} test_common platform)
add_test(${TEST_NAME} ${TEST_NAME})