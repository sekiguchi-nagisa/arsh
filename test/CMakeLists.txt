#+++++++++++++++++++++#
#     build gtest     #
#+++++++++++++++++++++#

set(GTEST_V "1.7.0")
set(GTEST_URL "https://googletest.googlecode.com/files/gtest-${GTEST_V}.zip")
set(GTEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/gtest-${GTEST_V}.zip)
set(GTEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gtest-${GTEST_V})

if(NOT EXISTS "${GTEST_DIR}")
    file(DOWNLOAD "${GTEST_URL}" "${GTEST_PATH}" SHOW_PROGRESS)
    execute_process(COMMAND unzip ${GTEST_PATH} -d ${CMAKE_CURRENT_SOURCE_DIR})
endif()

add_subdirectory(${GTEST_DIR})


#++++++++++++++++++++++++++++++++++++#
#     setup test in subdirectory     #
#++++++++++++++++++++++++++++++++++++#

include_directories(${GTEST_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/tools/directive)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Lexer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Type)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Exec)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Arg)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Option)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Redirect)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/Precedence)

if(USE_DBUS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/DBus)
endif()
