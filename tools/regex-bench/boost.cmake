# Support for benchmarking the Boost.Regex engine with rust-leipzig/regex-performance.
#
# This file is included from src/CMakeLists.txt when INCLUDE_BOOST is not "disabled".
# Boost has to be located explicitly because the upstream harness only assumes a system
# installation. Point BOOST_ROOT (or BOOST_INCLUDEDIR/BOOST_LIBRARYDIR) at the Boost tree
# to use a custom build.

set(BOOST_ROOT "" CACHE PATH "path to the Boost installation")

find_package(Boost REQUIRED COMPONENTS regex)

if(Boost_FOUND)
    message("-- Found Boost: ${Boost_INCLUDE_DIRS} (version ${Boost_VERSION})")
    include_directories(${Boost_INCLUDE_DIRS})
    # Boost::regex is an imported target, so link it directly instead of relying on
    # link_directories() + the plain library name.
    list(APPEND REGEX_ENGINES Boost::regex)
endif()
