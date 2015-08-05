#+++++++++++++++++++++++#
#     find xerces-c     #
#+++++++++++++++++++++++#

check_header(HAVE_XERCESC_H "xercesc/dom/DOM.hpp")
find_library(HAVE_LIBXERCES_C NAMES xerces-c)

#===================#
#     gen_iface     #
#===================#

add_executable(gen_iface main.cpp)
target_link_libraries(gen_iface ${HAVE_LIBXERCES_C})