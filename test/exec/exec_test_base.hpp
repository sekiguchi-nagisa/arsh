
#ifndef YDSH_EXEC_TEST_BASE_HPP
#define YDSH_EXEC_TEST_BASE_HPP

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

#ifndef LITECHECK_PATH
#error LITECHECK_PATH
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

inline std::vector<std::string> getSortedFileList(const char *dir,
                                                  const std::vector<std::string> &ignored = {}) {
  auto ret = getFileList(dir, true);
  assert(!ret.empty());
  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [&ignored](const std::string &v) {
                             for (auto &i : ignored) {
                               if (StringRef(v).startsWith(i)) {
                                 return true;
                               }
                             }
                             return !StringRef(v).endsWith(".ds");
                           }),
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

  const std::string &getSourceName() { return this->targetName; }

  void doTest() {
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
    if (*s) {
      ret = split(s, '_')[3];
    }
    ret += "(";
    ret += std::to_string(kind);
    ret += ")";
    return ret;
  }

  void doTestWithLitecheck() {
    const char *scriptName = this->getSourceName().c_str();
    ProcBuilder builder = {BIN_PATH, LITECHECK_PATH, "-b", BIN_PATH, scriptName};
    auto result = builder.exec();
    ASSERT_EQ(WaitStatus::EXITED, result.kind);
    if (result.value == 125) {
      printf("  [skip] %s\n", scriptName);
    } else {
      ASSERT_EQ(0, result.value);
    }
  }
};

#endif // YDSH_EXEC_TEST_BASE_HPP
