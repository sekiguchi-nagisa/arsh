cmake_minimum_required(VERSION 2.8)

project(ydsh)

#++++++++++++++++++++++#
#     helper macro     #
#++++++++++++++++++++++#

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


#++++++++++++++++++++++++++#
#     project version      #
#++++++++++++++++++++++++++#

set(YDSH_MAJOR_VERSION 0)
set(YDSH_MINOR_VERSION 3)
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

set(CMAKE_VERBOSE_MAKEFILE 1)
if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -std=c++11")


#++++++++++++++++++++++++++#
#     set extra option     #
#++++++++++++++++++++++++++#

option(USE_LOGGING "enable internal logging" OFF)
show_option(USE_LOGGING)

option(USE_DBUS "support D-Bus" ON)
if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
    set(USE_DBUS OFF)
endif()
show_option(USE_DBUS)

option(USE_IFACE_GEN "D-Bus interface generater" ON)
show_option(USE_IFACE_GEN)


#++++++++++++++++++++++#
#     find libedit     #
#++++++++++++++++++++++#

check_header(HAVE_HISTEDIT_H "histedit.h")
find_library(HAVE_LIBEDIT NAMES edit)


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


#+++++++++++++++++++++++++++#
#     generate config.h     #
#+++++++++++++++++++++++++++#

configure_file(src/config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/src/config.h)


#+++++++++++++++++++#
#     build re2c    #
#+++++++++++++++++++#

set(RE2C_V "0.14.3")
set(RE2C_URL "http://sourceforge.net/projects/re2c/files/re2c/${RE2C_V}/re2c-${RE2C_V}.tar.gz")
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


#=======================================#
#     generate embedded script file     #
#=======================================#

set(embed_src ${CMAKE_CURRENT_SOURCE_DIR}/src/embed.h)
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
    src/ast/dump.cpp
    src/ast/Node.cpp
    src/ast/TypeToken.cpp
    src/core/DSObject.cpp
    src/core/DSType.cpp
    src/core/FieldHandle.cpp
    src/core/ProcInvoker.cpp
    src/core/RuntimeContext.cpp
    src/core/SymbolTable.cpp
    src/core/TypeLookupError.cpp
    src/core/TypePool.cpp
    src/core/ErrorListener.cpp
    src/parser/Lexer.cpp
    src/parser/Parser.cpp
    src/parser/TokenKind.cpp
    src/parser/NodeVerifier.cpp
    src/parser/TypeChecker.cpp
    src/parser/TypeCheckError.cpp)

if(USE_DBUS)
    set(YDSH_SRC ${YDSH_SRC}
        src/dbus/DBusBindImpl.cpp
        src/dbus/DBusUtil.cpp)
endif()


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/src_to_str)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_binding)

set(YDSH_STATIC ydsh_static)
add_library(${YDSH_STATIC} STATIC ${YDSH_SRC} ${lexer_src} ${bind_src})
add_dependencies(${YDSH_STATIC} gen_lexer gen_binding gen_bind embed_script)
target_link_libraries(${YDSH_STATIC} ${HAVE_LIBDBUS})


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/directive)

if(USE_IFACE_GEN)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_iface)
endif()


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
