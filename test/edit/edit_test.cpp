#include "gtest/gtest.h"

#include "../test_common.h"

#include <encoding.h>

using namespace ydsh;

TEST(EncodingTest, charLen1) {
  // next char
  CharWidthProperties ps;
  StringRef line = "あaう";
  ASSERT_EQ("あaう", line);
  auto ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // あ
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("aう", line);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // a
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("う", line);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // う
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("", line);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // end
  ASSERT_EQ(0, ret.byteSize);
  ASSERT_EQ(0, ret.colSize);
}

TEST(EncodingTest, charLen2) {
  // prev char
  CharWidthProperties ps;
  StringRef line = "あaう";
  ASSERT_EQ("あaう", line);
  auto ret = getCharLen(line, CharLenOp::PREV_CHAR, ps); // う
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removeSuffix(ret.byteSize);
  ASSERT_EQ("あa", line);
  ret = getCharLen(line, CharLenOp::PREV_CHAR, ps); // a
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removeSuffix(ret.byteSize);
  ASSERT_EQ("あ", line);
  ret = getCharLen(line, CharLenOp::PREV_CHAR, ps); // あ
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removeSuffix(ret.byteSize);
  ASSERT_EQ("", line);
  ret = getCharLen(line, CharLenOp::PREV_CHAR, ps); // start
  ASSERT_EQ(0, ret.byteSize);
  ASSERT_EQ(0, ret.colSize);
}

TEST(EncodingTest, charLen3) {
  // next char
  CharWidthProperties ps;
  StringRef line = "○a○🇦🇽b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦";
  auto ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // ○
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(1, ret.colSize); // half width

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("a○🇦🇽b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // a
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("○🇦🇽b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ps.setProperty(CharWidthProperty::EAW, 2);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // ○
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize); // full width

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("🇦🇽b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ps = CharWidthProperties();
  ps.setProperty(CharWidthProperty::EMOJI_FLAG_SEQ, 2);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // 🇦🇽
  ASSERT_EQ(8, ret.byteSize);
  ASSERT_EQ(2, ret.colSize); // FLAG_SEQ width is 2

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("b🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // b
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("🇦🇽💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ps = CharWidthProperties();
  ps.setProperty(CharWidthProperty::EMOJI_FLAG_SEQ, 4);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // 🇦🇽
  ASSERT_EQ(8, ret.byteSize);
  ASSERT_EQ(4, ret.colSize); // FLAG_SEQ width is 4

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("💁🏾‍♀️c💁🏾‍♀️🇦", line);
  ps = CharWidthProperties();
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // 💁🏾‍♀️
  ASSERT_EQ(17, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("c💁🏾‍♀️🇦", line);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // c
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("💁🏾‍♀️🇦", line);
  ps = CharWidthProperties();
  ps.setProperty(CharWidthProperty::EMOJI_ZWJ_SEQ, 3);
  ps.setProperty(CharWidthProperty::EAW, 2);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); // 💁🏾‍♀️
  ASSERT_EQ(17, ret.byteSize);
  ASSERT_EQ(6, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("🇦", line);
  ret = getCharLen(line, CharLenOp::NEXT_CHAR, ps); //
  ASSERT_EQ(4, ret.byteSize);
  ASSERT_EQ(1, ret.colSize); // regional indicator is half
}

TEST(EncodingTest, worldLen1) {
  // next word
  CharWidthProperties ps;
  StringRef line = "/home/カタカナあい";
  auto ret = getWordLen(line, WordLenOp::NEXT_WORD, ps); // /
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("home/カタカナあい", line);
  ret = getWordLen(line, WordLenOp::NEXT_WORD, ps); // home
  ASSERT_EQ(4, ret.byteSize);
  ASSERT_EQ(4, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("/カタカナあい", line);
  ret = getWordLen(line, WordLenOp::NEXT_WORD, ps); // /
  ASSERT_EQ(1, ret.byteSize);
  ASSERT_EQ(1, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("カタカナあい", line);
  ret = getWordLen(line, WordLenOp::NEXT_WORD, ps); // カタカナ
  ASSERT_EQ(12, ret.byteSize);
  ASSERT_EQ(8, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("あい", line);
  ret = getWordLen(line, WordLenOp::NEXT_WORD, ps); // あ
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("い", line);
  ret = getWordLen(line, WordLenOp::NEXT_WORD, ps); // い
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removePrefix(ret.byteSize);
  ASSERT_EQ("", line);
  ret = getWordLen(line, WordLenOp::NEXT_WORD, ps); //
  ASSERT_EQ(0, ret.byteSize);
  ASSERT_EQ(0, ret.colSize);
}

TEST(EncodingTest, wordLen2) {
  // prev char
  CharWidthProperties ps;
  StringRef line = "3.14アアabcう";
  auto ret = getWordLen(line, WordLenOp::PREV_WORD, ps); // う
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(2, ret.colSize);

  line.removeSuffix(ret.byteSize);
  ASSERT_EQ("3.14アアabc", line);
  ret = getWordLen(line, WordLenOp::PREV_WORD, ps); // abc
  ASSERT_EQ(3, ret.byteSize);
  ASSERT_EQ(3, ret.colSize);

  line.removeSuffix(ret.byteSize);
  ASSERT_EQ("3.14アア", line);
  ret = getWordLen(line, WordLenOp::PREV_WORD, ps); // アア
  ASSERT_EQ(6, ret.byteSize);
  ASSERT_EQ(4, ret.colSize);

  line.removeSuffix(ret.byteSize);
  ASSERT_EQ("3.14", line);
  ret = getWordLen(line, WordLenOp::PREV_WORD, ps); // 3.14
  ASSERT_EQ(4, ret.byteSize);
  ASSERT_EQ(4, ret.colSize);

  line.removeSuffix(ret.byteSize);
  ASSERT_EQ("", line);
  ret = getWordLen(line, WordLenOp::PREV_WORD, ps); //
  ASSERT_EQ(0, ret.byteSize);
  ASSERT_EQ(0, ret.colSize);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}