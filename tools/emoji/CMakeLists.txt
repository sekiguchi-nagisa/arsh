#========================#
#     gen_emoji_trie     #
#========================#

add_executable(gen_emoji_trie main.cpp)
target_link_libraries(gen_emoji_trie radix casefold)