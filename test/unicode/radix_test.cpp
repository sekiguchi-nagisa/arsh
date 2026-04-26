#include "../test_common.h"

#include <unicode/radix_tree.h>

using namespace arsh;

static std::vector<std::string> list(const RadixTree &tree) {
  std::vector<std::string> values;
  tree.iterate([&values](StringRef ref, unsigned char) {
    values.push_back(ref.toString());
    return true;
  });
  std::sort(values.begin(), values.end());
  return values;
}

template <typename... Arg>
static std::vector<std::string> slist(Arg &&...args) {
  return std::vector<std::string>{std::forward<Arg>(args)...};
}

// for testing
enum RGIEmojiSeq : unsigned char {
  None,
  Basic_Emoji,
  Emoji_Keycap_Sequence,
  RGI_Emoji_Flag_Sequence,
  RGI_Emoji_Tag_Sequence,
  RGI_Emoji_ZWJ_Sequence,
};

TEST(RadixTest, base) {
  RadixTree tree;
  ASSERT_EQ(0, tree.longestStringSize());
  ASSERT_EQ(slist(), list(tree));

  // do nothing
  ASSERT_EQ(RadixTree::AddStatus::NONE_PROPERTY, tree.add("ss", 0));
  ASSERT_EQ(0, tree.longestStringSize());
  ASSERT_EQ(RadixTree::AddStatus::EMPTY, tree.add("", 1));
  ASSERT_EQ(0, tree.longestStringSize());

  // add AAA
  ASSERT_EQ(RadixTree::AddStatus::OK, tree.add("AAA", Basic_Emoji));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("AAA", Emoji_Keycap_Sequence));
  ASSERT_EQ(Basic_Emoji, tree.find("AAA"));
  ASSERT_FALSE(tree.empty());
  ASSERT_EQ("AAA", tree.getPrefix());
  ASSERT_EQ(Basic_Emoji, tree.getProperty());
  ASSERT_EQ(0, tree.getChildren().size());
  ASSERT_EQ(slist("AAA"), list(tree));
  ASSERT_EQ(3, tree.longestStringSize());

  // add BBBB
  ASSERT_EQ(RadixTree::AddStatus::OK, tree.add("BBBB", RGI_Emoji_Flag_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("AAA", Emoji_Keycap_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("BBBB", Emoji_Keycap_Sequence));
  ASSERT_EQ(RGI_Emoji_Flag_Sequence, tree.find("BBBB"));
  ASSERT_EQ(Basic_Emoji, tree.find("AAA"));
  ASSERT_EQ("", tree.getPrefix());
  ASSERT_EQ(None, tree.getProperty());
  ASSERT_EQ(2, tree.getChildren().size());
  ASSERT_EQ("AAA", tree.childAt('A')->getPrefix());
  ASSERT_EQ(Basic_Emoji, tree.childAt('A')->getProperty());
  ASSERT_EQ(0, tree.childAt('A')->getChildren().size());
  ASSERT_EQ("BBBB", tree.childAt('B')->getPrefix());
  ASSERT_EQ(RGI_Emoji_Flag_Sequence, tree.childAt('B')->getProperty());
  ASSERT_EQ(0, tree.childAt('B')->getChildren().size());
  ASSERT_EQ(slist("AAA", "BBBB"), list(tree));
  ASSERT_EQ(4, tree.longestStringSize());

  // add BB
  ASSERT_EQ(RadixTree::AddStatus::OK, tree.add("BB", RGI_Emoji_Tag_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("AAA", Emoji_Keycap_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("BBBB", Emoji_Keycap_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("BB", Emoji_Keycap_Sequence));
  ASSERT_EQ(RGI_Emoji_Flag_Sequence, tree.find("BBBB"));
  ASSERT_EQ(Basic_Emoji, tree.find("AAA"));
  ASSERT_EQ(RGI_Emoji_Tag_Sequence, tree.find("BB"));
  ASSERT_EQ(None, tree.find("B"));
  ASSERT_EQ(None, tree.find("BBB"));
  ASSERT_EQ("", tree.getPrefix());
  ASSERT_EQ(None, tree.getProperty());
  ASSERT_EQ(2, tree.getChildren().size());
  ASSERT_EQ("AAA", tree.childAt('A')->getPrefix());
  ASSERT_EQ(Basic_Emoji, tree.childAt('A')->getProperty());
  ASSERT_EQ(0, tree.childAt('A')->getChildren().size());
  ASSERT_EQ("BB", tree.childAt('B')->getPrefix());
  ASSERT_EQ(RGI_Emoji_Tag_Sequence, tree.childAt('B')->getProperty());
  ASSERT_EQ(1, tree.childAt('B')->getChildren().size());
  ASSERT_EQ("BB", tree.childAt('B')->childAt('B')->getPrefix());
  ASSERT_EQ(RGI_Emoji_Flag_Sequence, tree.childAt('B')->childAt('B')->getProperty());
  ASSERT_EQ(0, tree.childAt('B')->childAt('B')->getChildren().size());
  ASSERT_EQ(slist("AAA", "BB", "BBBB"), list(tree));
  ASSERT_EQ(4, tree.longestStringSize());

  // add AAACD
  ASSERT_EQ(RadixTree::AddStatus::OK, tree.add("AAACD", RGI_Emoji_ZWJ_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("AAA", Emoji_Keycap_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("BBBB", Emoji_Keycap_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("BB", Emoji_Keycap_Sequence));
  ASSERT_EQ(RadixTree::AddStatus::ADDED, tree.add("AAACD", Emoji_Keycap_Sequence));
  ASSERT_EQ(RGI_Emoji_Flag_Sequence, tree.find("BBBB"));
  ASSERT_EQ(Basic_Emoji, tree.find("AAA"));
  ASSERT_EQ(RGI_Emoji_Tag_Sequence, tree.find("BB"));
  ASSERT_EQ(RGI_Emoji_ZWJ_Sequence, tree.find("AAACD"));
  ASSERT_EQ("", tree.getPrefix());
  ASSERT_EQ(None, tree.getProperty());
  ASSERT_EQ(2, tree.getChildren().size());
  ASSERT_EQ("AAA", tree.childAt('A')->getPrefix());
  ASSERT_EQ(Basic_Emoji, tree.childAt('A')->getProperty());
  ASSERT_EQ(1, tree.childAt('A')->getChildren().size());
  ASSERT_EQ("CD", tree.childAt('A')->childAt('C')->getPrefix());
  ASSERT_EQ(RGI_Emoji_ZWJ_Sequence, tree.childAt('A')->childAt('C')->getProperty());
  ASSERT_EQ(0, tree.childAt('A')->childAt('C')->getChildren().size());
  ASSERT_EQ("BB", tree.childAt('B')->getPrefix());
  ASSERT_EQ(RGI_Emoji_Tag_Sequence, tree.childAt('B')->getProperty());
  ASSERT_EQ(1, tree.childAt('B')->getChildren().size());
  ASSERT_EQ("BB", tree.childAt('B')->childAt('B')->getPrefix());
  ASSERT_EQ(RGI_Emoji_Flag_Sequence, tree.childAt('B')->childAt('B')->getProperty());
  ASSERT_EQ(0, tree.childAt('B')->childAt('B')->getChildren().size());
  ASSERT_EQ(slist("AAA", "AAACD", "BB", "BBBB"), list(tree));
  ASSERT_EQ(5, tree.longestStringSize());

  // serialize
  FlexBuffer<uint8_t> buf;
  ASSERT_TRUE(serialize(tree, buf));
  PackedRadixTree packedTree(tree.longestStringSize(), buf.data(), buf.size());
  ASSERT_EQ(Basic_Emoji, packedTree.find("AAA"));
  ASSERT_EQ(RGI_Emoji_Flag_Sequence, packedTree.find("BBBB"));
  ASSERT_EQ(RGI_Emoji_Tag_Sequence, packedTree.find("BB"));
  ASSERT_EQ(RGI_Emoji_ZWJ_Sequence, packedTree.find("AAACD"));

  auto ret = packedTree.findLongestMatched("");
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(0, ret.second);
  ret = packedTree.findLongestMatched("A");
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(0, ret.second);
  ret = packedTree.findLongestMatched("AA");
  ASSERT_EQ(0, ret.first);
  ASSERT_EQ(0, ret.second);
  ret = packedTree.findLongestMatched("AAA");
  ASSERT_EQ(3, ret.first);
  ASSERT_EQ(Basic_Emoji, ret.second);
  ret = packedTree.findLongestMatched("AAAC");
  ASSERT_EQ(3, ret.first);
  ASSERT_EQ(Basic_Emoji, ret.second);
  ret = packedTree.findLongestMatched("AAACD");
  ASSERT_EQ(5, ret.first);
  ASSERT_EQ(RGI_Emoji_ZWJ_Sequence, ret.second);
  ret = packedTree.findLongestMatched("AAACDE");
  ASSERT_EQ(5, ret.first);
  ASSERT_EQ(RGI_Emoji_ZWJ_Sequence, ret.second);
  ret = packedTree.findLongestMatched("BBB");
  ASSERT_EQ(2, ret.first);
  ASSERT_EQ(RGI_Emoji_Tag_Sequence, ret.second);
  ret = packedTree.findLongestMatched("BBBBB");
  ASSERT_EQ(4, ret.first);
  ASSERT_EQ(RGI_Emoji_Flag_Sequence, ret.second);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}