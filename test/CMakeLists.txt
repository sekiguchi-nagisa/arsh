#++++++++++++++++++++++++++++++++++++#
#     setup test in subdirectory     #
#++++++++++++++++++++++++++++++++++++#

if (CMAKE_VERSION VERSION_LESS 3.2)
    set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
else()
    set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
endif()

download_project(
        PROJ                googletest
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/googletest-source
        DOWNLOAD_DIR        ${CMAKE_SOURCE_DIR}/googletest-download
        GIT_REPOSITORY      https://github.com/google/googletest.git
        GIT_TAG             release-1.8.1
        ${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR} EXCLUDE_FROM_ALL)
include_directories("${gtest_SOURCE_DIR}/include" "${gmock_SOURCE_DIR}/include")

include_directories(${CMAKE_SOURCE_DIR}/tools/directive)
include_directories(${CMAKE_SOURCE_DIR}/tools/process)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/arg)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/buffer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bytecode)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/process)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lexer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/type)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/precedence)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/unicode)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/time)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/resource)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/result)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/signals)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/history)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vm)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/api)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/exec)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/redirect)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cmdline)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/interactive)

include_directories(${CMAKE_SOURCE_DIR}/tools/lsp_srv)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/json)

if(FUZZING_BUILD_MODE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
endif()

if(USE_EXTRA_TEST)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extra)
endif()


#=====================#
#     test_common     #
#=====================#

add_library(test_common STATIC test_common.cpp)
target_link_libraries(test_common process)