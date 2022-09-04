#======================#
#     unicode_test     #
#======================#

set(TEST_NAME unicode_test)

add_definitions(-DGRAPHEME_BREAK_TEST_H="${CMAKE_CURRENT_SOURCE_DIR}/grapheme_break_test.h")
add_definitions(-DWORD_BREAK_TEST_H="${CMAKE_CURRENT_SOURCE_DIR}/word_break_test.h")

add_executable(${TEST_NAME}
        unicode_test.cpp
        )
target_link_libraries(${TEST_NAME} gtest gtest_main)
add_test(${TEST_NAME} ${TEST_NAME})
