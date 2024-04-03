
# for re2c detection

find_program(RE2C_EXECUTABLE NAMES re2c)

if (RE2C_EXECUTABLE)
    ## detect re2c version
    execute_process(
            COMMAND "${RE2C_EXECUTABLE}" --vernum
            OUTPUT_VARIABLE RE2C_VERNUM_OUT
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    math(EXPR RE2C_VERSION_MAJOR "${RE2C_VERNUM_OUT} / 10000")
    math(EXPR RE2C_VERSION_MINOR "(${RE2C_VERNUM_OUT} - ${RE2C_VERSION_MAJOR} * 10000 ) / 100")
    math(EXPR RE2C_VERSION_PATCH "(${RE2C_VERNUM_OUT} - ${RE2C_VERSION_MAJOR} * 10000 - ${RE2C_VERSION_MINOR} * 100)")

    set(RE2C_VERSION ${RE2C_VERSION_MAJOR}.${RE2C_VERSION_MINOR}.${RE2C_VERSION_PATCH})
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RE2C
        REQUIRED_VARS RE2C_EXECUTABLE
        VERSION_VAR RE2C_VERSION
)