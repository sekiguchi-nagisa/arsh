#
# USAGE:
#
#   add_gen_lexer_target(TARGET        gen_lexer
#                        SOURCE_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/nextToken.re2c
#                        OUTPUT_FILE   ${CMAKE_CURRENT_BINARY_DIR}/nextToken.cpp
#                        RE2C_OPTION   "-W -Werror -c -8 -s -t ${CMAKE_CURRENT_BINARY_DIR}/src/yycond.h"
#   )
#

include(CMakeParseArguments)

function(add_gen_lexer_target)
    set(single_args
            TARGET
            SOURCE_FILE
            OUTPUT_FILE
            RE2C_OPTION)

    cmake_parse_arguments(GL_ARGS "" "${single_args}" "" ${ARGN})

    # check arguments
    if (NOT GL_ARGS_TARGET)
        message(FATAL_ERROR "require TARGET")
    endif ()

    if (NOT GL_ARGS_SOURCE_FILE)
        message(FATAL_ERROR "require SOURCE_FILE")
    endif ()

    if (NOT GL_ARGS_OUTPUT_FILE)
        message(FATAL_ERROR "require OUTPUT_FILE")
    endif ()

    set(lexer_src ${GL_ARGS_OUTPUT_FILE})
    set(option ${GL_ARGS_RE2C_OPTION})
    separate_arguments(option)

    add_custom_command(OUTPUT ${lexer_src}
            COMMAND ${RE2C_BIN} ${option} -o ${lexer_src} ${GL_ARGS_SOURCE_FILE}
            MAIN_DEPENDENCY ${GL_ARGS_SOURCE_FILE})
    add_custom_target(${GL_ARGS_TARGET} DEPENDS ${lexer_src})
endfunction()