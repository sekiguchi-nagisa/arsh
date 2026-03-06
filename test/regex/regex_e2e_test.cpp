
#include "../test_common.h"

#ifndef BIN_PATH
#error require BIN_PATH
#endif

#ifndef REDUMP_PATH
#error require REDUMP_PATH
#endif

#ifndef REGEX_TEST_DIR
#error require REGEX_TEST_DIR
#endif

using namespace arsh;

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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}