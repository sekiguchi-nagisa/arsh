set(CMAKE_VERBOSE_MAKEFILE 1)

cmake_minimum_required(VERSION 2.8)

PROJECT(ydsh)

# variable definition
SET(BIN_NAME ydsh)
SET(YDSH_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")

# definition of some helper function
## build static link library in current directory
function(as_static_lib)
    get_filename_component(LIB_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)
    add_library(${LIB_NAME} STATIC ${SOURCES})
endfunction(as_static_lib)


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_subdirectory(${YDSH_SRC}/ast)
add_subdirectory(${YDSH_SRC}/core)
add_subdirectory(${YDSH_SRC}/eval)
add_subdirectory(${YDSH_SRC}/parser)

add_executable(${BIN_NAME} ${YDSH_SRC}/ydsh.cpp)
target_link_libraries(${BIN_NAME} ast core eval parser)

install(TARGETS ${BIN_NAME} RUNTIME DESTINATION bin)

MESSAGE(STATUS "CMAKE_BUILD_TYPE = ${uppercase_CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "CMAKE_C_COMPILER = ${CMAKE_C_COMPILER}")
MESSAGE(STATUS "CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")
MESSAGE(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
MESSAGE(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
