
#===================#
#     directive     #
#===================#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(directive STATIC
        directive.cpp
)
add_dependencies(directive gen_lexer)
target_link_libraries(directive arsh_static platform)