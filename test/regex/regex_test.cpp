
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

TEST(InputTest, remainForward) {
  StringRef text = "12ÿþあ𤅕い";
  regex::Input input;
  ASSERT_EQ(regex::Input::Status::OK, regex::Input::create(text, input));

  ASSERT_EQ("1", input.remainForwardAtLeast(1).toString());
  ASSERT_EQ("12", input.remainForwardAtLeast(2).toString());
  ASSERT_EQ("12ÿ", input.remainForwardAtLeast(3).toString());
  ASSERT_EQ("12ÿ", input.remainForwardAtLeast(4).toString());
  ASSERT_EQ("12ÿþ", input.remainForwardAtLeast(5).toString());
  ASSERT_EQ("12ÿþ", input.remainForwardAtLeast(6).toString());
  ASSERT_EQ("12ÿþあ", input.remainForwardAtLeast(7).toString());
  ASSERT_EQ("12ÿþあ𤅕い", input.remainForwardAtLeast(60).toString());

  while (input.available()) {
    input.consumeForward();
  }
  ASSERT_EQ("", input.remainForwardAtLeast(1).toString());
}

TEST(InputTest, remainBackward) {
  StringRef text = "12ÿþあ𤅕い";
  regex::Input input;
  ASSERT_EQ(regex::Input::Status::OK, regex::Input::create(text, input));
  ASSERT_EQ("", input.remainBackwardAtLeast(1).toString());
  ASSERT_EQ("", input.remainBackwardAtLeast(10).toString());

  while (input.available()) {
    input.consumeForward();
  }
  ASSERT_EQ("い", input.remainBackwardAtLeast(1).toString());
  ASSERT_EQ("い", input.remainBackwardAtLeast(2).toString());
  ASSERT_EQ("い", input.remainBackwardAtLeast(3).toString());
  ASSERT_EQ("𤅕い", input.remainBackwardAtLeast(4).toString());
  ASSERT_EQ("𤅕い", input.remainBackwardAtLeast(5).toString());
  ASSERT_EQ("𤅕い", input.remainBackwardAtLeast(6).toString());
  ASSERT_EQ("𤅕い", input.remainBackwardAtLeast(7).toString());
  ASSERT_EQ("あ𤅕い", input.remainBackwardAtLeast(8).toString());
  ASSERT_EQ("12ÿþあ𤅕い", input.remainBackwardAtLeast(69).toString());
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
  std::vector<regex::Capture> captures;
  auto status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(0, captures[0].offset);
  ASSERT_EQ(3, captures[0].size);

  re = compile("\\0");
  ASSERT_TRUE(re.hasValue());
  status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(2, captures[0].offset);
  ASSERT_EQ(1, captures[0].size);

  re = compile("\\0", regex::Flag(regex::Mode::UNICODE, regex::Modifier::IGNORE_CASE));
  ASSERT_TRUE(re.hasValue());
  status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(2, captures[0].offset);
  ASSERT_EQ(1, captures[0].size);

  re = compile("(?<=\\0)");
  ASSERT_TRUE(re.hasValue());
  status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(3, captures[0].offset);
  ASSERT_EQ(0, captures[0].size);

  re = compile("(?<=\\0)", regex::Flag(regex::Mode::UNICODE, regex::Modifier::IGNORE_CASE));
  ASSERT_TRUE(re.hasValue());
  status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(3, captures[0].offset);
  ASSERT_EQ(0, captures[0].size);

  re = compile("\\00");
  ASSERT_TRUE(re.hasValue());
  status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(2, captures[0].offset);
  ASSERT_EQ(1, captures[0].size);

  re = compile("(?<=\\00)");
  ASSERT_TRUE(re.hasValue());
  status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, status);
  ASSERT_EQ(1, captures.size());
  ASSERT_EQ(3, captures[0].offset);
  ASSERT_EQ(0, captures[0].size);

  re = compile("\\00", regex::Flag(regex::Mode::UNICODE, {}));
  ASSERT_FALSE(re.hasValue());
}

TEST(RegexMatchTest, error) {
  auto re = compile("ab.");
  ASSERT_TRUE(re.hasValue());
  std::vector<regex::Capture> captures;
  StringRef text = "\xFF\xFF\xFF";
  auto status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::INVALID_UTF8, status);

  text = StringRef("ss", static_cast<size_t>(UINT32_MAX) * 2);
  status = regex::match(re.unwrap(), text, captures, nullptr);
  ASSERT_EQ(regex::MatchStatus::INPUT_LIMIT, status);
}

class RegexReplaceTest : public ::testing::Test {
public:
  struct TestParam {
    StringRef text;
    StringRef replace;
    bool global;
  };

  static void assertReplace(const StringRef pattern, const regex::Flag flag, const TestParam &param,
                            const std::string &expectOut) {
    auto re = compile(pattern, flag);
    ASSERT_TRUE(re.hasValue());
    std::string out;
    std::string err;
    auto appender = [&out](StringRef ref) {
      out += ref;
      return true;
    };
    regex::ReplaceParam replaceParam = {
        .text = param.text,
        .replacement = param.replace,
        .global = param.global,
        .err = &err,
        .consumer = appender,
    };
    auto s = regex::replace(re.unwrap(), replaceParam, nullptr);
    ASSERT_EQ(regex::MatchStatus::OK, s);
    ASSERT_EQ(expectOut, out);
    ASSERT_EQ("", err);
  }

  static void assertReplaceError(const StringRef pattern, const regex::Flag flag,
                                 const TestParam &param, regex::MatchStatus expectStatus,
                                 const std::string &expectErr) {
    auto re = compile(pattern, flag);
    ASSERT_TRUE(re.hasValue());
    std::string out;
    std::string err;
    auto appender = [&out](StringRef ref) {
      out += ref;
      return out.size() < 1024;
    };
    regex::ReplaceParam replaceParam = {
        .text = param.text,
        .replacement = param.replace,
        .global = param.global,
        .err = &err,
        .consumer = appender,
    };
    auto s = regex::replace(re.unwrap(), replaceParam, nullptr);
    ASSERT_EQ(expectStatus, s);
    ASSERT_EQ(expectErr, err);
  }
};

TEST_F(RegexReplaceTest, replace) {
  ASSERT_NO_FATAL_FAILURE(assertReplace("", {}, {"", "@", true}, "@"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("", {}, {"", "@", false}, "@"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("", {}, {"AB", "@", true}, "@A@B@"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("", {}, {"AB", "@", false}, "@AB"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("\\d+|", {}, {"", "@", true}, "@"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("\\d+|", {}, {"", "@", false}, "@"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+", {}, {"2025-12-23 (火)", "@", false}, "@-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("\\d+", {}, {"2025-12-23", "@", true}, "@-@-@"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("\\d+", {}, {"2025-12-23", "@", false}, "@-12-23"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("\\d+", {}, {"2025-12-23 (火)", "@", true}, "@-@-@ (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+", {}, {"2025-12-23 (火)", "@", false}, "@-12-23 (火)"));

  // no matches
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("@{4}", {}, {"2025-12-23 (火)", "", true}, "2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("@{4}", {}, {"2025-12-23 (火)", "", false}, "2025-12-23 (火)"));

  // $$
  ASSERT_NO_FATAL_FAILURE(assertReplace("\\d+", {}, {"2025-12-23 (火)", "$$", true}, "$-$-$ (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+", {}, {"2025-12-23 (火)", "$$", false}, "$-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+|", {}, {"2025-12-23 (火)", "$$", true}, "$$-$$-$$ $($火$)$"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+|", {}, {"2025-12-23 (火)", "$$", false}, "$-12-23 (火)"));

  // $&
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+", {}, {"2025-12-23 (火)", "$&", true}, "2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+", {}, {"2025-12-23 (火)", "$&", false}, "2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+|", {}, {"2025-12-23 (火)", "$&", true}, "2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+|", {}, {"2025-12-23 (火)", "$&", false}, "2025-12-23 (火)"));

  // $`
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+", {}, {"2025-12-23 (火)", "$`", true}, "-2025--2025-12- (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+", {}, {"2025-12-23 (火)", "$`", false}, "-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("\\d+|", {}, {"2025-12-23 (火)", "$`", true},
                                        "2025-2025-2025-12-2025-12-2025-12-23 2025-12-23 "
                                        "(2025-12-23 (火2025-12-23 (火)2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+|", {}, {"2025-12-23 (火)", "$`", false}, "-12-23 (火)"));

  // $'
  ASSERT_NO_FATAL_FAILURE(assertReplace("\\d+", {}, {"2025-12-23 (火)", "$'", true},
                                        "-12-23 (火)--23 (火)- (火) (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+", {}, {"2025-12-23 (火)", "$'", false}, "-12-23 (火)-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+|", {}, {"2025-12-23 (火)", "$'", true},
                    "-12-23 (火)-12-23 (火)--23 (火)-23 (火)- (火) (火) (火)(火)火))"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("\\d+|", {}, {"2025-12-23 (火)", "$'", false}, "-12-23 (火)-12-23 (火)"));

  // $1
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(\\d+)", {}, {"2025-12-23 (火)", "$1", true}, "2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(\\d+)", {}, {"2025-12-23 (火)", "$1", false}, "2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(\\d+)|", {}, {"2025-12-23 (火)", "$1", true}, "2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(\\d+)|", {}, {"2025-12-23 (火)", "$1", false}, "2025-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(\\d)+|", {}, {"2025-12-23 (火)", "$1", true}, "5-2-3 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(\\d)+|", {}, {"2025-12-23 (火)", "$1", false}, "5-12-23 (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(\\d)+|()", {}, {"2025-12-23 (火)", "$2", true}, "-- (火)"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(\\d)+|()", {}, {"2025-12-23 (火)", "$2", false}, "-12-23 (火)"));

  // $<name>
  ASSERT_NO_FATAL_FAILURE(assertReplace("(?<year>\\d{4})-\\d{2}|\\d{2}-(?<year>\\d{4})", {},
                                        {"2025-12-23", "year=$<year>", true}, "year=2025-23"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("(?<year>\\d{4})-\\d{2}|\\d{2}-(?<year>\\d{4})", {},
                                        {"2025-12-23", "year=$<year>", false}, "year=2025-23"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("(?<year>\\d{4})-\\d{2}|\\d{2}-(?<year>\\d{4})", {},
                                        {"12-2025 23", "year=$<year>", true}, "year=2025 23"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("(?<year>\\d{4})-\\d{2}|\\d{2}-(?<year>\\d{4})", {},
                                        {"12-2025 23", "year=$<year>", false}, "year=2025 23"));
}

TEST_F(RegexReplaceTest, replaceError1) {
  // $
  ASSERT_NO_FATAL_FAILURE(assertReplaceError(".", {}, {"abc", "$", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "invalid replace pattern: `$'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("12", {}, {"abc", "$", true}, "abc"));

  // $Q
  ASSERT_NO_FATAL_FAILURE(assertReplaceError(".", {}, {"abc", "$Q", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "invalid replace pattern: `$'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("12", {}, {"abc", "$Q", true}, "abc"));

  // $00
  ASSERT_NO_FATAL_FAILURE(assertReplaceError(".", {}, {"abc", "$00", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "undefined capture group index: `00'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("12", {}, {"abc", "$00", true}, "abc"));

  // $0
  ASSERT_NO_FATAL_FAILURE(assertReplaceError(".", {}, {"abc", "$0", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "undefined capture group index: `0'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("12", {}, {"abc", "$0", true}, "abc"));

  // $3
  ASSERT_NO_FATAL_FAILURE(assertReplaceError("(.)", {}, {"abc", "$3", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "undefined capture group index: `3'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("(12)", {}, {"abc", "$3", true}, "abc"));

  // $9999999999999999999999999999
  ASSERT_NO_FATAL_FAILURE(
      assertReplaceError("(.)", {}, {"abc", "$9999999999999999999999999999", true},
                         regex::MatchStatus::INVALID_REPLACE_PATTERN,
                         "undefined capture group index: `9999999999999999999999999999'"));
  ASSERT_NO_FATAL_FAILURE(
      assertReplace("(12)", {}, {"abc", "$9999999999999999999999999999", true}, "abc"));

  // $<
  ASSERT_NO_FATAL_FAILURE(assertReplaceError("(.)", {}, {"abc", "$<", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "replace pattern `$<' must end with `>'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("(12)", {}, {"abc", "$<", true}, "abc"));
  // $<abc
  ASSERT_NO_FATAL_FAILURE(assertReplaceError("(.)", {}, {"abc", "$<abc", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "replace pattern `$<' must end with `>'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("(12)", {}, {"abc", "$<abc", true}, "abc"));

  // $<>
  ASSERT_NO_FATAL_FAILURE(assertReplaceError("(.)", {}, {"abc", "$<>", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "undefined capture group name: `'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("(12)", {}, {"abc", "$<>", true}, "abc"));

  // $<ABC>
  ASSERT_NO_FATAL_FAILURE(assertReplaceError("(?<AAA>.)", {}, {"abc", "@=$<ABC>", true},
                                             regex::MatchStatus::INVALID_REPLACE_PATTERN,
                                             "undefined capture group name: `ABC'"));
  ASSERT_NO_FATAL_FAILURE(assertReplace("(?<AAA>12)", {}, {"abc", "@=$<ABC>", true}, "abc"));
}

TEST_F(RegexReplaceTest, replaceError2) {
  ASSERT_NO_FATAL_FAILURE(assertReplaceError(
      "(.)(?=(.+))", {}, {"abcdefghijklmnopqrstu", "$2$2$2$2$2$2$`$'$`$'$`$'$`$'", true},
      regex::MatchStatus::REPLACED_LIMIT, ""));
  ASSERT_NO_FATAL_FAILURE(assertReplaceError("(.)(?=(.+))", {}, {"\xFF\xFF\xFF", "", true},
                                             regex::MatchStatus::INVALID_UTF8, ""));

  StringRef dummy("", static_cast<size_t>(UINT32_MAX) * 2);
  ASSERT_NO_FATAL_FAILURE(assertReplaceError("(.)(?=(.+))", {}, {dummy, "", true},
                                             regex::MatchStatus::INPUT_LIMIT, ""));
}

TEST_F(RegexReplaceTest, replaceNop) {
  auto re = compile("(.)(?=(.+))");
  ASSERT_TRUE(re.hasValue());
  const regex::ReplaceParam param = {
      .text = "abcdefghijklmnopqrstuvwxyzABC@",
      .replacement = "$2$2$2$2$2$2$`$'$`$'$`$'$`$'",
      .global = true,
      .err = nullptr,
      .consumer = nullptr,
  };
  auto s = regex::replace(re.unwrap(), param, nullptr);
  ASSERT_EQ(regex::MatchStatus::OK, s);
}

TEST_F(RegexReplaceTest, replaceTimeout) {
  auto re = compile("^(([a-zA-Z0-9あ-ん])+)+$");
  ASSERT_TRUE(re.hasValue());
  const regex::ReplaceParam param = {
      .text = "abcdefghijklmnopqrstuvwxyzABC@",
      .replacement = "",
      .global = true,
      .err = nullptr,
      .consumer = nullptr,
  };
  {
    regex::Timer timer(std::chrono::milliseconds(200));
    auto s = regex::replace(re.unwrap(), param, makeObserver(timer));
    ASSERT_EQ(regex::MatchStatus::TIMEOUT, s);
  }

  {
    regex::Timer timer(std::chrono::seconds(5));
    timer.setCancelToken([] { return true; });
    auto s = regex::replace(re.unwrap(), param, makeObserver(timer));
    ASSERT_EQ(regex::MatchStatus::CANCEL, s);
  }
}

struct RegexEscapeParam {
  StringRef in;
  StringRef out;
};

std::ostream &operator<<(std::ostream &stream, const RegexEscapeParam &param) {
  return stream << "original: " << param.in.toString() << ", escaped: " << param.out.toString();
}

struct RegexEscapeTest : public ::testing::TestWithParam<RegexEscapeParam> {
  static void doTest() {
    auto &param = GetParam();
    std::string out;
    ASSERT_TRUE(regex::escape(param.in, out.max_size(), out));
    ASSERT_EQ(param.out.toString(), out);
  }
};

TEST_P(RegexEscapeTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

static constexpr RegexEscapeParam regexEscapeParams[] = {
    {"", ""},
    {"A", "\\x41"},
    {"1ae", "\\x31ae"},
    {"foo", "\\x66oo"},
    {"_A", "_A"},
    {"$OSTYPE", "\\$OSTYPE"},
    {"(Hey)", "\\(Hey\\)"},
    {"foo-bar", "\\x66oo\\x2dbar"},
    {"^$\\.*+?()[]{}|/", R"(\^\$\\\.\*\+\?\(\)\[\]\{\}\|\/)"},
    {",-=<>#&!%:;@~'`\"", R"(\x2c\x2d\x3d\x3c\x3e\x23\x26\x21\x25\x3a\x3b\x40\x7e\x27\x60\x22)"},
    {"\f\n\r\t\v ", R"(\f\n\r\t\v\x20)"},
    {"foo\u2028bar", "\\x66oo\\u2028bar"},

    // not support surrogate
    /*{toUtf8({'f', 'o', 'o', 0xD800, 'b', 'a', 'r'}), "\\x66oo\\ud800bar"},*/
};

INSTANTIATE_TEST_SUITE_P(RegexEscapeTest, RegexEscapeTest, ::testing::ValuesIn(regexEscapeParams));

static std::vector<std::pair<std::string, std::string>> initAsciiOrLetterBaseCases() {
  std::vector<std::pair<std::string, std::string>> values;
  for (char c = 'a'; c <= 'z'; c++) {
    std::string before;
    before.resize(5, c);
    char data[16];
    snprintf(data, std::size(data), "\\x%02x", c);
    std::string after = data;
    after.append(4, c);
    values.emplace_back(std::move(before), std::move(after));
  }
  for (char c = 'A'; c <= 'Z'; c++) {
    std::string before;
    before.resize(5, c);
    char data[16];
    snprintf(data, std::size(data), "\\x%02x", c);
    std::string after = data;
    after.append(4, c);
    values.emplace_back(std::move(before), std::move(after));
  }
  for (char c = '0'; c <= '9'; c++) {
    std::string before;
    before.resize(5, c);
    char data[16];
    snprintf(data, std::size(data), "\\x%02x", c);
    std::string after = data;
    after.append(4, c);
    values.emplace_back(std::move(before), std::move(after));
  }
  return values;
}

static std::vector<RegexEscapeParam> getAsciiLetterOrDigitCases() {
  static const auto base = initAsciiOrLetterBaseCases();
  std::vector<RegexEscapeParam> params;
  for (auto &[before, after] : base) {
    params.push_back(RegexEscapeParam{.in = before, .out = after});
  }
  return params;
}

INSTANTIATE_TEST_SUITE_P(RegexEscapeTest2, RegexEscapeTest,
                         ::testing::ValuesIn(getAsciiLetterOrDigitCases()));

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
  return stream << "/" << e.pattern << "/" << e.flag;
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
  printf(" case: %s/%s\n", this->GetParam().pattern, this->GetParam().flag);
  ASSERT_NO_FATAL_FAILURE(doTest());
}

#include "charset_matcher_test.in"

INSTANTIATE_TEST_SUITE_P(CharSetMatcherTest, CharSetMatcherTest,
                         ::testing::ValuesIn(charSetMatcherCases));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}