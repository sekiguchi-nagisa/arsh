
#include "../test_common.h"

#include <regex/dump.h>
#include <regex/emit.h>
#include <regex/flag.h>
#include <regex/input.h>
#include <regex/parser.h>

using namespace arsh;

TEST(RegexFlag, base) {
  auto flag = regex::Flag::parse("", nullptr); // default
  ASSERT_TRUE(flag.hasValue());
  ASSERT_TRUE(flag.unwrap().is(regex::Mode::UNICODE));

  flag = regex::Flag::parse("uims", nullptr);
  ASSERT_TRUE(flag.hasValue());
  ASSERT_TRUE(flag.unwrap().is(regex::Mode::UNICODE));
  ASSERT_TRUE(flag.unwrap().has(regex::Modifier::IGNORE_CASE | regex::Modifier::DOT_ALL |
                                regex::Modifier::MULTILINE));

  std::string err;
  flag = regex::Flag::parse("vimissvsssssmmm", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("`i' modifier has already been specified", err);

  err.clear();
  flag = regex::Flag::parse("vmissvsssssmmm", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("`s' modifier has already been specified", err);
  err.clear();
  flag = regex::Flag::parse("vmisvsssssmmm", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("flag has already been specified", err);

  err.clear();
  flag = regex::Flag::parse("vmismmm", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("`m' modifier has already been specified", err);

  err.clear();
  flag = regex::Flag::parse("ymq", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("invalid regex flag: `y'", err);

  err.clear();
  flag = regex::Flag::parse("mgu", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("invalid regex flag: `g'", err);

  err.clear();
  flag = regex::Flag::parse("mummv", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("`m' modifier has already been specified", err);

  err.clear();
  flag = regex::Flag::parse("vvviu", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("flag has already been specified", err);
}

TEST(RegexFlag, onlyModifier) {
  std::string err;
  auto modifiers = regex::Flag::parseModifier("a", &err);
  ASSERT_FALSE(modifiers.hasValue());
  ASSERT_EQ("invalid modifier: `a'", err);

  err.clear();
  modifiers = regex::Flag::parseModifier("v", &err);
  ASSERT_FALSE(modifiers.hasValue());
  ASSERT_EQ("invalid modifier: `v'", err);

  err.clear();
  modifiers = regex::Flag::parseModifier("u", &err);
  ASSERT_FALSE(modifiers.hasValue());
  ASSERT_EQ("invalid modifier: `u'", err);

  err.clear();
  modifiers = regex::Flag::parseModifier("imi", &err);
  ASSERT_FALSE(modifiers.hasValue());
  ASSERT_EQ("`i' modifier has already been specified", err);

  modifiers = regex::Flag::parseModifier("", nullptr);
  ASSERT_TRUE(modifiers.hasValue());
  ASSERT_TRUE(hasFlag(modifiers.unwrap(), regex::Modifier::NONE));
  ASSERT_EQ(regex::Modifier::NONE, modifiers.unwrap());

  modifiers = regex::Flag::parseModifier("i", nullptr);
  ASSERT_TRUE(modifiers.hasValue());
  ASSERT_TRUE(hasFlag(modifiers.unwrap(), regex::Modifier::IGNORE_CASE));
}

TEST(RegexFlag, str) {
  regex::Flag flag;
  ASSERT_EQ("", flag.str());
  flag = regex::Flag(regex::Mode::BMP, regex::Modifier::DOT_ALL);
  ASSERT_EQ("s", flag.str());
  flag = regex::Flag(regex::Mode::UNICODE, regex::Modifier::MULTILINE);
  ASSERT_EQ("mu", flag.str());
  flag = regex::Flag(regex::Mode::UNICODE_SET,
                     regex::Modifier::MULTILINE | regex::Modifier::IGNORE_CASE);
  ASSERT_EQ("imv", flag.str());
}

TEST(InputTest, base) {
  {
    StringRef text = "12あ\xFFい\xFF";
    regex::Input input;
    ASSERT_EQ(regex::Input::Status::INVALID_UTF8, regex::Input::create(text, input));
  }

  {
    constexpr size_t size = static_cast<size_t>(UINT32_MAX) + 1;
    StringRef text("sss", size);
    ASSERT_EQ(size, text.size());
    ASSERT_TRUE(text.size() > regex::Input::INPUT_MAX);
    regex::Input input;
    ASSERT_EQ(regex::Input::Status::TOO_LARGE, regex::Input::create(text, input));
  }
}

TEST(InputTest, forward) {
  StringRef text = "12ÿþあ𤅕い";
  regex::Input input;
  ASSERT_EQ(regex::Input::Status::OK, regex::Input::create(text, input));

  ASSERT_TRUE(input.isBegin());
  ASSERT_EQ('1', input.cur());
  ASSERT_EQ('1', input.consumeForward());
  ASSERT_TRUE(input.available());
  ASSERT_EQ('2', input.cur());
  ASSERT_EQ('2', input.consumeForward());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0xFF, input.cur());
  ASSERT_EQ(0xFF, input.consumeForward());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0xFE, input.cur());
  ASSERT_EQ(0xFE, input.consumeForward());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0x3042, input.cur());
  ASSERT_EQ(0x3042, input.consumeForward());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0x24155, input.cur());
  ASSERT_EQ(0x24155, input.consumeForward());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0x3044, input.cur());
  ASSERT_EQ(0x3044, input.consumeForward());
  ASSERT_TRUE(input.isEnd());
  ASSERT_FALSE(input.available());
}

TEST(InputTest, backward) {
  // 1byte
  {
    StringRef text = "1";
    regex::Input input;
    ASSERT_EQ(regex::Input::Status::OK, regex::Input::create(text, input));
    while (input.available()) {
      input.consumeForward();
    }
    ASSERT_TRUE(input.isEnd());
    ASSERT_FALSE(input.available());
    ASSERT_EQ('\0', *input.getIter());
    ASSERT_EQ('1', input.prev());
    ASSERT_EQ('1', input.consumeBackward());
    ASSERT_EQ('1', input.cur());
  }

  // 2 byte
  {
    StringRef text = "ÿ";
    regex::Input input;
    ASSERT_EQ(regex::Input::Status::OK, regex::Input::create(text, input));
    while (input.available()) {
      input.consumeForward();
    }
    ASSERT_TRUE(input.isEnd());
    ASSERT_FALSE(input.available());
    ASSERT_EQ('\0', *input.getIter());
    ASSERT_EQ(0xFF, input.prev());
    ASSERT_EQ(0xFF, input.consumeBackward());
    ASSERT_EQ(0xFF, input.cur());
  }

  // 3 byte
  {
    StringRef text = "あ";
    regex::Input input;
    ASSERT_EQ(regex::Input::Status::OK, regex::Input::create(text, input));
    while (input.available()) {
      input.consumeForward();
    }
    ASSERT_TRUE(input.isEnd());
    ASSERT_FALSE(input.available());
    ASSERT_EQ('\0', *input.getIter());
    ASSERT_EQ(0x3042, input.prev());
    ASSERT_EQ(0x3042, input.consumeBackward());
    ASSERT_EQ(0x3042, input.cur());
  }

  // 4byte
  {
    StringRef text = "𤅕";
    regex::Input input;
    ASSERT_EQ(regex::Input::Status::OK, regex::Input::create(text, input));
    while (input.available()) {
      input.consumeForward();
    }
    ASSERT_TRUE(input.isEnd());
    ASSERT_FALSE(input.available());
    ASSERT_EQ('\0', *input.getIter());
    ASSERT_EQ(0x24155, input.prev());
    ASSERT_EQ(0x24155, input.consumeBackward());
    ASSERT_EQ(0x24155, input.cur());
  }

  StringRef text = "12ÿþあ𤅕い";
  regex::Input input;
  ASSERT_EQ(regex::Input::Status::OK, regex::Input::create(text, input));
  while (input.available()) {
    input.consumeForward();
  }
  ASSERT_TRUE(input.isEnd());
  ASSERT_FALSE(input.available());
  ASSERT_EQ(0x3044, input.prev());
  ASSERT_EQ(0x3044, input.consumeBackward());
  ASSERT_EQ(0x3044, input.cur());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0x24155, input.prev());
  ASSERT_EQ(0x24155, input.consumeBackward());
  ASSERT_EQ(0x24155, input.cur());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0x3042, input.prev());
  ASSERT_EQ(0x3042, input.consumeBackward());
  ASSERT_EQ(0x3042, input.cur());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0xFE, input.prev());
  ASSERT_EQ(0xFE, input.consumeBackward());
  ASSERT_EQ(0xFE, input.cur());
  ASSERT_TRUE(input.available());
  ASSERT_EQ(0xFF, input.prev());
  ASSERT_EQ(0xFF, input.consumeBackward());
  ASSERT_EQ(0xFF, input.cur());
  ASSERT_TRUE(input.available());
  ASSERT_EQ('2', input.prev());
  ASSERT_EQ('2', input.consumeBackward());
  ASSERT_EQ('2', input.cur());
  ASSERT_TRUE(input.available());
  ASSERT_EQ('1', input.prev());
  ASSERT_EQ('1', input.consumeBackward());
  ASSERT_EQ('1', input.cur());
  ASSERT_TRUE(input.available());
  ASSERT_TRUE(input.isBegin());
}

static Optional<regex::Regex> compile(StringRef pattern, regex::Flag flag = regex::Flag()) {
  regex::Parser parser;
  if (auto tree = parser(pattern, flag)) {
    regex::CodeGen codegen;
    return codegen(std::move(tree));
  }
  return {};
}

TEST(RegexMatchTest, null) {
  char data[] = {'a', 'b', '\0', 'c', 'd'};
  StringRef text(data, sizeof(data));

  auto re = compile("ab.");
  ASSERT_TRUE(re.hasValue());
  FlexBuffer<regex::Capture> captures;
  auto status = regex::match(re.unwrap(), text, captures);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(0, captures[0].offset);
  ASSERT_EQ(3, captures[0].size);

  re = compile("\\0");
  ASSERT_TRUE(re.hasValue());
  status = regex::match(re.unwrap(), text, captures);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(2, captures[0].offset);
  ASSERT_EQ(1, captures[0].size);

  re = compile("\\00");
  ASSERT_TRUE(re.hasValue());
  status = regex::match(re.unwrap(), text, captures);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(2, captures[0].offset);
  ASSERT_EQ(1, captures[0].size);

  re = compile("\\00", regex::Flag(regex::Mode::UNICODE, {}));
  ASSERT_FALSE(re.hasValue());
}

TEST(MatcherTest, ascii) {
  regex::AsciiSet set;
  set.add(7);
  ASSERT_TRUE(set.contains(7));
  set.add(8);
  ASSERT_TRUE(set.contains(8));
  set.add(12);
  ASSERT_TRUE(set.contains(12));
  set.add(64);
  ASSERT_TRUE(set.contains(64));
  set.add(3);
  ASSERT_TRUE(set.contains(3));
  set.add(127);
  ASSERT_TRUE(set.contains(127));
  set.add(128);
  ASSERT_FALSE(set.contains(128));

  regex::Matcher matcher(set);
  ASSERT_FALSE(matcher.contains(3)); // truncate 0~7
  ASSERT_FALSE(matcher.asAsciiSet().contains(3));
  ASSERT_FALSE(matcher.contains(7)); // truncate 0~7
  ASSERT_FALSE(matcher.asAsciiSet().contains(7));
  ASSERT_TRUE(matcher.contains(8));
  ASSERT_TRUE(matcher.asAsciiSet().contains(8));
  ASSERT_TRUE(matcher.contains(12));
  ASSERT_TRUE(matcher.asAsciiSet().contains(12));
  ASSERT_TRUE(matcher.contains(64));
  ASSERT_TRUE(matcher.asAsciiSet().contains(64));
  ASSERT_TRUE(matcher.contains(127));
  ASSERT_TRUE(matcher.asAsciiSet().contains(127));
  ASSERT_FALSE(matcher.contains(128));
  ASSERT_FALSE(matcher.asAsciiSet().contains(128));

  const char *expect = R"(AsciiSet
{ 0x0008, 0x0008 },
{ 0x000C, 0x000C },
{ 0x0040, 0x0040 },
{ 0x007F, 0x007F },
)";
  std::string actual;
  toString(matcher, actual);
  ASSERT_EQ(expect, actual);
}

TEST(MatcherTest, codePointSet) {
  {
    auto set = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassDigit));
    regex::Matcher matcher(std::move(set));
    const char *expect = R"(CodePointSet(borrowed)
{ 0x0030, 0x0039 },
)";
    std::string actual;
    toString(matcher, actual);
    ASSERT_EQ(expect, actual);
  }

  {
    auto set = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassWord));
    regex::Matcher matcher(std::move(set));
    const char *expect = R"(CodePointSet(borrowed)
{ 0x0030, 0x0039 },
{ 0x0041, 0x005A },
{ 0x005F, 0x005F },
{ 0x0061, 0x007A },
)";
    std::string actual;
    toString(matcher, actual);
    ASSERT_EQ(expect, actual);
  }

  {
    auto set = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::Any));
    regex::Matcher matcher(std::move(set));
    const char *expect = R"(CodePointSet(borrowed)
{ 0x0000, 0xFFFF },
{ 0x10000, 0x10FFFF },
)";
    std::string actual;
    toString(matcher, actual);
    ASSERT_EQ(expect, actual);
  }

  {
    auto set = ucp::getPropertySet(ucp::Property::category(ucp::Category::Z));
    regex::Matcher matcher(std::move(set));
    const char *expect = R"(CodePointSet(owned)
{ 0x0020, 0x0020 },
{ 0x00A0, 0x00A0 },
{ 0x1680, 0x1680 },
{ 0x2000, 0x200A },
{ 0x2028, 0x2029 },
{ 0x202F, 0x202F },
{ 0x205F, 0x205F },
{ 0x3000, 0x3000 },
)";
    std::string actual;
    toString(matcher, actual);
    ASSERT_EQ(expect, actual);
  }
}

struct BMPCodePointRangeEntry {
  const BMPCodePointRange *ptr;
  unsigned int size;

  template <unsigned int N>
  constexpr BMPCodePointRangeEntry(const BMPCodePointRange (&data)[N]) noexcept // NOLINT
      : ptr(data), size(std::size(data)) {}
};

struct CharSetMatcherEntry {
  const char *pattern;
  const char *flag;
  unsigned short bmpSize;
  BMPCodePointRangeEntry entry;
};

std::ostream &operator<<(std::ostream &stream, const CharSetMatcherEntry &e) {
  return stream << e.pattern;
}

struct CharSetMatcherTest : public ::testing::TestWithParam<CharSetMatcherEntry> {
  static void doTest() {
    std::string err;
    auto flag = regex::Flag::parse(GetParam().flag, regex::Mode::BMP, &err);
    ASSERT_EQ("", err);
    ASSERT_TRUE(flag.hasValue());
    regex::Parser parser;
    auto tree = parser(GetParam().pattern, flag.unwrap());
    ASSERT_FALSE(parser.hasError());
    ASSERT_TRUE(tree);
    regex::CodeGen codeGen;
    auto re = codeGen(std::move(tree));
    ASSERT_TRUE(re.hasValue());
    ASSERT_EQ("", codeGen.getError());
    ASSERT_EQ(1, re.unwrap().getMatchers().size()); // exact one matcher

    std::string expected;
    auto &param = GetParam();
    toString(
        regex::Matcher(CodePointSet::borrow(param.bmpSize, 0, param.entry.ptr, param.entry.size)),
        expected, false);

    std::string actual;
    toString(re.unwrap().getMatchers()[0], actual, false);
    ASSERT_EQ(expected, actual);
  }
};

TEST_P(CharSetMatcherTest, base) {
  printf(" case: %s\n", this->GetParam().pattern);
  ASSERT_NO_FATAL_FAILURE(doTest());
}

#include "charset_matcher_test.in"

INSTANTIATE_TEST_SUITE_P(CharSetMatcherTest, CharSetMatcherTest,
                         ::testing::ValuesIn(charSetMatcherCases));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}