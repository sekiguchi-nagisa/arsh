#+++++++++++++++++++++++++++++#
#     find expect command     #
#+++++++++++++++++++++++++++++#

execute_process(COMMAND which expect RESULT_VARIABLE status)
if(NOT (${status} EQUAL 0))
    message(FATAL_ERROR "not found expect command")
endif()


#==========================#
#     interactive_test     #
#==========================#

set(TEST_NAME interactive_test)

add_definitions(-DBIN_PATH="${CMAKE_BINARY_DIR}/${BIN_NAME}")
add_definitions(-DINTERACTIVE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cases")
add_definitions(-DINTERACTIVE_TEST_WORK_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(${TEST_NAME}
    interactive_test.cpp
)
target_link_libraries(${TEST_NAME} gtest gtest_main)
add_dependencies(${TEST_NAME} ${BIN_NAME})
add_test(${TEST_NAME} ${TEST_NAME})