cmake_minimum_required(VERSION 3.8)

if (POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
endif ()

project(ydsh VERSION 0.30.0)
enable_language(CXX)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules")

include(GNUInstallDirs)
include(CheckIPOSupported)
include(Helper)
include(DownloadProject)
include(GetRE2C)
include(GenLexer)


#++++++++++++++++++++++++++#
#     project version      #
#++++++++++++++++++++++++++#

set(YDSH_STABLE_RELEASE on)
set(YDSH_VERSION ${PROJECT_VERSION})
if (NOT ${YDSH_STABLE_RELEASE})
    set(YDSH_VERSION ${YDSH_VERSION}-unstable)
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

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DBEGIN_MISC_LIB_NAMESPACE_DECL=namespace\ ydsh\ {)
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
#     build re2c    #
#+++++++++++++++++++#

getRE2C()


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
#     libydsh     #
#=================#

set(BIN_NAME ydsh)

include_directories(src)
include_directories(include)
include_directories(/usr/local/include) # for osx Catalina

set(YDSH_SRC
        src/ydsh.cpp
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
        src/core.cpp
        src/scope.cpp
        src/type_pool.cpp
        src/codegen.cpp
        src/vm.cpp
        src/lexer.cpp
        src/parser.cpp
        src/type_checker.cpp
        src/type_checker_cmd.cpp
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
        src/highlighter_base.cpp
        src/line_renderer.cpp
        src/keycode.cpp
        src/rotate.cpp
        src/ordered_map.cpp
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
SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_library(ydsh_obj OBJECT ${YDSH_SRC})
add_dependencies(ydsh_obj gen_lexer)
set_target_properties(ydsh_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(YDSH_STATIC ydsh_static)
add_library(${YDSH_STATIC} STATIC $<TARGET_OBJECTS:ydsh_obj>)
target_link_libraries(${YDSH_STATIC} ${PCRE2_LIBRARY})

if (BUILD_SHARED_LIB)
    set(YDSH_LIB ydsh_lib)
    add_library(${YDSH_LIB} SHARED $<TARGET_OBJECTS:ydsh_obj>)
    set_target_properties(${YDSH_LIB} PROPERTIES OUTPUT_NAME ydsh)
    target_link_libraries(${YDSH_LIB} ${PCRE2_LIBRARY})
else ()
    set(YDSH_LIB ydsh_static)
endif ()

#==============#
#     ydsh     #
#==============#

add_executable(${BIN_NAME} src/main.cpp)
target_link_libraries(${BIN_NAME} ${YDSH_LIB})

if (USE_LTO)
    check_ipo_supported(RESULT ipo_supported OUTPUT output)
    if (ipo_supported)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            check_program(lld)
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
            set_property(TARGET ${YDSH_LIB} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_property(TARGET ${BIN_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif ()
    else ()
        message(FATAL_ERROR "IPO is not supported: ${output}")
    endif ()
endif ()

if (BUILD_SHARED_LIB)  # also install shared library / header
    install(TARGETS ${BIN_NAME} ${YDSH_LIB}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
else ()
    install(TARGETS ${BIN_NAME}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()

#install(DIRECTORY etc/ DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR})
install(DIRECTORY share/ DESTINATION ${CMAKE_INSTALL_FULL_DATADIR})


#++++++++++++++++++++#
#     setup test     #
#++++++++++++++++++++#

enable_testing()
add_subdirectory(test)


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
set(CPACK_RPM_PACKAGE_URL "https://github.com/sekiguchi-nagisa/ydsh")
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
message("")
