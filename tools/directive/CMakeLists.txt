#==================================#
#     generate directive lexer     #
#==================================#

set(dLexer_src ${CMAKE_CURRENT_BINARY_DIR}/nextToken.cpp)
add_custom_command(OUTPUT ${dLexer_src}
    COMMAND ${RE2C_DIR}/re2c -W -Werror -8 -o ${dLexer_src} ${CMAKE_CURRENT_SOURCE_DIR}/nextToken.re2c.cpp
)
add_custom_target(gen_directive_lexer DEPENDS ${dLexer_src})


#===================#
#     directive     #
#===================#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(directive STATIC
    directive.cpp
    directive_lexer.cpp
    ${dLexer_src}
)
add_dependencies(directive gen_directive_lexer)
target_link_libraries(directive ${YDSH_STATIC})