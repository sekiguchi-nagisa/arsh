
include(DownloadProject)

macro(getRE2C)
    if (CMAKE_VERSION VERSION_LESS 3.2)
        set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
    else()
        set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
    endif()

    download_project(
            PROJ                re2c
            SOURCE_DIR          ${CMAKE_SOURCE_DIR}/re2c-source
            DOWNLOAD_DIR        ${CMAKE_SOURCE_DIR}/re2c-download
            BINARY_DIR          ${CMAKE_SOURCE_DIR}/re2c-build
            GIT_REPOSITORY      https://github.com/skvadrik/re2c.git
            GIT_TAG             1.1.1
            GIT_PROGRESS        1
            ${UPDATE_DISCONNECTED_IF_AVAILABLE}
    )

    set(RE2C_BIN "${re2c_BINARY_DIR}/re2c")

    if(NOT EXISTS "${re2c_SOURCE_DIR}/re2c/configure")
        execute_process(COMMAND ./autogen.sh WORKING_DIRECTORY ${re2c_SOURCE_DIR}/re2c)
    endif()

    if(NOT EXISTS "${RE2C_BIN}")
        execute_process(COMMAND ${re2c_SOURCE_DIR}/re2c/configure WORKING_DIRECTORY ${re2c_BINARY_DIR})
        execute_process(COMMAND make WORKING_DIRECTORY ${re2c_BINARY_DIR})
    endif()
endmacro()
