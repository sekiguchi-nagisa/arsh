

function(check_pcre2_version LEAST_VERSION HAVE_PCRE2_H HAVE_LIBPCRE2)
    set(SRC_CONTENT "
        #include <stdio.h>

        #define PCRE2_CODE_UNIT_WIDTH 8
        #include <pcre2.h>

        int main(void) {
            char v[256];
            pcre2_config(PCRE2_CONFIG_VERSION, v);
            printf(\"%s\\n\", v);
            return 0;
        }
    ")

    file(WRITE "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/pcre2_version.c" "${SRC_CONTENT}\n")

    set(GET_PCRE2_VERSION "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/pcre2_version")

    try_compile(RESULT
            ${CMAKE_BINARY_DIR}
            ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/pcre2_version.c
            COMPILE_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS}
            ${CHECK_CXX_SOURCE_COMPILES_ADD_LINK_OPTIONS}
            ${CHECK_CXX_SOURCE_COMPILES_ADD_LIBRARIES}
            LINK_LIBRARIES ${HAVE_LIBPCRE2}
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${HAVE_PCRE2_H}"
            COPY_FILE ${GET_PCRE2_VERSION})

    if(NOT RESULT)
        message(FATAL_ERROR "compilation failed")
    endif()

    execute_process(COMMAND ${GET_PCRE2_VERSION} OUTPUT_VARIABLE PCRE2_VERSION)
    string(REPLACE " " ";" PCRE2_VERSION ${PCRE2_VERSION})
    list(GET PCRE2_VERSION 0 PCRE2_VERSION)
    if(PCRE2_VERSION VERSION_LESS ${LEAST_VERSION})
        message(FATAL_ERROR "require PCRE2 ${LEAST_VERSION} or later")
    else()
        message(STATUS "PCRE2 version - ${PCRE2_VERSION}")
    endif()
endfunction()