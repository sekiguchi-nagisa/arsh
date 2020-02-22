
#===================#
#     directive     #
#===================#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(directive STATIC
        directive.cpp ${lexer_src}
)
target_link_libraries(directive ${YDSH_STATIC} platform)