#include "gtest/gtest.h"

#include <cstring>

#include <misc/grapheme.hpp>
#include <misc/num_util.hpp>
#include <misc/word.hpp>

#include "../test_common.h"

using namespace ydsh;

class UnicodeTest : public ::testing::Test {
public:
  UnicodeTest() = default;

  static void toCodePoint(const char *str, int &codePoint) {
    ASSERT_TRUE(str != nullptr);

    int code = 0;
    unsigned int len = strlen(str);

    unsigned int s = UnicodeUtil::utf8ToCodePoint(str, len == 0 ? 1 : len, code);
    ASSERT_NE(0u, s);
    ASSERT_NE(-1, code);

    codePoint = code;
  }

  template <unsigned int N>
  static void assertByteSize(const unsigned int size, const char (&str)[N]) {
    ASSERT_EQ(size, UnicodeUtil::utf8ValidateChar(str, str + N));
  }

  static void assertCodePoint(const int expect, const char *str) {
    int code = 0;
    toCodePoint(str, code);
    ASSERT_EQ(expect, code);
  }

  static void assertWidth(const int width, const char *str, bool ambiguousWidth2 = false) {
    int codePoint = 0;
    toCodePoint(str, codePoint);

    auto e = ambiguousWidth2 ? AmbiguousCharWidth::FULL : AmbiguousCharWidth::HALF;
    ASSERT_EQ(width, UnicodeUtil::width(codePoint, e));
  }

  static void assertIllegal(const char *const buf, unsigned int bufSize) {
    int codePoint = 0;
    ASSERT_EQ(0u, UnicodeUtil::utf8ToCodePoint(buf, bufSize, codePoint));
    ASSERT_EQ(-1, codePoint);

    ASSERT_EQ(-1, UnicodeUtil::utf8ToCodePoint(buf, bufSize));
  }

  template <unsigned int N>
  static void assertCodePoint2Utf8(const char (&ch)[N]) {
    assertCodePoint2Utf8(ch, N - 1);
  }

  static void assertCodePoint2Utf8(const char *ch, unsigned int byteSize) {
    char buf[4];
    int codePoint = UnicodeUtil::utf8ToCodePoint(ch, byteSize);
    unsigned int size = UnicodeUtil::codePointToUtf8(codePoint, buf);
    ASSERT_EQ(byteSize, size);
    std::string before(ch, byteSize);
    std::string after(buf, size);
    ASSERT_EQ(before, after);
  }
};

TEST_F(UnicodeTest, size) {
  ASSERT_NO_FATAL_FAILURE(assertByteSize(1, ""));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(1, "1"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(1, "a"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(1, "\n"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(1, "\t"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(2, "å"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(2, "¶"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(2, "Ω"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(3, "あ"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(3, "解"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(3, "墨"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(3, "ｱ"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(4, "𪗱"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(4, "𣏤"));
  ASSERT_NO_FATAL_FAILURE(assertByteSize(4, "𣴀"));
}

TEST_F(UnicodeTest, codepoint2utf8) {
  char buf[4];
  ASSERT_EQ(0, UnicodeUtil::codePointToUtf8(-3, buf));

  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("\0"));
  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("a"));
  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("å"));
  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("¶"));
  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("あ"));
  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("ｱ"));
  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("𣏤"));
  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("𣴀"));
  ASSERT_NO_FATAL_FAILURE(assertCodePoint2Utf8("�"));
}

TEST_F(UnicodeTest, base) {
  ASSERT_EQ(0, UnicodeUtil::width(0));
  ASSERT_EQ(-1, UnicodeUtil::width('\n'));
  ASSERT_EQ(-1, UnicodeUtil::width('\t'));
  ASSERT_EQ(1, UnicodeUtil::width('1'));
  ASSERT_EQ(1, UnicodeUtil::width(' '));
  ASSERT_EQ(1, UnicodeUtil::width('\\'));
  ASSERT_EQ(1, UnicodeUtil::width('~'));
}

TEST_F(UnicodeTest, codePoint) {
  ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x40, "@"));
  ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x7E, "~"));
  ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x0150, "Ő"));
  ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x305F, "た"));
  ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x25E56, "𥹖"));
  ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(UnicodeUtil::REPLACEMENT_CHAR_CODE,
                                                UnicodeUtil::REPLACEMENT_CHAR_UTF8));
}

TEST_F(UnicodeTest, multi) {
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(-1, "\n"));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(-1, "\n", true));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(-1, "\r"));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(-1, "\r", true));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(0, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(0, "", true));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "a"));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "a", true));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "å"));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "å", true));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "◯"));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, "◯", true));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "■"));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, "■", true));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, "ま"));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, "ま", true));

  unsigned char b[] = {0xCC, 0x88, 0}; // combining character
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(-2, (char *)b));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(-2, (char *)b, true));

  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "ｱ"));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "ｱ", true));

  // emoji (regional indicator)
  char buf1[5] = {0};
  ASSERT_EQ(4, UnicodeUtil::codePointToUtf8(0x1F1E6, buf1));
  buf1[4] = '\0';
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, buf1));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, buf1, true));

  // emoji
  char buf2[5] = {0};
  ASSERT_EQ(4, UnicodeUtil::codePointToUtf8(0x1F970, buf2));
  buf2[4] = '\0';
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, buf2));
  ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, buf2, true));
}

TEST_F(UnicodeTest, multi2) {
  int code = 0;
  toCodePoint("◯", code);

  // C
  const char *r = setlocale(LC_CTYPE, "C");
  ASSERT_TRUE(r != nullptr);
  ASSERT_EQ(1, UnicodeUtil::localeAwareWidth(code));

  // ja
  r = setlocale(LC_CTYPE, "ja_JP.UTF-8");
  if (r != nullptr) {
    ASSERT_EQ(2, UnicodeUtil::localeAwareWidth(code));
  }

  // zh
  r = setlocale(LC_CTYPE, "zh_CN.UTF-8");
  if (r != nullptr) {
    ASSERT_EQ(2, UnicodeUtil::localeAwareWidth(code));
  }

  // ko
  r = setlocale(LC_CTYPE, "ko_KR.UTF-8");
  if (r != nullptr) {
    ASSERT_EQ(2, UnicodeUtil::localeAwareWidth(code));
  }

  // reset locale
  setlocale(LC_ALL, "");
}

TEST_F(UnicodeTest, illegal) {
  // 2byte
  {
    char b[] = {static_cast<char>(200)};
    ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
  }

  // 3byte
  {
    char b[] = {static_cast<char>(238), 32};
    ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
  }

  // 4byte
  {
    char b[] = {static_cast<char>(243), 32};
    ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
  }

  // illegal byte
  {
    char b[] = {static_cast<char>(144)};
    ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
  }

  {
    char b[] = {static_cast<char>(253)};
    ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
  }

  // illegal code point
  ASSERT_EQ(-3, UnicodeUtil::width(-1, AmbiguousCharWidth::HALF));
}

TEST_F(UnicodeTest, utf16) {
  unsigned short high = 0xD867;
  unsigned short low = 0xDE3D;

  ASSERT_TRUE(UnicodeUtil::isHighSurrogate(high));
  ASSERT_FALSE(UnicodeUtil::isHighSurrogate(low));

  ASSERT_TRUE(UnicodeUtil::isLowSurrogate(low));
  ASSERT_FALSE(UnicodeUtil::isLowSurrogate(high));

  ASSERT_TRUE(UnicodeUtil::isSurrogate(high));
  ASSERT_TRUE(UnicodeUtil::isSurrogate(low));

  ASSERT_TRUE(UnicodeUtil::isBmpCodePoint(high));
  ASSERT_TRUE(UnicodeUtil::isBmpCodePoint(low));

  int code = UnicodeUtil::utf16ToCodePoint(high, low);
  ASSERT_TRUE(UnicodeUtil::isSupplementaryCodePoint(code));
  ASSERT_FALSE(UnicodeUtil::isBmpCodePoint(code));
  ASSERT_EQ(0x29E3D, code);
}

TEST_F(UnicodeTest, graphemeBreakProperty) {
  int code = 0;
  toCodePoint("1", code);
  auto p = GraphemeBoundary::getBreakProperty(code);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Any, p);

  toCodePoint("灘", code);
  p = GraphemeBoundary::getBreakProperty(code);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Any, p);

  toCodePoint("\r", code);
  p = GraphemeBoundary::getBreakProperty(code);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::CR, p);

  toCodePoint("\n", code);
  p = GraphemeBoundary::getBreakProperty(code);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::LF, p);

  toCodePoint("\a", code);
  p = GraphemeBoundary::getBreakProperty(code);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Control, p);

  p = GraphemeBoundary::getBreakProperty(0x200C); // ZERO WIDTH NON-JOINER
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Extend, p);

  p = GraphemeBoundary::getBreakProperty(0x200D); // ZERO WIDTH JOINER
  ASSERT_EQ(GraphemeBoundary::BreakProperty::ZWJ, p);

  p = GraphemeBoundary::getBreakProperty(0x1F1E6); // REGIONAL INDICATOR SYMBOL LETTER A
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Regional_Indicator, p);

  p = GraphemeBoundary::getBreakProperty(0x1F1FF); // REGIONAL INDICATOR SYMBOL LETTER Z
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Regional_Indicator, p);

  p = GraphemeBoundary::getBreakProperty(0x0602);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Prepend, p);

  p = GraphemeBoundary::getBreakProperty(0x102B); // exclude
  ASSERT_NE(GraphemeBoundary::BreakProperty::SpacingMark, p);

  p = GraphemeBoundary::getBreakProperty(0x0E33); // exclude
  ASSERT_EQ(GraphemeBoundary::BreakProperty::SpacingMark, p);

  p = GraphemeBoundary::getBreakProperty(0xA960);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::L, p);

  p = GraphemeBoundary::getBreakProperty(0x11A2);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::V, p);

  p = GraphemeBoundary::getBreakProperty(0xD7FB);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::T, p);

  p = GraphemeBoundary::getBreakProperty(0xAC1C);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::LV, p);

  p = GraphemeBoundary::getBreakProperty(0xAC03);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::LVT, p);

  p = GraphemeBoundary::getBreakProperty(0x1F0CC);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Extended_Pictographic, p);

  p = GraphemeBoundary::getBreakProperty(0x1F6B9); // Emoji/Extended_Pictographic
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Extended_Pictographic, p);

  p = GraphemeBoundary::getBreakProperty(UnicodeUtil::REPLACEMENT_CHAR_CODE); // placement char
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Any, p);

  // dummy property for broken code points (`Control` is always grapheme boundary)
  p = GraphemeBoundary::getBreakProperty(-1);
  ASSERT_EQ(GraphemeBoundary::BreakProperty::Control, p);
}

TEST_F(UnicodeTest, wordBreakProperty) {
  int code = 0;
  toCodePoint("1", code);
  auto p = WordBoundary::getBreakProperty(code);
  ASSERT_EQ(WordBoundary::BreakProperty::Numeric, p);

  toCodePoint(".", code);
  p = WordBoundary::getBreakProperty(code);
  ASSERT_EQ(WordBoundary::BreakProperty::MidNumLet, p);

  p = WordBoundary::getBreakProperty(0x0E33);
  ASSERT_EQ(WordBoundary::BreakProperty::Any, p);

  p = WordBoundary::getBreakProperty(UnicodeUtil::REPLACEMENT_CHAR_CODE);
  ASSERT_EQ(WordBoundary::BreakProperty::Any, p);

  // dummy property for broken code points (`Newline` is always word boundary)
  p = WordBoundary ::getBreakProperty(-1);
  ASSERT_EQ(WordBoundary::BreakProperty::Newline, p);
}

static std::vector<int> getInput(const std::string &param) {
  auto values = split(param, ' ');
  auto iter = std::remove_if(values.begin(), values.end(),
                             [](const std::string &v) { return v == "÷" || v == "×"; });
  values.erase(iter, values.end());

  std::vector<int> ret;
  for (auto &v : values) {
    StringRef ref = v;
    auto pair = convertToNum<int>(ref.begin(), ref.end(), 16);
    if (!pair) {
      fatal("broken format: %s\n", v.c_str());
    }
    ret.push_back(pair.value);
  }
  return ret;
}

static std::vector<std::vector<int>> getExpected(const std::string &param) {
  auto values = split(param, ' ');
  std::vector<std::vector<int>> ret;
  ret.emplace_back();
  for (auto &v : values) {
    if (v == "÷") {
      ret.emplace_back();
    } else if (v == "×") {
      continue;
    } else {
      StringRef ref = v;
      auto pair = convertToNum<int>(ref.begin(), ref.end(), 16);
      if (!pair) {
        fatal("broken format: %s\n", v.c_str());
      }
      ret.back().push_back(pair.value);
    }
  }

  // remove empty grapheme
  auto iter =
      std::remove_if(ret.begin(), ret.end(), [](const std::vector<int> &v) { return v.empty(); });
  ret.erase(iter, ret.end());
  return ret;
}

static std::string toUTF8(const std::vector<int> &codes) {
  std::string ret;
  for (auto &c : codes) {
    char buf[8];
    unsigned int size = UnicodeUtil::codePointToUtf8(c, buf);
    ret.append(buf, size);
  }
  return ret;
}

class CodePointStream {
private:
  std::reference_wrapper<const std::vector<int>> codePoints;
  unsigned int index{0};

public:
  explicit CodePointStream(const std::vector<int> &codePoints)
      : codePoints(std::cref(codePoints)) {}

  explicit operator bool() const { return this->index < this->codePoints.get().size(); }

  unsigned int saveState() const { return this->index; }

  void restoreState(unsigned int i) { this->index = i; }

  int nextCodePoint() { return this->codePoints.get()[this->index++]; }
};

struct GraphemeBreakTest : public ::testing::TestWithParam<std::string> {
  static void doTest() {
    auto input = getInput(GetParam());
    auto expected = getExpected(GetParam());

    ASSERT_FALSE(input.empty());
    ASSERT_FALSE(expected.empty());
    for (auto &e : expected) {
      ASSERT_FALSE(e.empty());
    }

    std::vector<std::vector<int>> output;
    output.emplace_back();
    CodePointStream stream(input);
    GraphemeScanner<CodePointStream> scanner(std::move(stream));
    while (scanner.getStream()) {
      auto p = scanner.nextProperty();
      auto c = scanner.getCodePoint();
      if (scanner.scanBoundary(p)) {
        output.emplace_back();
      }
      output.back().push_back(c);
    }
    ASSERT_EQ(expected, output);
  }

  static void doTest2() {
    auto input = getInput(GetParam());
    auto expected = getExpected(GetParam());

    std::string inputStr = toUTF8(input);
    std::vector<std::string> expectedList;
    for (auto &e : expected) {
      expectedList.push_back(toUTF8(e));
    }

    Utf8GraphemeScanner scanner(inputStr);
    std::vector<std::string> outputList;
    GraphemeCluster ret;
    for (unsigned int i = 0; scanner.hasNext(); i++) {
      scanner.next(ret);
      ASSERT_EQ(expected[i][0], ret.getCodePointAt(0));
      outputList.push_back(ret.getRef().toString());
    }
    ASSERT_FALSE(scanner.hasNext());
    ASSERT_EQ(expectedList, outputList);
  }
};

TEST(GraphemeBreakTestBase, input) {
  auto input = getInput("÷ 034F × 0308 × 034F ÷");
  std::vector<int> expect = {0x034F, 0x0308, 0x034F};
  ASSERT_EQ(expect, input);

  input = getInput("÷ 034F ÷ 000A ÷");
  expect = {0x034F, 0x000A};
  ASSERT_EQ(expect, input);
}

TEST(GraphemeBreakTestBase, expect) {
  auto input = getExpected("÷ 034F × 0308 × 034F ÷");
  std::vector<std::vector<int>> expect = {{0x034F, 0x0308, 0x034F}};
  ASSERT_EQ(expect, input);

  input = getExpected("÷ 034F ÷ 000A ÷");
  expect = {{0x034F}, {0x000A}};
  ASSERT_EQ(expect, input);
}

TEST(GraphemeBreakTestBase, scan1) {
  Utf8GraphemeScanner scanner("abc");
  ASSERT_TRUE(scanner.hasNext());
  GraphemeCluster ret;

  scanner.next(ret);
  ASSERT_EQ("a", ret.getRef());
  ASSERT_EQ('a', ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_TRUE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_EQ("b", ret.getRef());
  ASSERT_EQ('b', ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_TRUE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_EQ("c", ret.getRef());
  ASSERT_EQ('c', ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_FALSE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_EQ(0, ret.getRef().size());
  ASSERT_EQ(0, ret.getCodePointCount());
  ASSERT_FALSE(scanner.hasNext());

  scanner = Utf8GraphemeScanner("🇯🇵");
  scanner.next(ret);
  ASSERT_EQ("🇯🇵", ret.getRef());
  ASSERT_EQ(0x1F1E6 + ('j' - 'a'), ret.getCodePointAt(0));
  ASSERT_EQ(0x1F1E6 + ('p' - 'a'), ret.getCodePointAt(1));
  ASSERT_EQ(2, ret.getCodePointCount());
  ASSERT_FALSE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_FALSE(scanner.hasNext());

  scanner = Utf8GraphemeScanner("");
  ASSERT_FALSE(scanner.hasNext());
  scanner.next(ret);
  ASSERT_FALSE(scanner.hasNext());

  scanner = Utf8GraphemeScanner(StringRef("\0", 1));
  ASSERT_TRUE(scanner.hasNext());
  scanner.next(ret);
  ASSERT_FALSE(scanner.hasNext());
}

TEST(GraphemeBreakTestBase, scan2) {
  Utf8GraphemeScanner scanner("\xC2\x24\xE0\xA4\xC2\xE0\xB8\xB3");
  ASSERT_TRUE(scanner.hasNext());
  GraphemeCluster ret;

  scanner.next(ret);
  ASSERT_EQ("\xC2", ret.getRef());
  ASSERT_EQ(-1, ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_TRUE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_EQ("\x24", ret.getRef());
  ASSERT_EQ(0x24, ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_TRUE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_EQ("\xE0", ret.getRef());
  ASSERT_EQ(-1, ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_TRUE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_EQ("\xA4", ret.getRef());
  ASSERT_EQ(-1, ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_TRUE(scanner.hasNext());

  // break before spacing mark U+0E33(E0 B8 B3), if broken code point
  scanner.next(ret);
  ASSERT_EQ("\xC2", ret.getRef());
  ASSERT_EQ(-1, ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_TRUE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_EQ("\xE0\xB8\xB3", ret.getRef());
  ASSERT_EQ(0x0E33, ret.getCodePointAt(0));
  ASSERT_EQ(1, ret.getCodePointCount());
  ASSERT_FALSE(scanner.hasNext());

  scanner.next(ret);
  ASSERT_FALSE(scanner.hasNext());
}

TEST_P(GraphemeBreakTest, base) {
  ASSERT_NO_FATAL_FAILURE(doTest());
  ASSERT_NO_FATAL_FAILURE(doTest2());
}

static std::vector<std::string> getGraphemeTargets() {
#include GRAPHEME_BREAK_TEST_H
  std::vector<std::string> ret;
  for (auto &e : grapheme_break_tests) {
    ret.emplace_back(e);
  }
  return ret;
}

INSTANTIATE_TEST_SUITE_P(GraphemeBreakTest, GraphemeBreakTest,
                         ::testing::ValuesIn(getGraphemeTargets()));

struct WordBreakTest : public ::testing::TestWithParam<std::string> {
  static void doTest() {
    auto input = getInput(GetParam());
    auto expected = getExpected(GetParam());

    ASSERT_FALSE(input.empty());
    ASSERT_FALSE(expected.empty());
    for (auto &e : expected) {
      ASSERT_FALSE(e.empty());
    }

    std::vector<std::vector<int>> output;
    output.emplace_back();
    CodePointStream stream(input);
    auto scanner = makeWordScanner(stream);
    for (unsigned int count = 0; stream; count++) {
      auto codePoint = input[count];
      if (scanner.scanBoundary()) {
        output.emplace_back();
      }
      output.back().push_back(codePoint);
    }

    ASSERT_EQ(expected, output);
  }

  static void doTest2() {
    auto input = getInput(GetParam());
    auto expected = getExpected(GetParam());

    ASSERT_FALSE(input.empty());
    ASSERT_FALSE(expected.empty());
    for (auto &e : expected) {
      ASSERT_FALSE(e.empty());
    }

    std::string inputStr = toUTF8(input);
    std::vector<std::string> expectedList;
    for (auto &e : expected) {
      expectedList.push_back(toUTF8(e));
    }

    Utf8Stream stream(inputStr.c_str(), inputStr.c_str() + inputStr.size());
    Utf8WordScanner scanner(stream);
    std::vector<std::string> outputList;
    while (scanner.hasNext()) {
      outputList.push_back(scanner.next().toString());
    }
    ASSERT_EQ(expectedList, outputList);
  }
};

TEST(WordBreakTestBase, base1) {
  StringRef ref = "\n1234";
  Utf8Stream stream(ref.begin(), ref.end());
  auto scanner = makeWordScanner(stream);

  // newline
  auto old1 = stream.iter;
  auto old2 = old1;
  while (true) {
    old2 = stream.iter;
    if (scanner.scanBoundary()) {
      break;
    }
  }
  auto consumed = StringRef(old1, old2 - old1).toString();
  ASSERT_EQ("\n", consumed);
  ASSERT_TRUE(stream);

  // 1234 i word
  old1 = old2;
  while (true) {
    old2 = stream.iter;
    if (scanner.scanBoundary()) {
      break;
    }
  }
  consumed = StringRef(old1, old2 - old1).toString();
  ASSERT_EQ("1234", consumed);
  ASSERT_FALSE(stream);

  // reach end
  old1 = old2;
  while (true) {
    old2 = stream.iter;
    if (scanner.scanBoundary()) {
      break;
    }
  }
  consumed = StringRef(old1, old2 - old1).toString();
  ASSERT_EQ("", consumed);
  ASSERT_FALSE(stream);
}

TEST(WordBreakTestBase, base2) {
  StringRef ref = "\r1234\n🇯🇵3.14";
  Utf8Stream stream(ref.begin(), ref.end());
  Utf8WordScanner scanner(stream);

  ASSERT_TRUE(scanner.hasNext());

  // \r
  auto consumed = scanner.next().toString();
  ASSERT_EQ("\r", consumed);
  ASSERT_TRUE(scanner.hasNext());

  // 1234
  consumed = scanner.next().toString();
  ASSERT_EQ("1234", consumed);
  ASSERT_TRUE(scanner.hasNext());

  // \n
  consumed = scanner.next().toString();
  ASSERT_EQ("\n", consumed);
  ASSERT_TRUE(scanner.hasNext());

  // 🇯🇵
  consumed = scanner.next().toString();
  ASSERT_EQ("🇯🇵", consumed);
  ASSERT_TRUE(scanner.hasNext());

  //  3.14
  consumed = scanner.next().toString();
  ASSERT_EQ("3.14", consumed);
  ASSERT_FALSE(scanner.hasNext());

  //
  consumed = scanner.next().toString();
  ASSERT_EQ("", consumed);
  ASSERT_FALSE(scanner.hasNext());
}

TEST_P(WordBreakTest, base) {
  ASSERT_NO_FATAL_FAILURE(doTest());
  ASSERT_NO_FATAL_FAILURE(doTest2());
}

static std::vector<std::string> getWordTargets() {
#include WORD_BREAK_TEST_H
  std::vector<std::string> ret;
  for (auto &e : word_break_tests) {
    ret.emplace_back(e);
  }
  return ret;
}

INSTANTIATE_TEST_SUITE_P(WordBreakTest, WordBreakTest, ::testing::ValuesIn(getWordTargets()));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}