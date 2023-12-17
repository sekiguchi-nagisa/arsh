
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

macro(install_symlink_bindir old_bin new_bin)
    install(CODE "execute_process(\
        COMMAND ${CMAKE_COMMAND} -E create_symlink \
        ${CMAKE_INSTALL_FULL_BINDIR}/${old_bin} \
        ${CMAKE_INSTALL_FULL_BINDIR}/${new_bin})")
    install(CODE
            "message(STATUS \"Create symlink: ${old_bin} to ${CMAKE_INSTALL_FULL_BINDIR}/${new_bin}\")")
endmacro()