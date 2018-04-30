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

macro(check_library VAR NAME)
    find_library(${VAR} NAMES ${NAME})
    if(NOT ${VAR})
        message(FATAL_ERROR "not found library - ${NAME}")
    endif()
    message(STATUS "found library - ${NAME} in ${${VAR}}")
endmacro()

macro(check_program NAME)
    find_program(VAR_${NAME} NAMES ${NAME})
    if(NOT VAR_${NAME})
        message(FATAL_ERROR "not found command - ${NAME}")
    endif()
    message(STATUS "found command - ${NAME} in ${VAR_${NAME}}")
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
set(YDSH_MINOR_VERSION 10)
set(YDSH_PATCH_VERSION 0)
set(YDSH_STABLE_RELEASE on)

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

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wall -Wextra -Wno-missing-field-initializers")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall -Wextra -Wno-missing-field-initializers -std=gnu++11")


#++++++++++++++++++++++++++#
#     set extra option     #
#++++++++++++++++++++++++++#

option(USE_DBUS "support D-Bus" ON)
if(NOT ("${CMAKE_SYSTEM_NAME}" MATCHES "Linux"))
    set(USE_DBUS OFF)
endif()
show_option(USE_DBUS)

option(USE_LOGGING "enable internal logging" OFF)
option(USE_SAFE_CAST "check object cast" OFF)
option(USE_FIXED_TIME "enable fixed time source" OFF)
option(FUZZING_BUILD_MODE "enable fuzzing-aware build" OFF)

if(FUZZING_BUILD_MODE)
    set(CMAKE_BUILD_TYPE Debug)
    set(SANITIZER "address")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" lower_type)
if(("${lower_type}" STREQUAL "debug") OR ("${lower_type}" STREQUAL "coverage"))
    set(USE_LOGGING ON)
    set(USE_SAFE_CAST ON)
    set(USE_FIXED_TIME ON)
endif()

show_option(USE_LOGGING)
show_option(USE_SAFE_CAST)
show_option(USE_FIXED_TIME)
show_option(FUZZING_BUILD_MODE)


#+++++++++++++++++++++++++++++++++#
#     set coverage build type     #
#+++++++++++++++++++++++++++++++++#

set(SANITIZER "" CACHE STRING "which sanitizer to use")

if(NOT ("${SANITIZER}" STREQUAL ""))
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O1 -fsanitize=${SANITIZER} -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O1 -fsanitize=${SANITIZER} -fno-omit-frame-pointer")
endif()

set(CMAKE_C_FLAGS_COVERAGE "-g -fprofile-arcs -ftest-coverage")
set(CMAKE_CXX_FLAGS_COVERAGE "-g -fprofile-arcs -ftest-coverage")


#++++++++++++++++++++++++++++#
#     check integer size     #
#++++++++++++++++++++++++++++#

assert_type_size(char 1 SIZEOF_CHAR)
assert_type_size(short 2 SIZEOF_SHORT)
assert_type_size(int 4 SIZEOF_INT)
assert_type_size(long 8 SIZEOF_LONG)
assert_type_size("void *" 8 SIZEOF_PTR)


#+++++++++++++++++++++++++++#
#     find some command     #
#+++++++++++++++++++++++++++#

check_program(git)
check_program(autoreconf)
check_program(aclocal)


#++++++++++++++++++++++#
#     find libpcre     #
#++++++++++++++++++++++#

check_header(HAVE_PCRE_H "pcre.h")
set(CMAKE_LIBRARY_PATH "/usr/lib/x86_64-linux-gnu")
check_library(HAVE_LIBPCRE "pcre")


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
    check_library(HAVE_LIBDBUS "dbus-1")
else()
    unset(HAVE_LIBDBUS CACHE)
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

set(RE2C_SRC_DIR ${CMAKE_SOURCE_DIR}/ext/re2c/re2c)
set(RE2C_DIR ${CMAKE_SOURCE_DIR}/build-re2c)

if(NOT EXISTS "${RE2C_DIR}")
    file(MAKE_DIRECTORY ${RE2C_DIR})
endif()

 if(NOT EXISTS "${RE2C_SRC_DIR}/configure")
    execute_process(COMMAND ./autogen.sh WORKING_DIRECTORY ${RE2C_SRC_DIR})
 endif()

execute_process(COMMAND ${RE2C_SRC_DIR}/configure WORKING_DIRECTORY ${RE2C_DIR})
execute_process(COMMAND make WORKING_DIRECTORY ${RE2C_DIR})

#========================#
#     generate Lexer     #
#========================#

set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/nextToken.cpp)
add_custom_command(OUTPUT ${lexer_src}
    COMMAND ${RE2C_DIR}/re2c -W -Werror -c -8
            -s -t ${CMAKE_CURRENT_BINARY_DIR}/src/yycond.h -o ${lexer_src}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/nextToken.re2c.cpp
)
add_custom_target(gen_lexer DEPENDS ${lexer_src})


#==================================#
#     generate builtin binding     #
#==================================#

set(bind_src ${CMAKE_CURRENT_BINARY_DIR}/src/bind.h)
add_custom_command(OUTPUT ${bind_src}
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tools/gen_binding/gen_binding
        ${CMAKE_SOURCE_DIR}/src/builtin.h ${bind_src}
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
        src/node.cpp
        src/object.cpp
        src/type.cpp
        src/handle.cpp
        src/cmd.cpp
        src/core.cpp
        src/symbol_table.cpp
        src/codegen.cpp
        src/vm.cpp
        src/lexer.cpp
        src/parser.cpp
        src/type_checker.cpp
        src/frontend.cpp
        src/signals.cpp
        src/job.cpp)

if(USE_DBUS)
    set(YDSH_SRC ${YDSH_SRC}
            src/dbus/dbus_bind.cpp
            src/dbus/dbus_util.cpp)
endif()


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/src_to_str)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_binding)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_iface)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/pid_check)

set(GTEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/gtest)
add_subdirectory(${GTEST_DIR})

add_definitions(-D_GNU_SOURCE)
if("${CMAKE_BUILD_TYPE}" STREQUAL "RELEASE" OR "${CMAKE_BUILD_TYPE}" STREQUAL "RELWITHDEBINFO")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_definitions(-D_FORTIFY_SOURCE=2)
    endif()
endif()

set(YDSH_STATIC ydsh_static)
add_library(${YDSH_STATIC} STATIC ${YDSH_SRC} ${lexer_src} ${bind_src})
add_dependencies(${YDSH_STATIC} gen_lexer gen_binding gen_bind embed_script)
target_link_libraries(${YDSH_STATIC} ${HAVE_LIBDBUS} ${HAVE_LIBPCRE})


#==============#
#     ydsh     #
#==============#

include_directories(ext/linenoise)
add_executable(${BIN_NAME}
        src/main.cpp
        src/interactive.cpp
        ext/linenoise/linenoise.c)
target_link_libraries(${BIN_NAME} ${YDSH_STATIC})

install(TARGETS ${BIN_NAME} RUNTIME DESTINATION bin)


#++++++++++++++++#
#     fuzzer     #
#++++++++++++++++#

if(FUZZING_BUILD_MODE)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize-coverage=trace-pc-guard")
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fuzzing)
    else()
        message(FATAL_ERROR "require clang")
    endif()
endif()


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
