
#=====================#
#     highlighter     #
#=====================#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(highlighter STATIC
        style.cpp
        formatter.cpp
        factory.cpp)
target_link_libraries(highlighter arsh_static)

set(HIGHLIGHTER arcolorize)
add_executable(${HIGHLIGHTER} main.cpp)
target_link_libraries(${HIGHLIGHTER} highlighter arsh_static)

install(TARGETS ${HIGHLIGHTER}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install_symlink_bindir(${HIGHLIGHTER} "dscolorize")