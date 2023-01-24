
#include "open_test.hpp"

TEST_P(OpenTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(OpenTest1, OpenTest, ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR "/base")));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}