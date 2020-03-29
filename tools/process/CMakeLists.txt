
#=================#
#     process     #
#=================#

set(ansi_parser_src ${CMAKE_CURRENT_BINARY_DIR}/ansi.cpp)
add_gen_lexer_target(
        TARGET      gen_ansi_parser
        SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/ansi.re2c.cpp
        OUTPUT_FILE ${ansi_parser_src}
        RE2C_OPTION "-W -Werror -s -8 -f"
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(process STATIC process.cpp ${ansi_parser_src})