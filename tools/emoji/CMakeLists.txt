#========================#
#     gen_emoji_trie     #
#========================#

add_library(emoji_trie STATIC emoji_trie.cpp)

add_executable(gen_emoji_trie main.cpp)
target_link_libraries(gen_emoji_trie emoji_trie)