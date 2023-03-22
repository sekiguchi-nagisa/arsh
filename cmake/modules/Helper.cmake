
#++++++++++++++++++++++++++++++++++#
#     helper macro or function     #
#++++++++++++++++++++++++++++++++++#

include(CheckTypeSize)

macro(check_program NAME)
    find_program(VAR_${NAME} NAMES ${NAME})
    if (NOT VAR_${NAME})
        message(FATAL_ERROR "not found command - ${NAME}")
    endif ()
    message(STATUS "found command - ${NAME} in ${VAR_${NAME}}")
endmacro()

macro(show_option OPTION)
    message(STATUS "extra option - ${OPTION}=${${OPTION}}")
endmacro()

macro(assert_type_size TYPE SIZE VAR)
    check_type_size(${TYPE} ${VAR})
    message(STATUS "sizeof ${TYPE}: ${${VAR}}")
    if (NOT ("${SIZE}" STREQUAL "${${VAR}}"))
        message(FATAL_ERROR "expect ${SIZE}")
    endif ()
endmacro()