# setup gtest

set(GTEST_V "1.7.0")
set(GTEST_URL "https://googletest.googlecode.com/files/gtest-${GTEST_V}.zip")
set(GTEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/gtest-${GTEST_V}.zip)
set(GTEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gtest-${GTEST_V})

if(NOT EXISTS "${GTEST_DIR}")
    file(DOWNLOAD "${GTEST_URL}" "${GTEST_PATH}" SHOW_PROGRESS)
    execute_process(COMMAND unzip ${GTEST_PATH} -d ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_subdirectory(${GTEST_DIR})


# helper macro for testing
macro(setup_test TEST_NAME)
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} ADD_SOURCES)
    add_executable(${TEST_NAME} ${ADD_SOURCES})
    target_link_libraries(${TEST_NAME} gtest gtest_main ${YDSH_STATIC})
    add_test(${TEST_NAME} ${TEST_NAME})
endmacro()

include_directories(${GTEST_DIR}/include)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Lexer)
