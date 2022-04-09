
#=====================#
#     highlighter     #
#=====================#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(highlighter STATIC
        emitter.cpp
        style.cpp
        formatter.cpp
        factory.cpp)
target_link_libraries(highlighter ydsh_static)

add_executable(dscolorize main.cpp)
target_link_libraries(dscolorize highlighter ydsh_static)