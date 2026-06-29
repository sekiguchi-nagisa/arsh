#====================#
#     re262_test     #
#====================#

set(TEST_NAME re262_test)

add_definitions("-DBIN_PATH=\"${CMAKE_BINARY_DIR}/${BIN_NAME}\"")
add_definitions("-DRE262_PATH=\"${CMAKE_BINARY_DIR}/tools/test262regex/re262\"")
add_definitions("-DRE262_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/cases\"")
add_definitions("-DLITECHECK_PATH=\"${CMAKE_SOURCE_DIR}/tools/litecheck/litecheck\"")
add_executable(${TEST_NAME}
        re262_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main test_common re262_static)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})