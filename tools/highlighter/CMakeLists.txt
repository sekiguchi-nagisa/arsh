
#=====================#
#     highlighter     #
#=====================#


include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(highlighter STATIC emitter.cpp)
target_link_libraries(highlighter ydsh_static)