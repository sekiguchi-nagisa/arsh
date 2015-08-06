#++++++++++++++++++++++#
#     find libxml2     #
#++++++++++++++++++++++#

set(CMAKE_INCLUDE_PATH "/usr/include/libxml2")

check_header(HAVE_XML2_H "libxml/parser.h")
find_library(HAVE_LIBXML2 NAMES xml2)
include_directories(${HAVE_XML2_H})


#===================#
#     gen_iface     #
#===================#

add_executable(gen_iface main.cpp)
target_link_libraries(gen_iface ${HAVE_LIBXML2})