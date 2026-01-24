#======================#
#     unicode_test     #
#======================#

set(TEST_NAME unicode_test)

add_definitions("-DGRAPHEME_BREAK_TEST_H=\"${CMAKE_CURRENT_SOURCE_DIR}/grapheme_break_test.in\"")
add_definitions("-DWORD_BREAK_TEST_H=\"${CMAKE_CURRENT_SOURCE_DIR}/word_break_test.in\"")

add_executable(${TEST_NAME}
        unicode_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main unicode)
add_test(${TEST_NAME} ${TEST_NAME})


#==================#
#     set_test     #
#==================#

set(TEST_NAME set_test)
add_executable(${TEST_NAME}
        set_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main unicode test_common)
add_test(${TEST_NAME} ${TEST_NAME})


#====================#
#     emoji_test     #
#====================#

set(TEST_NAME emoji_test)
add_executable(${TEST_NAME}
        emoji_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main emoji_trie unicode test_common)
add_test(${TEST_NAME} ${TEST_NAME})