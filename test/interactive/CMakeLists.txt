#==========================#
#     interactive_test     #
#==========================#

add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DINTERACTIVE_TEST_WORK_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
add_definitions("-DHIGHLIGHTER_PATH=\"${CMAKE_BINARY_DIR}/tools/highlighter/dscolorize\"")
add_definitions("-DHELPER_PATH=\"${CMAKE_BINARY_DIR}/tools/helper/arhelper\"")

# test1
set(TEST_NAME interactive_test1)
add_executable(${TEST_NAME}
        interactive_test1.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME} dscolorize)
add_test(${TEST_NAME} ${TEST_NAME})

# test2
set(TEST_NAME interactive_test2)
add_executable(${TEST_NAME}
        interactive_test2.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})

# test3
set(TEST_NAME interactive_test3)
add_executable(${TEST_NAME}
        interactive_test3.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME} arhelper)
add_test(${TEST_NAME} ${TEST_NAME})

# test4
set(TEST_NAME interactive_test4)
add_executable(${TEST_NAME}
        interactive_test4.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})