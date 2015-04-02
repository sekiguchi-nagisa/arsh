include(CheckIncludeFiles)

cmake_minimum_required(VERSION 2.8)

PROJECT(ydsh)

# variable definition
SET(CMAKE_VERBOSE_MAKEFILE 1)
SET(BIN_NAME ydsh)
SET(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
SET(YDSH_SRC "")
SET(YDSH_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    SET(CMAKE_BUILD_TYPE Release)
endif()

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")

# definition of some helper function
# add source in current directory to target
macro(add_source)
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} ADD_SOURCES)
    set(YDSH_SRC
        ${YDSH_SRC}
        ${ADD_SOURCES}
        PARENT_SCOPE
    )
endmacro(add_source)

# set extra option
option(USE_TRACE_TOKEN "trace generated token" OFF)
if(USE_TRACE_TOKEN)
    add_definitions(-DX_TRACE_TOKEN)
endif()


include_directories(${YDSH_INCLUDE})
add_subdirectory(${SRC_DIR}/ast)
add_subdirectory(${SRC_DIR}/core)
add_subdirectory(${SRC_DIR}/parser)
add_subdirectory(${SRC_DIR}/exe)
add_subdirectory(${SRC_DIR}/misc)

# check editline
check_include_files("histedit.h" HAVE_HISTEDIT_H)
find_library(HAVE_LIBEDIT NAMES edit)

# build re2c
set(RE2C_V "0.14.2")
set(RE2C_URL "http://downloads.sourceforge.net/project/re2c/re2c/0.14.2/re2c-0.14.2.tar.gz?r=&ts=1427624328&use_mirror=jaist")
set(RE2C_PATH ${CMAKE_CURRENT_BINARY_DIR}/re2c-${RE2C_V}.tar.gz)
set(RE2C_DIR ${CMAKE_CURRENT_BINARY_DIR}/re2c-${RE2C_V})

if(NOT EXISTS "${RE2C_PATH}")
    file(DOWNLOAD "${RE2C_URL}" "${RE2C_PATH}" SHOW_PROGRESS)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xvf ${RE2C_PATH})
    execute_process(COMMAND ./configure WORKING_DIRECTORY ${RE2C_DIR})
    execute_process(COMMAND make WORKING_DIRECTORY ${RE2C_DIR})
endif()


# generate lexer 
set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/src/parser/nextToken.cpp) 
add_custom_command(OUTPUT ${lexer_src} 
    COMMAND ${RE2C_DIR}/re2c -c -8 -o ${lexer_src} ${SRC_DIR}/nextToken.re2c.cpp
)
add_custom_target(gen_lexer DEPENDS ${lexer_src})


# add executable
set(YDSH_STATIC ydsh_static)
add_library(${YDSH_STATIC} STATIC ${YDSH_SRC} ${lexer_src})
add_dependencies(${YDSH_STATIC} gen_lexer)
add_executable(${BIN_NAME} ${SRC_DIR}/ydsh.cpp)
target_link_libraries(${BIN_NAME} ${YDSH_STATIC} ${HAVE_LIBEDIT} args_parser)

install(TARGETS ${BIN_NAME} RUNTIME DESTINATION bin)

# test setup
enable_testing()
add_subdirectory(test)
add_custom_target(run_test COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure)



MESSAGE(STATUS "CMAKE_BUILD_TYPE                 = ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "CMAKE_C_COMPILER                 = ${CMAKE_C_COMPILER}")
MESSAGE(STATUS "CMAKE_C_FLAGS                    = ${CMAKE_C_FLAGS}")
MESSAGE(STATUS "CMAKE_C_FLAGS_DEBUG              = ${CMAKE_C_FLAGS_DEBUG}")
MESSAGE(STATUS "CMAKE_C_FLAGS_RELEASE            = ${CMAKE_C_FLAGS_RELEASE}")
MESSAGE(STATUS "CMAKE_C_FLAGS_RELWITHDEBINFO     = ${CMAKE_C_FLAGS_RELWITHDEBINFO}")
MESSAGE(STATUS "CMAKE_CXX_COMPILER               = ${CMAKE_CXX_COMPILER}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS                  = ${CMAKE_CXX_FLAGS}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS_DEBUG            = ${CMAKE_CXX_FLAGS_DEBUG}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS_RELEASE          = ${CMAKE_CXX_FLAGS_RELEASE}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO   = ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
MESSAGE(STATUS "CMAKE_INSTALL_PREFIX             = ${CMAKE_INSTALL_PREFIX}")
