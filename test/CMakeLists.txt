#++++++++++++++++++++++++++++++++++++#
#     setup test in subdirectory     #
#++++++++++++++++++++++++++++++++++++#

include_directories(${GTEST_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/tools/directive)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/api)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/arg)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/buffer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bytecode)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lexer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/linenum)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/type)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/precedence)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/exec)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/redirect)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cmdline)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/interactive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/unicode)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/time)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/resource)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/history)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vm)

if(USE_DBUS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dbus)
endif()
