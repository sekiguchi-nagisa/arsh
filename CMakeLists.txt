cmake_minimum_required(VERSION 2.8)

PROJECT(ydsh)

# variable definition
SET(CMAKE_VERBOSE_MAKEFILE 1)
SET(BIN_NAME ydsh)
SET(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
SET(YDSH_SRC ${SRC_DIR}/ydsh.cpp)
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


include_directories(${YDSH_INCLUDE})
add_subdirectory(${SRC_DIR}/ast)
add_subdirectory(${SRC_DIR}/core)
add_subdirectory(${SRC_DIR}/eval)
add_subdirectory(${SRC_DIR}/parser)
add_subdirectory(${SRC_DIR}/util)

# test setup
enable_testing()
add_subdirectory(test)
add_custom_target(run_test COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure)


# generate lexer 
set(lexer_src ${SRC_DIR}/parser/Lexer.cpp) 
add_custom_command(OUTPUT ${lexer_src} 
    COMMAND re2c -c -8 -o ${lexer_src} ${SRC_DIR}/Lexer.re2c.cpp
)
add_custom_target(gen_lexer DEPENDS ${lexer_src})


add_executable(${BIN_NAME} ${YDSH_SRC} ${lexer_src})
add_dependencies(${BIN_NAME} gen_lexer)

install(TARGETS ${BIN_NAME} RUNTIME DESTINATION bin)

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
