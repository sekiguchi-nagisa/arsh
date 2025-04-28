
include(DownloadProject)
include(Helper)

macro(getRE2C)
    download_project(
            PROJ re2c
            GIT_REPOSITORY https://github.com/skvadrik/re2c.git
            GIT_TAG 7bc7c1980a1a8142402574bbfb5ebe5fe2d995a9 # 4.2
            GIT_PROGRESS 1
            UPDATE_DISCONNECTED 1
    )

    set(RE2C_SRC "${re2c_SOURCE_DIR}")
    set(RE2C_EXECUTABLE "${re2c_BINARY_DIR}/re2c")

    execute_process(COMMAND ${CMAKE_COMMAND} -DCMAKE_CXX_FLAGS=-D_GNU_SOURCE -G "${CMAKE_GENERATOR}" ${RE2C_SRC} WORKING_DIRECTORY ${re2c_BINARY_DIR})
    execute_process(COMMAND ${CMAKE_COMMAND} --build . WORKING_DIRECTORY ${re2c_BINARY_DIR})

    if (EXISTS "${RE2C_EXECUTABLE}")
        message(STATUS "complete build re2c in ${RE2C_EXECUTABLE}")
    else ()
        message(FATAL_ERROR "re2c is not found")
    endif ()

endmacro()
