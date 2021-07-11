#include "gtest/gtest.h"

#include <fstream>

#include <directive.h>
#include <misc/files.h>
#include <misc/string_ref.hpp>

#include "../test_common.h"

#ifndef EXEC_TEST_DIR
#error require EXEC_TEST_DIR
#endif

#ifndef BIN_PATH
#error require BIN_PATH
#endif

using namespace ydsh;
using namespace ydsh::directive;

// parse config(key = value)
template <typename... T>
int parse(const std::string &src, T &&...args) {
  return Extractor(src.c_str())(std::forward<T>(args)...);
}

template <typename... T>
int parse(const char *src, T &&...args) {
  return Extractor(src)(std::forward<T>(args)...);
}

static std::vector<std::string> getSortedFileList(const char *dir) {
  auto ret = getFileList(dir, true);
  assert(!ret.empty());
  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [](const std::string &v) { return !StringRef(v).endsWith(".ds"); }),
            ret.end());
  std::sort(ret.begin(), ret.end());
  ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
  return ret;
}

class ExecTest : public ::testing::TestWithParam<std::string>, public TempFileFactory {
protected:
  std::string targetName;

public:
  ExecTest() : INIT_TEMP_FILE_FACTORY(exec_test) { this->targetName = GetParam(); }

  virtual const std::string &getSourceName() { return this->targetName; }

  virtual void doTest() {
    // create directive
    Directive d;
    bool s = Directive::init(this->getSourceName().c_str(), d);
    ASSERT_TRUE(s);

    if (d.isIgnoredPlatform()) {
      fprintf(stderr, "ignore execution: %s\n", this->getSourceName().c_str());
      return;
    }

    const char *scriptName = this->getSourceName().c_str();
    ProcBuilder builder(BIN_PATH);
    builder.addArg("--status-log").addArg(this->getTempFileName());

    // set argument
    builder.addArg(scriptName);
    builder.addArgs(d.getParams());

    // set IO config
    if (!d.getIn().empty()) {
      builder.setIn(IOConfig::PIPE);
    }
    if (d.getOut()) {
      builder.setOut(IOConfig::PIPE);
    }
    if (d.getErr()) {
      builder.setErr(IOConfig::PIPE);
    }

    // set working dir
    builder.setWorkingDir(EXEC_TEST_DIR);

    // set env
    for (auto &env : d.getEnvs()) {
      builder.addEnv(env.first.c_str(), env.second.c_str());
    }

    // execute
    auto handle = builder();
    if (!d.getIn().empty()) {
      if (write(handle.in(), d.getIn().c_str(), d.getIn().size()) < 0) {
        fatal_perror("");
      }
      close(handle.in());
    }
    auto output = handle.waitAndGetResult(false);
    int ret = output.status.toShellStatus();

    // get internal status
    std::ifstream input(this->getTempFileName());
    ASSERT_FALSE(!input);

    std::string line;
    std::getline(input, line);

    int kind = -1;
    unsigned int lineNum = 0;
    unsigned int chars = 0;
    std::string name;
    std::string fileName;

    if (line.empty()) {
      printf("@ not found status-log at %s\n", this->targetName.c_str());
    } else {
      int r = parse(line, "kind", "=", kind, "lineNum", "=", lineNum, "chars", "=", chars, "name",
                    "=", name, "fileName", "=", fileName);
      ASSERT_EQ(0, r);
    }

    // check status
    ASSERT_EQ(formatKind(d.getKind()), formatKind(kind));
    ASSERT_EQ(d.getLineNum(), lineNum);
    ASSERT_EQ(d.getChars(), chars);
    ASSERT_EQ(d.getStatus(), ret);
    ASSERT_EQ(d.getErrorKind(), name);
    if (!d.getFileName().empty()) {
      ASSERT_EQ(d.getFileName(), fileName);
    }
    if (d.getOut()) {
      ASSERT_STREQ(d.getOut(), output.out.c_str());
    }
    if (d.getErr()) {
      ASSERT_STREQ(d.getErr(), output.err.c_str());
    }
  }

  static std::string formatKind(int kind) {
    const char *s = toString(static_cast<DSErrorKind>(kind));
    std::string ret;
    if(*s) {
      ret = split(s, '_')[3];
    }
    ret += "(";
    ret += std::to_string(kind);
    ret += ")";
    return ret;
  }
};

TEST_P(ExecTest, baseTest) {
  printf("@@ test script %s\n", this->targetName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doTest());
}

INSTANTIATE_TEST_SUITE_P(ExecTest, ExecTest, ::testing::ValuesIn(getSortedFileList(EXEC_TEST_DIR)));

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