#======================#
#     unicode_test     #
#======================#

set(TEST_NAME unicode_test)

add_executable(${TEST_NAME}
    unicode_test.cpp ../../src/grapheme.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main)
add_test(${TEST_NAME} ${TEST_NAME})
