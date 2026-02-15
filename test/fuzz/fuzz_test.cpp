#include "gtest/gtest.h"

#include "../test_common.h"
#include <misc/files.hpp>

#ifndef BIN_PATH
#error require BIN_PATH
#endif

#ifndef RE_BIN_PATH
#error require RE_BIN_PATH
#endif

#ifndef FUZZ_TEST_DIR
#error require FUZZ_TEST_DIR
#endif

#ifndef RE_FUZZ_TEST_DIR
#error require RE_FUZZ_TEST_DIR
#endif

using namespace arsh;

class FuzzTest : public ::testing::TestWithParam<std::string> {
private:
  std::string targetName;

public:
  virtual void SetUp() { this->targetName = this->GetParam(); }

  virtual void doTest() {
    ProcBuilder builder = {
        BIN_PATH,
        this->targetName.c_str(),
    };
    auto status = builder.exec();
    ASSERT_EQ(0, status.value);
  }

  virtual void doTestRegex() {
    ProcBuilder builder = {
        RE_BIN_PATH,
        this->targetName.c_str(),
    };
    auto status = builder.exec();
    ASSERT_EQ(0, status.value);
  }
};

INSTANTIATE_TEST_SUITE_P(FuzzTest, FuzzTest, ::testing::ValuesIn(getFileList(FUZZ_TEST_DIR, true)));

INSTANTIATE_TEST_SUITE_P(FuzzTestRe, FuzzTest,
                         ::testing::ValuesIn(getFileList(RE_FUZZ_TEST_DIR, true)));

TEST_P(FuzzTest, base) {
  ASSERT_NO_FATAL_FAILURE(this->doTest());
  ASSERT_NO_FATAL_FAILURE(this->doTestRegex());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}