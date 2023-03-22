
# for pcre2 detection

include(Helper)

find_path(PCRE2_INCLUDE_DIR "pcre2.h")
set(CMAKE_LIBRARY_PATH "/usr/lib/x86_64-linux-gnu")
find_library(PCRE2_LIBRARY NAMES pcre2-8)

if (PCRE2_INCLUDE_DIR AND PCRE2_LIBRARY)
    ## detect pcre2 version

endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PCRE2
        REQUIRED_VARS PCRE2_LIBRARY PCRE2_INCLUDE_DIR
        #        VERSION_VAR PCRE2_VERSION
        )
