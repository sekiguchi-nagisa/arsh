
include(DownloadProject)
include(Helper)

macro(getRE2C)
    if (CMAKE_VERSION VERSION_LESS 3.2)
        set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
    else ()
        set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
    endif ()

    if (CMAKE_VERSION VERSION_LESS 3.12)
        set(BUILD_RE2C_WITH_CMAKE OFF)
        check_program(autoreconf)
        check_program(aclocal)
    else ()
        set(BUILD_RE2C_WITH_CMAKE ON)
    endif ()

    download_project(
            PROJ re2c
            GIT_REPOSITORY https://github.com/skvadrik/re2c.git
            GIT_TAG 4.0
            GIT_PROGRESS 1
            ${UPDATE_DISCONNECTED_IF_AVAILABLE}
    )

    set(RE2C_SRC "${re2c_SOURCE_DIR}")
    set(RE2C_EXECUTABLE "${re2c_BINARY_DIR}/re2c")

    if (BUILD_RE2C_WITH_CMAKE)
        execute_process(COMMAND ${CMAKE_COMMAND} -DCMAKE_CXX_FLAGS=-D_GNU_SOURCE -G "${CMAKE_GENERATOR}" ${RE2C_SRC} WORKING_DIRECTORY ${re2c_BINARY_DIR})
        execute_process(COMMAND ${CMAKE_COMMAND} --build . WORKING_DIRECTORY ${re2c_BINARY_DIR})
    else ()
        if (NOT EXISTS "${RE2C_SRC}/configure")
            execute_process(COMMAND ./autogen.sh WORKING_DIRECTORY ${RE2C_SRC})
        endif ()

        execute_process(COMMAND ${RE2C_SRC}/configure WORKING_DIRECTORY ${re2c_BINARY_DIR})
        execute_process(COMMAND make WORKING_DIRECTORY ${re2c_BINARY_DIR})
    endif ()

    if (EXISTS "${RE2C_EXECUTABLE}")
        message(STATUS "complete build re2c in ${RE2C_EXECUTABLE}")
    else ()
        message(FATAL_ERROR "re2c is not found")
    endif ()

endmacro()
