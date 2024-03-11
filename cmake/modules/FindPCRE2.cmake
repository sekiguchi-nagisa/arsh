
# for pcre2 detection

find_path(PCRE2_INCLUDE_DIR NAMES "pcre2.h")
set(CMAKE_INCLUDE_PATH "/usr/local/include;/opt/homebrew/include") # for macOS Homebrew
set(CMAKE_LIBRARY_PATH "/usr/lib/x86_64-linux-gnu;/opt/homebrew/lib")
find_library(PCRE2_LIBRARY NAMES "pcre2-8")

if (PCRE2_INCLUDE_DIR)
    ## detect pcre2 version
    file(STRINGS ${PCRE2_INCLUDE_DIR}/pcre2.h PCRE2_VERSION_MAJOR
            REGEX "#define[ ]+PCRE2_MAJOR[ ]+[0-9]+")
    string(REGEX MATCH " [0-9]+" PCRE2_VERSION_MAJOR ${PCRE2_VERSION_MAJOR})
    string(STRIP "${PCRE2_VERSION_MAJOR}" PCRE2_VERSION_MAJOR)

    file(STRINGS ${PCRE2_INCLUDE_DIR}/pcre2.h PCRE2_VERSION_MINOR
            REGEX "#define[ ]+PCRE2_MINOR[ ]+[0-9]+")
    string(REGEX MATCH " [0-9]+" PCRE2_VERSION_MINOR ${PCRE2_VERSION_MINOR})
    string(STRIP "${PCRE2_VERSION_MINOR}" PCRE2_VERSION_MINOR)

    set(PCRE2_VERSION ${PCRE2_VERSION_MAJOR}.${PCRE2_VERSION_MINOR})
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PCRE2
        REQUIRED_VARS PCRE2_LIBRARY PCRE2_INCLUDE_DIR
        VERSION_VAR PCRE2_VERSION
)
