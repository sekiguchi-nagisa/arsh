#++++++++++++++++++++++++++++++++++++#
#     setup test in subdirectory     #
#++++++++++++++++++++++++++++++++++++#

download_project(
        PROJ googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723 # v1.17.0
        GIT_PROGRESS 1
        UPDATE_DISCONNECTED 0
)

add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR} EXCLUDE_FROM_ALL)
include_directories("${gtest_SOURCE_DIR}/include" "${gmock_SOURCE_DIR}/include")

include_directories(${CMAKE_SOURCE_DIR}/tools/directive)
include_directories(${CMAKE_SOURCE_DIR}/tools/process)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/arg)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/buffer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bytecode)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/node)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/process)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lexer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/type)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/scope)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/precedence)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/unicode)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/util)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/resource)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/result)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/signals)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/stringref)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vm)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/api)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/exec)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/redirect)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cmdline)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/interactive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/logger)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/glob)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/brace)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/litecheck)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/edit)

include_directories(${CMAKE_SOURCE_DIR}/tools/json)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/json)

include_directories(${CMAKE_SOURCE_DIR}/tools/uri)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/uri)

include_directories(${CMAKE_SOURCE_DIR}/tools/highlighter)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/highlighter)

include_directories(${CMAKE_SOURCE_DIR}/tools/analyzer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/analyzer)

if (FUZZING_BUILD_MODE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
endif ()

if (USE_EXTRA_TEST)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extra)
endif ()


#=====================#
#     test_common     #
#=====================#

add_library(test_common STATIC test_common.cpp)
target_link_libraries(test_common process platform)
