
#include "../test_common.h"

#include <regex/dump.h>
#include <regex/flag.h>
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

struct RegexSyntaxTest : public ::testing::TestWithParam<std::string> {
  static void doTest() {
    auto result =
        ProcBuilder{BIN_PATH, GetParam().c_str()}.addEnv("REDUMP_PATH", REDUMP_PATH).exec();
    ASSERT_EQ(WaitStatus::EXITED, result.kind);
    ASSERT_EQ(0, result.value);
  }
};

TEST_P(RegexSyntaxTest, base) {
  printf(" case: %s\n", this->GetParam().c_str());
  ASSERT_NO_FATAL_FAILURE(doTest());
}

INSTANTIATE_TEST_SUITE_P(RegexSyntaxTest, RegexSyntaxTest,
                         ::testing::ValuesIn(getTargetTestCases(REGEX_TEST_DIR)));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}