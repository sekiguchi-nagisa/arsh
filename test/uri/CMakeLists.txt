#==================#
#     uri_test     #
#==================#

set(TEST_NAME uri_test)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${TEST_NAME}
    uri_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main uri)
add_test(${TEST_NAME} ${TEST_NAME})
