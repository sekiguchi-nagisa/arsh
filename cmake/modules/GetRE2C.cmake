
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
            GIT_TAG             a1ebb2e1113cbea122cc8c99036d581d708a0aab
            GIT_PROGRESS        1
    )

    set(RE2C_SRC "${re2c_SOURCE_DIR}")
    set(RE2C_BIN "${re2c_BINARY_DIR}/re2c")

    execute_process(COMMAND cmake ${re2c_SOURCE_DIR} WORKING_DIRECTORY ${re2c_BINARY_DIR})
    execute_process(COMMAND make WORKING_DIRECTORY ${re2c_BINARY_DIR})
endmacro()
