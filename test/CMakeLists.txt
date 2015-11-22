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

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/arg)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/buffer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lexer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/linenum)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/type)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/precedence)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/exec)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/redirect)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/option)

if(USE_DBUS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dbus)
endif()
