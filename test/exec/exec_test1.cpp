#include "exec_test_base.hpp"

TEST_P(ExecTest, baseTest) {
  printf("@@ test script %s\n", this->targetName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doTest());
}

INSTANTIATE_TEST_SUITE_P(ExecTest, ExecTest,
                         ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR "/base")));

TEST(Base, case1) {
  std::string line(R"(type=3 lineNum=1 chars=0 kind="SystemError" fileName="../hoge.ds")");
  unsigned int type;
  unsigned int lineNum;
  unsigned int chars;
  std::string kind;
  std::string fileName;

  int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "chars", "=", chars, "kind",
                  "=", kind, "fileName", "=", fileName);
  ASSERT_EQ(0, ret);
  ASSERT_EQ(3u, type);
  ASSERT_EQ(1u, lineNum);
  ASSERT_EQ("SystemError", kind);
  ASSERT_EQ("../hoge.ds", fileName);
}

TEST(Base, case2) {
  std::string line("type=0 lineNum=0 kind=\"\"");
  unsigned int type;
  unsigned int lineNum;
  std::string kind;

  int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind);
  ASSERT_EQ(0, ret);
  ASSERT_EQ(0u, type);
  ASSERT_EQ(0u, lineNum);
  ASSERT_EQ("", kind);
}

TEST(Base, case3) {
  std::string line(R"(type=0 lineNum=0 kind="" fileName="" )");
  unsigned int type;
  unsigned int lineNum;
  std::string kind;
  std::string fileName;

  int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind, "fileName",
                  "=", fileName);
  ASSERT_EQ(0, ret);
  ASSERT_EQ(0u, type);
  ASSERT_EQ(0u, lineNum);
  ASSERT_EQ("", kind);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  if (chdir(EXEC_TEST_DIR) != 0) {
    fatal("broken test directory: %s\n", EXEC_TEST_DIR);
  }
  return RUN_ALL_TESTS();
}