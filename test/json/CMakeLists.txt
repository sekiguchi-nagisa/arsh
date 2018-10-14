#===================#
#     json_test     #
#===================#

set(TEST_NAME json_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
    json_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main json)
add_test(${TEST_NAME} ${TEST_NAME})
