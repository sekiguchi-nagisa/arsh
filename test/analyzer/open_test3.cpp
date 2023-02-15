
#include "open_test.hpp"

TEST_P(OpenTest, base) { ASSERT_NO_FATAL_FAILURE(doTest()); }

INSTANTIATE_TEST_SUITE_P(OpenTest3_cli, OpenTest,
                         ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR "/cli")));

INSTANTIATE_TEST_SUITE_P(OpenTest3_module, OpenTest,
                         ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR "/module")));

INSTANTIATE_TEST_SUITE_P(OpenTest3_syntax, OpenTest,
                         ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR "/syntax")));

INSTANTIATE_TEST_SUITE_P(OpenTest3_output, OpenTest,
                         ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR "/output")));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}