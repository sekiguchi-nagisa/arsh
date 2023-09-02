#include "gtest/gtest.h"

#include "../test_common.h"
#include <misc/files.hpp>

using namespace ydsh;

class FuzzTest : public ::testing::TestWithParam<std::string> {
private:
  std::string targetName;

public:
  FuzzTest() = default;
  virtual ~FuzzTest() = default;

  virtual void SetUp() { this->targetName = this->GetParam(); }

  virtual void TearDown() {}

  virtual void doTest() {
    ProcBuilder builder = {
        BIN_PATH,
        this->targetName.c_str(),
    };
    auto status = builder.exec();
    ASSERT_EQ(0, status.value);
  }
};

INSTANTIATE_TEST_SUITE_P(FuzzTest, FuzzTest, ::testing::ValuesIn(getFileList(FUZZ_TEST_DIR, true)));

TEST_P(FuzzTest, base) { ASSERT_NO_FATAL_FAILURE(this->doTest()); }

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}