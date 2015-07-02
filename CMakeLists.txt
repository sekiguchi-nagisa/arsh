cmake_minimum_required(VERSION 2.8)

PROJECT(ydsh)

#++++++++++++++++++++++#
#     helper macro     #
#++++++++++++++++++++++#

macro(check_header VAR NAME)
    find_path(${VAR} ${NAME})
    MESSAGE(STATUS "find header file - ${NAME} in ${${VAR}}")
endmacro()


#++++++++++++++++++++++++++++#
#     set some variable      #
#++++++++++++++++++++++++++++#

SET(CMAKE_VERBOSE_MAKEFILE 1)
if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    SET(CMAKE_BUILD_TYPE Release)
endif()

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")


#++++++++++++++++++++++++++++++++++++#
#     set some system infomation     #
#++++++++++++++++++++++++++++++++++++#

add_definitions(-DX_INFO_SYSTEM=${CMAKE_SYSTEM})
add_definitions(-DX_INFO_CPP=${CMAKE_CXX_COMPILER_ID})
add_definitions(-DX_INFO_CPP_V=${CMAKE_CXX_COMPILER_VERSION})


#++++++++++++++++++++++++++#
#     set extra option     #
#++++++++++++++++++++++++++#

option(USE_TRACE_TOKEN "trace generated token" OFF)
if(USE_TRACE_TOKEN)
    add_definitions(-DX_TRACE_TOKEN)
endif()

option(USE_DBUS "support D-Bus" ON)
if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
    set(USE_DBUS OFF)
    add_definitions(-DX_NO_DBUS)
elseif(NOT USE_DBUS)
    add_definitions(-DX_NO_DBUS)
endif()


#++++++++++++++++++++++#
#     find libedit     #
#++++++++++++++++++++++#

check_header(HAVE_HISTEDIT_H "histedit.h")
find_library(HAVE_LIBEDIT NAMES edit)


#++++++++++++++++++++++#
#     find libdbus     #
#++++++++++++++++++++++#

if(USE_DBUS)
    SET(CMAKE_INCLUDE_PATH
        "/usr/include/dbus-1.0"
        "/usr/lib64/dbus-1.0/include"
        "/usr/lib/dbus-1.0/include"
        "/usr/lib/x86_64-linux-gnu/dbus-1.0/include"
    )
    check_header(HAVE_DBUS_H "dbus/dbus.h")
    check_header(HAVE_DBUS_ARCH_DEPS_H "dbus/dbus-arch-deps.h")
    SET(CMAKE_LIBRARY_PATH "/usr/lib/x86_64-linux-gnu")
    find_library(HAVE_LIBDBUS NAMES dbus-1)
endif()


#+++++++++++++++++++#
#     build re2c    #
#+++++++++++++++++++#

set(RE2C_V "0.14.3")
set(RE2C_URL "http://sourceforge.net/projects/re2c/files/re2c/0.14.3/re2c-0.14.3.tar.gz")
set(RE2C_PATH ${CMAKE_CURRENT_BINARY_DIR}/re2c-${RE2C_V}.tar.gz)
set(RE2C_DIR ${CMAKE_CURRENT_BINARY_DIR}/re2c-${RE2C_V})

if(NOT EXISTS "${RE2C_DIR}")
    file(DOWNLOAD "${RE2C_URL}" "${RE2C_PATH}" SHOW_PROGRESS)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xvf ${RE2C_PATH})
    execute_process(COMMAND ./configure WORKING_DIRECTORY ${RE2C_DIR})
    execute_process(COMMAND make WORKING_DIRECTORY ${RE2C_DIR})
endif()


#========================#
#     generate Lexer     #
#========================#

set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/nextToken.cpp)
add_custom_command(OUTPUT ${lexer_src}
    COMMAND ${RE2C_DIR}/re2c -c -8 -o ${lexer_src} ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/nextToken.re2c.cpp
)
add_custom_target(gen_lexer DEPENDS ${lexer_src})


#==================================#
#     generate builtin binding     #
#==================================#

set(bind_src ${CMAKE_CURRENT_BINARY_DIR}/bind.cpp)
add_custom_command(OUTPUT ${bind_src}
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tools/gen_binding/gen_binding
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/builtin.h ${bind_src}
)
add_custom_target(gen_bind DEPENDS ${bind_src})
add_dependencies(gen_bind gen_binding)


#=================#
#     libydsh     #
#=================#

SET(BIN_NAME ydsh)

include_directories(src)
include_directories(include)

if(USE_DBUS)
    include_directories(${HAVE_DBUS_H} ${HAVE_DBUS_ARCH_DEPS_H})
endif()

SET(YDSH_SRC
    src/ydsh.cpp
    src/ast/dump.cpp
    src/ast/Node.cpp
    src/ast/TypeToken.cpp
    src/core/DSObject.cpp
    src/core/DSType.cpp
    src/core/FieldHandle.cpp
    src/core/ProcContext.cpp
    src/core/RuntimeContext.cpp
    src/core/SymbolTable.cpp
    src/core/TypeLookupError.cpp
    src/core/TypePool.cpp
    src/exe/Shell.cpp
    src/parser/ErrorListener.cpp
    src/parser/Lexer.cpp
    src/parser/Parser.cpp
    src/parser/TokenKind.cpp
    src/parser/TypeChecker.cpp
    src/parser/TypeCheckError.cpp)

if(USE_DBUS)
    SET(YDSH_SRC ${YDSH_SRC}
        src/dbus/DBusBindImpl.cpp
        src/dbus/DBusUtil.cpp)
endif()


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_binding)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/directive)

set(YDSH_STATIC ydsh_static)
add_library(${YDSH_STATIC} STATIC ${YDSH_SRC} ${lexer_src} ${bind_src})
add_dependencies(${YDSH_STATIC} gen_lexer gen_binding gen_bind)
target_link_libraries(${YDSH_STATIC} ${HAVE_LIBDBUS})


#==============#
#     ydsh     #
#==============#

add_executable(${BIN_NAME} src/main.cpp src/interactive.cpp)
target_link_libraries(${BIN_NAME} ${YDSH_STATIC} ${HAVE_LIBEDIT})

install(TARGETS ${BIN_NAME} RUNTIME DESTINATION bin)


#==================#
#     run_test     #
#==================#

enable_testing()
add_subdirectory(test)
add_custom_target(run_test COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure)


#+++++++++++++++++++++++++#
#     show enviroment     #
#+++++++++++++++++++++++++#

MESSAGE(STATUS "CMAKE_BUILD_TYPE                 = ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "CMAKE_SYSTEM                     = ${CMAKE_SYSTEM}")
MESSAGE(STATUS "CMAKE_C_COMPILER_ID              = ${CMAKE_C_COMPILER_ID}")
MESSAGE(STATUS "CMAKE_C_COMPILER_VERSION         = ${CMAKE_C_COMPILER_VERSION}")
MESSAGE(STATUS "CMAKE_C_COMPILER                 = ${CMAKE_C_COMPILER}")
MESSAGE(STATUS "CMAKE_C_FLAGS                    = ${CMAKE_C_FLAGS}")
MESSAGE(STATUS "CMAKE_C_FLAGS_DEBUG              = ${CMAKE_C_FLAGS_DEBUG}")
MESSAGE(STATUS "CMAKE_C_FLAGS_RELEASE            = ${CMAKE_C_FLAGS_RELEASE}")
MESSAGE(STATUS "CMAKE_C_FLAGS_RELWITHDEBINFO     = ${CMAKE_C_FLAGS_RELWITHDEBINFO}")
MESSAGE(STATUS "CMAKE_CXX_COMPILER_ID            = ${CMAKE_CXX_COMPILER_ID}")
MESSAGE(STATUS "CMAKE_CXX_COMPILER_VERSION       = ${CMAKE_CXX_COMPILER_VERSION}")
MESSAGE(STATUS "CMAKE_CXX_COMPILER               = ${CMAKE_CXX_COMPILER}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS                  = ${CMAKE_CXX_FLAGS}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS_DEBUG            = ${CMAKE_CXX_FLAGS_DEBUG}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS_RELEASE          = ${CMAKE_CXX_FLAGS_RELEASE}")
MESSAGE(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO   = ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
MESSAGE(STATUS "CMAKE_INSTALL_PREFIX             = ${CMAKE_INSTALL_PREFIX}")
