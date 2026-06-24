
#========================#
#     generate lexer     #
#========================#

set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/nextToken.cpp)
add_gen_lexer_target(
        TARGET gen_js_lexer
        SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/nextToken.re2c.cpp
        OUTPUT_FILE ${lexer_src}
        RE2C_OPTION "-W -Werror -8 -s"
)

#===============#
#     re262     #
#===============#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(re262_static STATIC
        meta.cpp
        js.cpp
        js_lexer.cpp
        ${lexer_src}
        js_regex.cpp
        harness.cpp
)
target_link_libraries(re262_static regex)

add_executable(re262 main.cpp)
target_link_libraries(re262 re262_static regex)