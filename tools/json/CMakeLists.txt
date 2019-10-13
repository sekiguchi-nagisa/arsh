
#========================#
#     generate lexer     #
#========================#

set(lexer_src ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)
add_gen_lexer_target(
        TARGET      gen_json_lexer
        SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/lexer.re2c.cpp
        OUTPUT_FILE ${lexer_src}
        RE2C_OPTION "-W -Werror -8 -s"
)

#==============#
#     json     #
#==============#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(json STATIC json.cpp ${lexer_src})


#=================#
#     jsonrpc     #
#=================#

add_library(jsonrpc STATIC jsonrpc.cpp)
target_link_libraries(jsonrpc json)
