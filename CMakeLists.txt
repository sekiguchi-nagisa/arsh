cmake_minimum_required(VERSION 2.8)

include(CheckTypeSize)

project(ydsh)

#++++++++++++++++++++++++++++++++++#
#     helper macro or function     #
#++++++++++++++++++++++++++++++++++#

macro(check_header VAR NAME)
    find_path(${VAR} ${NAME})
    if(NOT ${VAR})
        message(FATAL_ERROR "not found header file - ${NAME}")
    endif()
    message(STATUS "found header file - ${NAME} in ${${VAR}}")
endmacro()

macro(show_option OPTION)
    message(STATUS "extra option - ${OPTION}=${${OPTION}}")
endmacro()

macro(assert_type_size TYPE SIZE VAR)
    check_type_size(${TYPE} ${VAR})
    message(STATUS "sizeof ${TYPE}: ${${VAR}}")
    if(NOT ("${SIZE}" STREQUAL "${${VAR}}"))
        message(FATAL_ERROR "expect ${SIZE}")
    endif()
endmacro()


#++++++++++++++++++++++++++#
#     project version      #
#++++++++++++++++++++++++++#

set(YDSH_MAJOR_VERSION 0)
set(YDSH_MINOR_VERSION 4)
set(YDSH_PATCH_VERSION 0)
set(YDSH_STABLE_RELEASE off)

set(YDSH_VERSION
    "${YDSH_MAJOR_VERSION}.${YDSH_MINOR_VERSION}.${YDSH_PATCH_VERSION}"
)

if(NOT ${YDSH_STABLE_RELEASE})
    set(YDSH_VERSION ${YDSH_VERSION}-unstable)
endif()


#++++++++++++++++++++++++++++#
#     set some variable      #
#++++++++++++++++++++++++++++#

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -std=c++11")


#+++++++++++++++++++++++++++++++++#
#     set coverage build type     #
#+++++++++++++++++++++++++++++++++#

set(CMAKE_C_FLAGS_COVERAGE "${CMAKE_C_FLAGS_DEBUG} -fprofile-arcs -ftest-coverage")
set(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_DEBUG} -fprofile-arcs -ftest-coverage")


#++++++++++++++++++++++++++#
#     set extra option     #
#++++++++++++++++++++++++++#

option(USE_DBUS "support D-Bus" ON)
if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
    set(USE_DBUS OFF)
endif()
show_option(USE_DBUS)

option(USE_LOGGING "enable internal logging" OFF)
option(USE_SAFE_CAST "check object cast" OFF)
option(USE_FIXED_TIME "enable fixed time source" OFF)

string(TOLOWER "${CMAKE_BUILD_TYPE}" lower_type)
if(("${lower_type}" STREQUAL "debug") OR ("${lower_type}" STREQUAL "coverage"))
    set(USE_LOGGING ON)
    set(USE_SAFE_CAST ON)
    set(USE_FIXED_TIME ON)
endif()

show_option(USE_LOGGING)
show_option(USE_SAFE_CAST)
show_option(USE_FIXED_TIME)


#++++++++++++++++++++++++++++#
#     check integer size     #
#++++++++++++++++++++++++++++#

assert_type_size(char 1 SIZEOF_CHAR)
assert_type_size(short 2 SIZEOF_SHORT)
assert_type_size(int 4 SIZEOF_INT)
assert_type_size(long 8 SIZEOF_LONG)
assert_type_size("void *" 8 SIZEOF_PTR)


#++++++++++++++++++++++#
#     find libdbus     #
#++++++++++++++++++++++#

if(USE_DBUS)
    set(CMAKE_INCLUDE_PATH
        "/usr/include/dbus-1.0"
        "/usr/lib64/dbus-1.0/include"
        "/usr/lib/dbus-1.0/include"
        "/usr/lib/x86_64-linux-gnu/dbus-1.0/include"
    )
    check_header(HAVE_DBUS_H "dbus/dbus.h")
    check_header(HAVE_DBUS_ARCH_DEPS_H "dbus/dbus-arch-deps.h")
    set(CMAKE_LIBRARY_PATH "/usr/lib/x86_64-linux-gnu")
    find_library(HAVE_LIBDBUS NAMES dbus-1)
endif()

#++++++++++++++++++++++++#
#     init submodule     #
#++++++++++++++++++++++++#

execute_process(COMMAND git submodule update --init)


#+++++++++++++++++++++++++++#
#     generate config.h     #
#+++++++++++++++++++++++++++#

configure_file(src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src)


#+++++++++++++++++++#
#     build re2c    #
#+++++++++++++++++++#

set(RE2C_V "0.16")
set(RE2C_URL "https://github.com/skvadrik/re2c/releases/download/${RE2C_V}/re2c-${RE2C_V}.tar.gz")
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
    COMMAND ${RE2C_DIR}/re2c -W -Werror -c -8
            -s -t ${CMAKE_CURRENT_BINARY_DIR}/src/yycond.h -o ${lexer_src}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/parser/nextToken.re2c.cpp
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


#=======================================#
#     generate embedded script file     #
#=======================================#

set(embed_src ${CMAKE_CURRENT_BINARY_DIR}/src/embed.h)
add_custom_command(OUTPUT ${embed_src}
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tools/src_to_str/src_to_str
            -f ${CMAKE_CURRENT_SOURCE_DIR}/src/embed.ds
            -o ${embed_src} -v embed_script)
add_custom_target(embed_script DEPENDS ${embed_src})
add_dependencies(embed_script src_to_str)


#=================#
#     libydsh     #
#=================#

set(BIN_NAME ydsh)

include_directories(src)
include_directories(include)

if(USE_DBUS)
    include_directories(${HAVE_DBUS_H} ${HAVE_DBUS_ARCH_DEPS_H})
endif()

set(YDSH_SRC
        src/ydsh.cpp
        src/ast/node_dumper.cpp
        src/ast/node.cpp
        src/core/object.cpp
        src/core/type.cpp
        src/core/handle.cpp
        src/core/proc.cpp
        src/core/context.cpp
        src/core/symbol_table.cpp
        src/core/type_pool.cpp
        src/core/diagnosis.cpp
        src/parser/lexer.cpp
        src/parser/parser.cpp
        src/parser/token_kind.cpp
        src/parser/type_checker.cpp)

if(USE_DBUS)
    set(YDSH_SRC ${YDSH_SRC}
            src/dbus/dbus_bind_impl.cpp
            src/dbus/dbus_util.cpp)
endif()


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/src_to_str)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_binding)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_iface)

set(YDSH_STATIC ydsh_static)
add_library(${YDSH_STATIC} STATIC ${YDSH_SRC} ${lexer_src} ${bind_src})
add_dependencies(${YDSH_STATIC} gen_lexer gen_binding gen_bind embed_script)
target_link_libraries(${YDSH_STATIC} ${HAVE_LIBDBUS})


#==============#
#     ydsh     #
#==============#

include_directories(lib/linenoise)
add_executable(${BIN_NAME}
        src/main.cpp
        src/interactive.cpp
        lib/linenoise/linenoise.c)
target_link_libraries(${BIN_NAME} ${YDSH_STATIC})

install(TARGETS ${BIN_NAME} RUNTIME DESTINATION bin)


#++++++++++++++++++++#
#     setup test     #
#++++++++++++++++++++#

enable_testing()
add_subdirectory(test)


#+++++++++++++++++++++++++#
#     show enviroment     #
#+++++++++++++++++++++++++#

message("")
message("+++++ List of System Configuration +++++")
message(STATUS "CMAKE_BUILD_TYPE                 = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_SYSTEM                     = ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_C_COMPILER_ID              = ${CMAKE_C_COMPILER_ID}")
message(STATUS "CMAKE_C_COMPILER_VERSION         = ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "CMAKE_C_COMPILER                 = ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_C_FLAGS                    = ${CMAKE_C_FLAGS}")
message(STATUS "CMAKE_C_FLAGS_DEBUG              = ${CMAKE_C_FLAGS_DEBUG}")
message(STATUS "CMAKE_C_FLAGS_RELEASE            = ${CMAKE_C_FLAGS_RELEASE}")
message(STATUS "CMAKE_C_FLAGS_RELWITHDEBINFO     = ${CMAKE_C_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_CXX_COMPILER_ID            = ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION       = ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CMAKE_CXX_COMPILER               = ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_FLAGS                  = ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG            = ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE          = ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO   = ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_INSTALL_PREFIX             = ${CMAKE_INSTALL_PREFIX}")
message("")
