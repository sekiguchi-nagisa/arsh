#========================#
#     generate lexer     #
#========================#

set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/DescLexer.cpp)
add_custom_command(OUTPUT ${lexer_src}
    COMMAND ${RE2C_BIN} -W -Werror -8 -s -o ${lexer_src} ${CMAKE_CURRENT_SOURCE_DIR}/DescLexer.re2c.cpp
)
add_custom_target(gen_desc_lexer DEPENDS ${lexer_src})


#=====================#
#     gen_binding     #
#=====================#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(gen_binding ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${lexer_src})
add_dependencies(gen_binding gen_desc_lexer)