

set(uri_parser_src ${CMAKE_CURRENT_BINARY_DIR}/uri_parser.cpp)
add_gen_lexer_target(
        TARGET      gen_uri_parser
        SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/uri_parser.re2c.cpp
        OUTPUT_FILE ${uri_parser_src}
        RE2C_OPTION "-W -Werror -Wno-nondeterministic-tags -s --tags"
)

#=============#
#     uri     #
#=============#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(uri STATIC uri.cpp ${uri_parser_src})
