
#include "open_test.hpp"

TEST_P(OpenTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(OpenTest2, OpenTest,
                         ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR "/always_failed")));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}