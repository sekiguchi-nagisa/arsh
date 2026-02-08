
#include "../test_common.h"

#include <regex/dump.h>
#include <regex/flag.hpp>
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

  flag = regex::Flag::parse("vimissvsssssmmm", nullptr);
  ASSERT_TRUE(flag.hasValue());
  ASSERT_TRUE(flag.unwrap().is(regex::Mode::UNICODE_SET));
  ASSERT_TRUE(flag.unwrap().has(regex::Modifier::IGNORE_CASE | regex::Modifier::DOT_ALL |
                                regex::Modifier::MULTILINE));

  std::string err;
  flag = regex::Flag::parse("ymq", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("invalid regex flag: `y'", err);

  err.clear();
  flag = regex::Flag::parse("mmmgu", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("invalid regex flag: `g'", err);

  err.clear();
  flag = regex::Flag::parse("mummv", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("cannot specify `v' flag, since `u' flag has already been specified", err);

  err.clear();
  flag = regex::Flag::parse("vvviu", &err);
  ASSERT_FALSE(flag.hasValue());
  ASSERT_EQ("cannot specify `u' flag, since `v' flag has already been specified", err);
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