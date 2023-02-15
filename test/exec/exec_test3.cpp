#include "exec_test_base.hpp"

TEST_P(ExecTest, baseTest) {
  printf("@@ test script %s\n", this->targetName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doTestWithLitecheck());
}

INSTANTIATE_TEST_SUITE_P(ExecTest, ExecTest,
                         ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR "/output")));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  if (chdir(EXEC_TEST_DIR) != 0) {
    fatal("broken test directory: %s\n", EXEC_TEST_DIR);
  }
  return RUN_ALL_TESTS();
}