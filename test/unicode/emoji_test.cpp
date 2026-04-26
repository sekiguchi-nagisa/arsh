#include "../test_common.h"

#include <unicode/property.h>
#include <unicode/radix_tree.h>

#include "../tools/emoji/code_pointer_helper.hpp"

using namespace arsh;
using namespace arsh::ucp;

struct EMOJI_SEQ_ENTRY {
  RGIEmojiSeq property;
  CodePointArray codes;
};

#include "../tools/emoji/emoji_seq.in"

TEST(EmojiTrieTest, all) {
  RadixTree tree;

  // add all
  for (auto &[p, codes] : emoji_seq_table) {
    std::string str = codes.toUTF8();
    ASSERT_EQ(RadixTree::AddStatus::OK, tree.add(str, toUnderlying(p)));
    ASSERT_EQ(toUnderlying(p), tree.find(str));
  }

  // re-lookup
  for (auto &[p, codes] : emoji_seq_table) {
    std::string str = codes.toUTF8();
    ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add(str, toUnderlying(p)));
    ASSERT_EQ(toUnderlying(p), tree.find(str));
  }

  ASSERT_EQ(toUnderlying(RGIEmojiSeq::None), tree.find("AAA"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::None), tree.find(""));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::None), tree.find("123"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::None), tree.find("あああ"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::Basic_Emoji), tree.find("⚡"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::Basic_Emoji), tree.find("🪾"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::Basic_Emoji), tree.find("↕️"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::Basic_Emoji), tree.find("❤️"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::Basic_Emoji), tree.find("🕷️"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::Emoji_Keycap_Sequence), tree.find("#️⃣"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::Emoji_Keycap_Sequence), tree.find("7️⃣"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_Flag_Sequence), tree.find("🇧🇬"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_Flag_Sequence), tree.find("🇯🇵"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::None),
            tree.find("🇯")); // single regional indicator is not emoji seq
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::None), tree.find("🇯🇯")); // JJ is not defined
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_Tag_Sequence),
            tree.find("🏴󠁧󠁢󠁥󠁮󠁧󠁿"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_Modifier_Sequence), tree.find("✌🏼"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_Modifier_Sequence), tree.find("👦🏽"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_Modifier_Sequence), tree.find("🧓🏻"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence),
            tree.find("👨🏻‍❤️‍💋‍👨🏻"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence), tree.find("👨‍👧"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence),
            tree.find("👨🏽‍❤️‍💋‍👨🏾"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence),
            tree.find("👩🏼‍🤝‍👩🏽"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence), tree.find("🫱🏼‍🫲🏻"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence), tree.find("⛹🏽‍♂️"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence), tree.find("🤹🏿‍♀️"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence), tree.find("👩‍🦱"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence),
            tree.find("🧑🏼‍🫯‍🧑🏿"));
  ASSERT_EQ(toUnderlying(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence),
            tree.find("🧑🏿‍🐰‍🧑🏽"));
}

TEST(EmojiPropertyTest, base) {
  auto ret = ucp::parseEmojiProperty("");
  ASSERT_FALSE(ret.hasValue());
  ret = ucp::parseEmojiProperty("None");
  ASSERT_FALSE(ret.hasValue());
  ASSERT_STREQ("", ucp::toString(RGIEmojiSeq::None));

  ret = ucp::parseEmojiProperty("Basic_Emoji");
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, ret.unwrap());
  ASSERT_STREQ("Basic_Emoji", ucp::toString(ret.unwrap()));

  ret = ucp::parseEmojiProperty("Emoji_Keycap_Sequence");
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(RGIEmojiSeq::Emoji_Keycap_Sequence, ret.unwrap());
  ASSERT_STREQ("Emoji_Keycap_Sequence", ucp::toString(ret.unwrap()));

  ret = ucp::parseEmojiProperty("RGI_Emoji_Modifier_Sequence");
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Modifier_Sequence, ret.unwrap());
  ASSERT_STREQ("RGI_Emoji_Modifier_Sequence", ucp::toString(ret.unwrap()));

  ret = ucp::parseEmojiProperty("RGI_Emoji_Flag_Sequence");
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, ret.unwrap());
  ASSERT_STREQ("RGI_Emoji_Flag_Sequence", ucp::toString(ret.unwrap()));

  ret = ucp::parseEmojiProperty("RGI_Emoji_Tag_Sequence");
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Tag_Sequence, ret.unwrap());
  ASSERT_STREQ("RGI_Emoji_Tag_Sequence", ucp::toString(ret.unwrap()));

  ret = ucp::parseEmojiProperty("RGI_Emoji_ZWJ_Sequence");
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, ret.unwrap());
  ASSERT_STREQ("RGI_Emoji_ZWJ_Sequence", ucp::toString(ret.unwrap()));

  ret = ucp::parseEmojiProperty("RGI_Emoji");
  ASSERT_TRUE(ret.hasValue());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji, ret.unwrap());
  ASSERT_STREQ("RGI_Emoji", ucp::toString(ret.unwrap()));
}

struct EmojiSeqTest : public ::testing::TestWithParam<EMOJI_SEQ_ENTRY> {
  static void doTest() {
    auto &entry = GetParam();
    std::string str = entry.codes.toUTF8();
    auto out = format("(%s, %s)", str.c_str(), ucp::toString(entry.property));
    SCOPED_TRACE(out);
    ASSERT_EQ(entry.property, ucp::getEmojiProperty(str));
  }
};

TEST_P(EmojiSeqTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(EmojiSeqTest, EmojiSeqTest, ::testing::ValuesIn(emoji_seq_table));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}