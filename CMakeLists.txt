set(CMAKE_VERBOSE_MAKEFILE 1)

cmake_minimum_required(VERSION 2.8)

PROJECT(ydsh)

# variable definition
SET(BIN_NAME ydsh)
SET(YDSH_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_subdirectory(${YDSH_SRC}/ast)
add_subdirectory(${YDSH_SRC}/core)
add_subdirectory(${YDSH_SRC}/eval)
add_subdirectory(${YDSH_SRC}/parser)

add_executable(${BIN_NAME} ${YDSH_SRC}/ydsh.cpp)
target_link_libraries(${BIN_NAME} ast core eval parser)
