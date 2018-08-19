#========================#
#     generate lexer     #
#========================#

set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/DescLexer.cpp)
add_gen_lexer_target(
        TARGET      gen_desc_lexer
        SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/DescLexer.re2c.cpp
        OUTPUT_FILE ${lexer_src}
        RE2C_OPTION "-W -Werror -8 -s"
)


#=====================#
#     gen_binding     #
#=====================#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(gen_binding ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${lexer_src})
add_dependencies(gen_binding gen_desc_lexer)