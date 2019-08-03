
include(DownloadProject)

macro(getRE2C)
    if (CMAKE_VERSION VERSION_LESS 3.2)
        set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
    else()
        set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
    endif()

    download_project(
            PROJ                re2c
            GIT_REPOSITORY      https://github.com/skvadrik/re2c.git
            GIT_TAG             1.2
            GIT_PROGRESS        1
    )

    set(RE2C_SRC "${re2c_SOURCE_DIR}")
    set(RE2C_BIN "${re2c_BINARY_DIR}/re2c")

    if(NOT EXISTS "${RE2C_SRC}/configure")
        execute_process(COMMAND ./autogen.sh WORKING_DIRECTORY ${RE2C_SRC})
    endif()

    execute_process(COMMAND ${RE2C_SRC}/configure WORKING_DIRECTORY ${re2c_BINARY_DIR})
    execute_process(COMMAND make WORKING_DIRECTORY ${re2c_BINARY_DIR})
endmacro()
