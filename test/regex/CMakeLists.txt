
#====================#
#     regex_test     #
#====================#

add_executable(redump
        dump.cpp
)
target_link_libraries(redump regex)

set(TEST_NAME regex_test)
add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DREDUMP_PATH=\"${CMAKE_BINARY_DIR}/test/regex/redump\"")
add_definitions("-DREGEX_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/cases\"")
add_executable(${TEST_NAME}
        regex_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main regex test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME} redump)
add_test(${TEST_NAME} ${TEST_NAME})