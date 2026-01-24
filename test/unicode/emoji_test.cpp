#include "../test_common.h"

#include "../tools/emoji/emoji_trie.h"
#include <unicode/property.h>

using namespace arsh;

TEST(EmojiTrieTest, base) {
  EmojiRadixTree tree;
  ASSERT_TRUE(tree.empty());
  ASSERT_EQ(RGIEmojiSeq::None, tree.getProperty());
  ASSERT_FALSE(tree.add("AAA", RGIEmojiSeq::None));

  // add AAA
  ASSERT_TRUE(tree.add("AAA", RGIEmojiSeq::Basic_Emoji));
  ASSERT_FALSE(tree.add("AAA", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("AAA"));
  ASSERT_FALSE(tree.empty());
  ASSERT_EQ("AAA", tree.getPrefix());
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.getProperty());
  ASSERT_EQ(0, tree.getChildren().size());

  // add BBBB
  ASSERT_TRUE(tree.add("BBBB", RGIEmojiSeq::RGI_Emoji_Flag_Sequence));
  ASSERT_FALSE(tree.add("AAA", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_FALSE(tree.add("BBBB", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, tree.find("BBBB"));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("AAA"));
  ASSERT_EQ("", tree.getPrefix());
  ASSERT_EQ(RGIEmojiSeq::None, tree.getProperty());
  ASSERT_EQ(2, tree.getChildren().size());
  ASSERT_EQ("AAA", tree.childAt('A')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.childAt('A')->getProperty());
  ASSERT_EQ(0, tree.childAt('A')->getChildren().size());
  ASSERT_EQ("BBBB", tree.childAt('B')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, tree.childAt('B')->getProperty());
  ASSERT_EQ(0, tree.childAt('B')->getChildren().size());

  // add BB
  ASSERT_TRUE(tree.add("BB", RGIEmojiSeq::RGI_Emoji_Tag_Sequence));
  ASSERT_FALSE(tree.add("AAA", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_FALSE(tree.add("BBBB", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_FALSE(tree.add("BB", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, tree.find("BBBB"));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("AAA"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Tag_Sequence, tree.find("BB"));
  ASSERT_EQ(RGIEmojiSeq::None, tree.find("B"));
  ASSERT_EQ(RGIEmojiSeq::None, tree.find("BBB"));
  ASSERT_EQ("", tree.getPrefix());
  ASSERT_EQ(RGIEmojiSeq::None, tree.getProperty());
  ASSERT_EQ(2, tree.getChildren().size());
  ASSERT_EQ("AAA", tree.childAt('A')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.childAt('A')->getProperty());
  ASSERT_EQ(0, tree.childAt('A')->getChildren().size());
  ASSERT_EQ("BB", tree.childAt('B')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Tag_Sequence, tree.childAt('B')->getProperty());
  ASSERT_EQ(1, tree.childAt('B')->getChildren().size());
  ASSERT_EQ("BB", tree.childAt('B')->childAt('B')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, tree.childAt('B')->childAt('B')->getProperty());
  ASSERT_EQ(0, tree.childAt('B')->childAt('B')->getChildren().size());

  // add AAACD
  ASSERT_TRUE(tree.add("AAACD", RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence));
  ASSERT_FALSE(tree.add("AAA", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_FALSE(tree.add("BBBB", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_FALSE(tree.add("BB", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_FALSE(tree.add("AAACD", RGIEmojiSeq::Emoji_Keycap_Sequence));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, tree.find("BBBB"));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("AAA"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Tag_Sequence, tree.find("BB"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("AAACD"));
  ASSERT_EQ("", tree.getPrefix());
  ASSERT_EQ(RGIEmojiSeq::None, tree.getProperty());
  ASSERT_EQ(2, tree.getChildren().size());
  ASSERT_EQ("AAA", tree.childAt('A')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.childAt('A')->getProperty());
  ASSERT_EQ(1, tree.childAt('A')->getChildren().size());
  ASSERT_EQ("CD", tree.childAt('A')->childAt('C')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.childAt('A')->childAt('C')->getProperty());
  ASSERT_EQ(0, tree.childAt('A')->childAt('C')->getChildren().size());
  ASSERT_EQ("BB", tree.childAt('B')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Tag_Sequence, tree.childAt('B')->getProperty());
  ASSERT_EQ(1, tree.childAt('B')->getChildren().size());
  ASSERT_EQ("BB", tree.childAt('B')->childAt('B')->getPrefix());
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, tree.childAt('B')->childAt('B')->getProperty());
  ASSERT_EQ(0, tree.childAt('B')->childAt('B')->getChildren().size());
}

struct EMOJI_SEQ_ENTRY {
  RGIEmojiSeq property;
  CodePointArray codes;
};

#include "../tools/emoji/emoji_seq.in"

TEST(EmojiTrieTest, all) {
  EmojiRadixTree tree;

  // add all
  for (auto &[p, codes] : emoji_seq_table) {
    std::string str = codes.toUTF8();
    ASSERT_TRUE(tree.add(str, p));
    ASSERT_EQ(p, tree.find(str));
  }

  // re-lookup
  for (auto &[p, codes] : emoji_seq_table) {
    std::string str = codes.toUTF8();
    ASSERT_FALSE(tree.add(str, p));
    ASSERT_EQ(p, tree.find(str));
  }

  ASSERT_EQ(RGIEmojiSeq::None, tree.find("AAA"));
  ASSERT_EQ(RGIEmojiSeq::None, tree.find(""));
  ASSERT_EQ(RGIEmojiSeq::None, tree.find("123"));
  ASSERT_EQ(RGIEmojiSeq::None, tree.find("あああ"));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("⚡"));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("🪾"));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("↕️"));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("❤️"));
  ASSERT_EQ(RGIEmojiSeq::Basic_Emoji, tree.find("🕷️"));
  ASSERT_EQ(RGIEmojiSeq::Emoji_Keycap_Sequence, tree.find("#️⃣"));
  ASSERT_EQ(RGIEmojiSeq::Emoji_Keycap_Sequence, tree.find("7️⃣"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, tree.find("🇧🇬"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Flag_Sequence, tree.find("🇯🇵"));
  ASSERT_EQ(RGIEmojiSeq::None, tree.find("🇯"));  // single regional indicator is not emoji seq
  ASSERT_EQ(RGIEmojiSeq::None, tree.find("🇯🇯")); // JJ is not defined
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Tag_Sequence, tree.find("🏴󠁧󠁢󠁥󠁮󠁧󠁿"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Modifier_Sequence, tree.find("✌🏼"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Modifier_Sequence, tree.find("👦🏽"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_Modifier_Sequence, tree.find("🧓🏻"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence,
            tree.find("👨🏻‍❤️‍💋‍👨🏻"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("👨‍👧"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence,
            tree.find("👨🏽‍❤️‍💋‍👨🏾"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("👩🏼‍🤝‍👩🏽"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("🫱🏼‍🫲🏻"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("⛹🏽‍♂️"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("🤹🏿‍♀️"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("👩‍🦱"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("🧑🏼‍🫯‍🧑🏿"));
  ASSERT_EQ(RGIEmojiSeq::RGI_Emoji_ZWJ_Sequence, tree.find("🧑🏿‍🐰‍🧑🏽"));
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