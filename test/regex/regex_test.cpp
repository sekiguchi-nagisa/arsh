
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

#ifndef BIN_PATH
#error require BIN_PATH
#endif

#ifndef REDUMP_PATH
#error require REDUMP_PATH
#endif

#ifndef REGEX_TEST_DIR
#error require REGEX_TEST_DIR
#endif

static std::vector<std::string> getTargetTestCases(const char *dir) {
  auto ret = getFileList(dir, true);
  assert(!ret.empty());
  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [](const std::string &v) { return !StringRef(v).endsWith(".arsh"); }),
            ret.end());
  std::sort(ret.begin(), ret.end());
  return ret;
}

struct RegexTest : public ::testing::TestWithParam<std::string> {
  static void doTest() {
    auto result =
        ProcBuilder{BIN_PATH, GetParam().c_str()}.addEnv("REDUMP_PATH", REDUMP_PATH).exec();
    ASSERT_EQ(WaitStatus::EXITED, result.kind);
    ASSERT_EQ(0, result.value);
  }
};

TEST_P(RegexTest, base) {
  printf(" case: %s\n", this->GetParam().c_str());
  ASSERT_NO_FATAL_FAILURE(doTest());
}

INSTANTIATE_TEST_SUITE_P(RegexTest, RegexTest,
                         ::testing::ValuesIn(getTargetTestCases(REGEX_TEST_DIR)));

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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}