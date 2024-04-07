cmake_minimum_required(VERSION 3.8)

if (POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
endif ()

project(arsh VERSION 0.33.2)
enable_language(CXX)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules")

include(GNUInstallDirs)
include(CheckIPOSupported)
include(Helper)
include(DownloadProject)
include(GenLexer)
include(GetRE2C)


#++++++++++++++++++++++++++#
#     project version      #
#++++++++++++++++++++++++++#

set(ARSH_STABLE_RELEASE on)
set(ARSH_VERSION ${PROJECT_VERSION})
if (NOT ${ARSH_STABLE_RELEASE})
    set(ARSH_VERSION ${ARSH_VERSION}-unstable)
endif ()


#++++++++++++++++++++++++++#
#     set extra option     #
#++++++++++++++++++++++++++#

option(USE_LOGGING "enable internal logging" OFF)
option(USE_SAFE_CAST "check object cast" OFF)
option(FUZZING_BUILD_MODE "enable fuzzing-aware build" OFF)
option(USE_LTO "enable Link Time Optimization" OFF)
option(USE_EXTRA_TEST "enable system extra test cases" OFF)
option(USE_PCRE "enable pcre2 support" ON)
option(BUILD_SHARED_LIB "build shared library" OFF)
option(USE_CTEST "enable CTest" ON)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DBEGIN_MISC_LIB_NAMESPACE_DECL=namespace\ arsh\ {)
add_definitions(-DEND_MISC_LIB_NAMESPACE_DECL=})
add_definitions(-D_GNU_SOURCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall -Wextra")
if (NOT EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-strong")
endif ()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-mismatched-new-delete")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-array-bounds")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-use-after-free")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-warning-option")

if (FUZZING_BUILD_MODE)
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_BUILD_TYPE Debug)
        set(SANITIZER "address,fuzzer-no-link")
        set(BUILD_SHARED_LIB on)
    else ()
        message(FATAL_ERROR "require clang")
    endif ()
endif ()

set(SANITIZER "" CACHE STRING "which sanitizer to use")

if (NOT ("${SANITIZER}" STREQUAL ""))
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O1 -fsanitize=${SANITIZER} -fno-omit-frame-pointer")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
endif ()

string(TOLOWER "${CMAKE_BUILD_TYPE}" lower_type)
if (("${lower_type}" STREQUAL "debug") OR ("${lower_type}" STREQUAL "coverage"))
    set(USE_LOGGING ON)
    set(USE_SAFE_CAST ON)
elseif ("${lower_type}" STREQUAL "release" OR "${lower_type}" STREQUAL "relwithdebinfo")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_definitions(-D_FORTIFY_SOURCE=2)
    endif ()
endif ()


#+++++++++++++++++++++++++++++++++#
#     set coverage build type     #
#+++++++++++++++++++++++++++++++++#

if ("${lower_type}" STREQUAL "coverage")
    add_definitions(-DCODE_COVERAGE=ON)
endif ()
set(CMAKE_CXX_FLAGS_COVERAGE "-g -fprofile-arcs -ftest-coverage")


#+++++++++++++++++++++++++++#
#     show extra option     #
#+++++++++++++++++++++++++++#

show_option(USE_LOGGING)
show_option(USE_SAFE_CAST)
show_option(FUZZING_BUILD_MODE)
show_option(USE_LTO)
show_option(USE_EXTRA_TEST)
show_option(USE_PCRE)
show_option(BUILD_SHARED_LIB)
show_option(USE_CTEST)


#+++++++++++++++++++++++++++#
#     find some command     #
#+++++++++++++++++++++++++++#

find_package(Threads REQUIRED)

check_program(git)


#++++++++++++++++++++++#
#     find libpcre     #
#++++++++++++++++++++++#

if (USE_PCRE)
    find_package(PCRE2 10.30 REQUIRED)
endif ()


#+++++++++++++++++++++++++++#
#     generate config.h     #
#+++++++++++++++++++++++++++#

configure_file(src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src)


#+++++++++++++++++++#
#     find re2c    #
#+++++++++++++++++++#

find_package(RE2C 2.0)
if (NOT ${RE2C_FOUND})
    getRE2C()
endif ()


#========================#
#     generate Lexer     #
#========================#

set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/nextToken.cpp)
add_gen_lexer_target(
        TARGET gen_lexer
        SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/nextToken.re2c.cpp
        OUTPUT_FILE ${lexer_src}
        RE2C_OPTION "-W -Werror -c -8 -s -t ${CMAKE_CURRENT_BINARY_DIR}/src/yycond.h"
)


#==================================#
#     generate builtin binding     #
#==================================#

set(bind_doc ${CMAKE_SOURCE_DIR}/doc/std.md)
add_custom_command(OUTPUT ${bind_doc}
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tools/gen_binding/gen_binding --doc
        ${CMAKE_SOURCE_DIR}/src/builtin.h ${bind_doc}
        MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/src/builtin.h
        DEPENDS gen_binding
)

set(bind_header ${CMAKE_CURRENT_BINARY_DIR}/src/bind.h)
set(bind_src ${CMAKE_CURRENT_BINARY_DIR}/src/bind.cpp)
add_custom_command(OUTPUT ${bind_header} ${bind_src}
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tools/gen_binding/gen_binding
        --header ${bind_header}
        ${CMAKE_SOURCE_DIR}/src/builtin.h ${bind_src}
        MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/src/builtin.h
        DEPENDS gen_binding ${bind_doc}
)


#=======================================#
#     generate embedded script file     #
#=======================================#

set(embed_src ${CMAKE_CURRENT_BINARY_DIR}/src/embed.h)
add_custom_command(OUTPUT ${embed_src}
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/tools/src_to_str/src_to_str
        -f ${CMAKE_CURRENT_SOURCE_DIR}/src/embed.ds
        -o ${embed_src} -v embed_script
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/src/embed.ds
        DEPENDS src_to_str
)


#=================#
#     libarsh     #
#=================#

set(BIN_NAME arsh)

include_directories(src)
include_directories(include)
include_directories(${PCRE2_INCLUDE_DIR})

set(ARSH_SRC
        src/arsh.cpp
        src/node.cpp
        src/object.cpp
        src/type.cpp
        src/cmd.cpp
        src/cmd_desc.cpp
        src/cmd_job.cpp
        src/cmd_read.cpp
        src/cmd_shctl.cpp
        src/cmd_dirs.cpp
        src/cmd_printf.cpp
        src/cmd_test.cpp
        src/core.cpp
        src/scope.cpp
        src/type_pool.cpp
        src/codegen.cpp
        src/vm.cpp
        src/lexer.cpp
        src/parser.cpp
        src/type_checker.cpp
        src/type_checker_cmd.cpp
        src/type_checker_attr.cpp
        src/frontend.cpp
        src/signals.cpp
        src/regex_wrapper.cpp
        src/job.cpp
        src/redir.cpp
        src/state.cpp
        src/complete.cpp
        src/paths.cpp
        src/compiler.cpp
        src/sysconfig.cpp
        src/brace.cpp
        src/line_editor.cpp
        src/line_editor_setting.cpp
        src/highlighter_base.cpp
        src/line_renderer.cpp
        src/keycode.cpp
        src/ordered_map.cpp
        src/arg_parser_base.cpp
        src/arg_parser.cpp
        src/attribute.cpp
        src/line_buffer.cpp
        src/glob.cpp
        src/expand.cpp
        src/pager.cpp
        src/candidates.cpp
        src/format_signature.cpp
        ${lexer_src}
        ${bind_src}
        ${embed_src})


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/src_to_str)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_binding)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/platform)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/directive)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/pid_check)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/process)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/uri)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/json)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/highlighter)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/analyzer)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tools/inspect)


set(CMAKE_MACOSX_RPATH ON)
if (BUILD_SHARED_LIB)
    SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
    SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif ()

add_library(arsh_obj OBJECT ${ARSH_SRC})
add_dependencies(arsh_obj gen_lexer)
set_target_properties(arsh_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(ARSH_STATIC arsh_static)
add_library(${ARSH_STATIC} STATIC $<TARGET_OBJECTS:arsh_obj>)
target_link_libraries(${ARSH_STATIC} ${PCRE2_LIBRARY})

if (BUILD_SHARED_LIB)
    set(ARSH_LIB arsh_lib)
    add_library(${ARSH_LIB} SHARED $<TARGET_OBJECTS:arsh_obj>)
    set_target_properties(${ARSH_LIB} PROPERTIES OUTPUT_NAME arsh)
    target_link_libraries(${ARSH_LIB} ${PCRE2_LIBRARY})
else ()
    set(ARSH_LIB arsh_static)
endif ()

#==============#
#     arsh     #
#==============#

add_executable(${BIN_NAME} src/main.cpp)
target_link_libraries(${BIN_NAME} ${ARSH_LIB})

if (USE_LTO)
    check_ipo_supported(RESULT ipo_supported OUTPUT output)
    if (ipo_supported)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            check_program(lld)
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
            set_property(TARGET ${ARSH_LIB} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_property(TARGET ${BIN_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif ()
    else ()
        message(FATAL_ERROR "IPO is not supported: ${output}")
    endif ()
endif ()

if (BUILD_SHARED_LIB)  # also install shared library / header
    install(TARGETS ${BIN_NAME} ${ARSH_LIB}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
else ()
    install(TARGETS ${BIN_NAME}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()

#install(DIRECTORY etc/ DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR})
install(DIRECTORY share/ DESTINATION ${CMAKE_INSTALL_FULL_DATADIR})
install(PROGRAMS tools/litecheck/litecheck DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/arsh/tools)
install_symlink_bindir(${BIN_NAME} "ydsh")

#++++++++++++++++++++#
#     setup test     #
#++++++++++++++++++++#

if (USE_CTEST)
    enable_testing()
    add_subdirectory(test)
endif ()


#++++++++++++++++#
#     fuzzer     #
#++++++++++++++++#

if (FUZZING_BUILD_MODE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fuzzing)
endif ()


#+++++++++++++++#
#     CPack     #
#+++++++++++++++#

include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "statically typed shell language")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_BUILD_SOURCE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(CPACK_RPM_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}.${CMAKE_SYSTEM_PROCESSOR}.rpm")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
set(CPACK_RPM_PACKAGE_URL "https://github.com/sekiguchi-nagisa/arsh")
set(CPACK_RPM_DEBUGINFO_PACKAGE ON)
set(CPACK_RPM_DEBUGINFO_SINGLE_PACKAGE ON)
set(CPACK_RPM_DEBUGINFO_FILE_NAME
        "${CMAKE_PROJECT_NAME}-debuginfo-${CPACK_PACKAGE_VERSION}.${CMAKE_SYSTEM_PROCESSOR}.rpm")
include(CPack)


#++++++++++++++++++++++++++#
#     show environment     #
#++++++++++++++++++++++++++#

message("")
message("+++++ List of System Configuration +++++")
message(STATUS "CMAKE_BUILD_TYPE                 = ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_SYSTEM                     = ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR           = ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_VERSION                    = ${CMAKE_VERSION}")
message(STATUS "CMAKE_CXX_COMPILER_ID            = ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION       = ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CMAKE_CXX_COMPILER               = ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_FLAGS                  = ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG            = ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE          = ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO   = ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_CXX_FLAGS_MINSIZEREL       = ${CMAKE_CXX_FLAGS_MINSIZEREL}")
message(STATUS "CMAKE_INSTALL_PREFIX             = ${CMAKE_INSTALL_PREFIX}")
message(STATUS "PCRE2_INCLUDE_DIR                = ${PCRE2_INCLUDE_DIR}")
message(STATUS "PCRE2_LIBRARY                    = ${PCRE2_LIBRARY}")
message("")
