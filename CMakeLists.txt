cmake_minimum_required(VERSION 2.8.12)

if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()

project(ydsh)
enable_language(CXX)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules")

include(Helper)
include(DownloadProject)
include(GetRE2C)
include(GenLexer)


#++++++++++++++++++++++++++#
#     project version      #
#++++++++++++++++++++++++++#

set(YDSH_MAJOR_VERSION 0)
set(YDSH_MINOR_VERSION 12)
set(YDSH_PATCH_VERSION 0)
set(YDSH_STABLE_RELEASE off)

set(YDSH_VERSION
    "${YDSH_MAJOR_VERSION}.${YDSH_MINOR_VERSION}.${YDSH_PATCH_VERSION}"
)

if(NOT ${YDSH_STABLE_RELEASE})
    set(YDSH_VERSION ${YDSH_VERSION}-unstable)
endif()


#++++++++++++++++++++++++++#
#     set extra option     #
#++++++++++++++++++++++++++#

option(USE_LOGGING "enable internal logging" OFF)
option(USE_SAFE_CAST "check object cast" OFF)
option(USE_FIXED_TIME "enable fixed time source" OFF)
option(FUZZING_BUILD_MODE "enable fuzzing-aware build" OFF)
option(USE_LTO "enable Link Time Optimization" OFF)

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

add_definitions(-D_GNU_SOURCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall -Wextra -Wno-missing-field-initializers -std=gnu++11")

if(FUZZING_BUILD_MODE)
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_BUILD_TYPE Debug)
        set(SANITIZER "address,fuzzer-no-link")
    else()
        message(FATAL_ERROR "require clang")
    endif()
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" lower_type)
if(("${lower_type}" STREQUAL "debug") OR ("${lower_type}" STREQUAL "coverage"))
    set(USE_LOGGING ON)
    set(USE_SAFE_CAST ON)
    set(USE_FIXED_TIME ON)
elseif("${lower_type}" STREQUAL "release" OR "${lower_type}" STREQUAL "relwithdebinfo")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_definitions(-D_FORTIFY_SOURCE=2)
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto")
endif()


#+++++++++++++++++++++++++++++++++#
#     set coverage build type     #
#+++++++++++++++++++++++++++++++++#

set(SANITIZER "" CACHE STRING "which sanitizer to use")

if(NOT ("${SANITIZER}" STREQUAL ""))
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O1 -fsanitize=${SANITIZER} -fno-omit-frame-pointer")
endif()

set(CMAKE_CXX_FLAGS_COVERAGE "-g -fprofile-arcs -ftest-coverage")


#+++++++++++++++++++++++++++#
#     show extra option     #
#+++++++++++++++++++++++++++#

show_option(USE_LOGGING)
show_option(USE_SAFE_CAST)
show_option(USE_FIXED_TIME)
show_option(FUZZING_BUILD_MODE)
show_option(USE_LTO)


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

getRE2C()


#========================#
#     generate Lexer     #
#========================#

set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/nextToken.cpp)
add_gen_lexer_target(
        TARGET      gen_lexer
        SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/nextToken.re2c.cpp
        OUTPUT_FILE ${lexer_src}
        RE2C_OPTION "-W -Werror -c -8 -s -t ${CMAKE_CURRENT_BINARY_DIR}/src/yycond.h"
)


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
        src/job.cpp
        ${lexer_src}
        ${bind_src})


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/src_to_str)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_binding)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/pid_check)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/process)


set(CMAKE_MACOSX_RPATH ON)
SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_library(ydsh_obj OBJECT ${YDSH_SRC})
add_dependencies(ydsh_obj gen_lexer gen_binding gen_bind embed_script)
set_target_properties(ydsh_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(YDSH_STATIC ydsh_static)
add_library(${YDSH_STATIC} STATIC $<TARGET_OBJECTS:ydsh_obj>)
target_link_libraries(${YDSH_STATIC} ${HAVE_LIBPCRE})

set(YDSH_LIB ydsh_lib)
add_library(${YDSH_LIB} SHARED $<TARGET_OBJECTS:ydsh_obj>)
set_target_properties(${YDSH_LIB} PROPERTIES OUTPUT_NAME ydsh)
target_link_libraries(${YDSH_LIB} ${HAVE_LIBPCRE})


#==============#
#     ydsh     #
#==============#

include_directories(ext/linenoise)
add_executable(${BIN_NAME}
        src/main.cpp
        src/interactive.cpp
        ext/linenoise/linenoise.c)
target_link_libraries(${BIN_NAME} ${YDSH_LIB})

install(TARGETS ${BIN_NAME} ${YDSH_LIB}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
install(DIRECTORY etc/ DESTINATION etc)
install(DIRECTORY include/ DESTINATION include)


#++++++++++++++++++++#
#     setup test     #
#++++++++++++++++++++#

enable_testing()
add_subdirectory(test)


#++++++++++++++++#
#     fuzzer     #
#++++++++++++++++#

if(FUZZING_BUILD_MODE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fuzzing)
endif()


#+++++++++++++++++++++++++#
#     show enviroment     #
#+++++++++++++++++++++++++#

message("")
message("+++++ List of System Configuration +++++")
message(STATUS "CMAKE_BUILD_TYPE                 = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_SYSTEM                     = ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_CXX_COMPILER_ID            = ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION       = ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CMAKE_CXX_COMPILER               = ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_FLAGS                  = ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG            = ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE          = ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO   = ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_INSTALL_PREFIX             = ${CMAKE_INSTALL_PREFIX}")
message("")
