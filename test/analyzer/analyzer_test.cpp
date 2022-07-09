#include "../test_common.h"

#include <fstream>

#include "analyzer.h"
#include "indexer.h"

#ifndef BIN_PATH
#error require BIN_PATH
#endif

#ifndef EXEC_TEST_DIR
#error require EXEC_TEST_DIR
#endif

#ifndef LITECHECK_PATH
#error require LITECHECK_PATH
#endif

#ifndef ANALYZER_TEST_DIR
#error require ANALYZER_TEST_DIR
#endif

#ifndef ANALYZER_PATH
#error require ANALYZ_PATH
#endif

using namespace ydsh::lsp;
using namespace ydsh;

/**
 * compare analyzer output with frontend output
 */
class ASTDumpTest : public ::testing::TestWithParam<std::string>, public TempFileFactory {
public:
  ASTDumpTest() : INIT_TEMP_FILE_FACTORY(analyzer_test) {}

protected:
  static void readContent(const std::string &fileName, std::string &value) {
    std::ifstream input(fileName);
    ASSERT_FALSE(!input);

    for (std::string line; std::getline(input, line);) {
      value += line;
      value += '\n';
    }
  }

  void readContent(std::string &value) { readContent(this->getTempFileName(), value); }

  void doTest() {
    // dump ast
    std::string dump = "--dump-ast=";
    dump += this->getTempFileName();

    auto ret = ProcBuilder{BIN_PATH, dump.c_str(), "--check-only", GetParam().c_str()}.exec();
    ASSERT_EQ(WaitStatus::EXITED, ret.kind);
    ASSERT_EQ(0, ret.value);
    std::string expected;
    ASSERT_NO_FATAL_FAILURE(this->readContent(expected));

    // dump analyzer output
    std::string content;
    readContent(GetParam(), content);
    SourceManager man;
    ModuleArchives archives;
    std::string tempFileName;
    auto tmpFile = this->createTempFilePtr(tempFileName, "");
    NodeDumper dumper(tmpFile.get());
    auto src = man.update("<dummy>", 0, ""); // dummy
    src = man.update(GetParam(), 0, std::move(content));
    ASSERT_EQ(2, src->getSrcId());
    SysConfig sysConfig;
    AnalyzerAction action;
    SymbolIndexes indexes;
    SymbolIndexer indexer(sysConfig, indexes);
    action.dumper.reset(&dumper);
    action.pass.reset(&indexer);
    Analyzer analyzer(sysConfig, man, archives);
    analyzer.analyze(*src, action);
    tmpFile.reset();
    content = std::string();
    readContent(tempFileName, content);
    ASSERT_EQ(expected, content);
  }
};

static std::vector<std::string> getTargetCases(const char *dir) {
  auto real = getRealpath(dir);
  auto ret = getFileList(real.get(), true);
  assert(!ret.empty());
  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [](const std::string &v) { return !StringRef(v).endsWith(".ds"); }),
            ret.end());
  std::sort(ret.begin(), ret.end());
  ret.erase(std::unique(ret.begin(), ret.end()), ret.end());

  // filter ignored cases
  const char *ignoredPattern[] = {"mod",       "subcmd", "shctl",   "complete5",
                                  "complete6", "load",   "fullname"};

  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [&ignoredPattern](const std::string &v) {
                             StringRef ref = v;
                             return std::any_of(
                                 std::begin(ignoredPattern), std::end(ignoredPattern),
                                 [&ref](const char *pt) { return ref.contains(pt); });
                           }),
            ret.end());
  return ret;
}

TEST_P(ASTDumpTest, base) {
  printf("@@ test script %s\n", GetParam().c_str());
  ASSERT_NO_FATAL_FAILURE(this->doTest());
}

INSTANTIATE_TEST_SUITE_P(ASTDumpTest, ASTDumpTest,
                         ::testing::ValuesIn(getTargetCases(EXEC_TEST_DIR "/base")));

static std::vector<std::string> getSortedFileList(const char *dir) {
  auto ret = getFileList(dir, true);
  assert(!ret.empty());
  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [](const std::string &v) { return !StringRef(v).endsWith(".test"); }),
            ret.end());
  std::sort(ret.begin(), ret.end());
  ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
  return ret;
}

static ProcBuilder litecheck(const std::string &scriptPath) {
  return ProcBuilder{BIN_PATH, LITECHECK_PATH, "-b", ANALYZER_PATH}.addArg(scriptPath);
}

struct AnalyzerTest : public ::testing::TestWithParam<std::string> {
  void doTest() {
    auto result = litecheck(GetParam()).exec();
    ASSERT_EQ(WaitStatus::EXITED, result.kind);
    ASSERT_EQ(0, result.value);
  }
};

TEST_P(AnalyzerTest, base) {
  printf("@@ test script %s\n", GetParam().c_str());
  ASSERT_NO_FATAL_FAILURE(this->doTest());
}

INSTANTIATE_TEST_SUITE_P(AnalyzerTest, AnalyzerTest,
                         ::testing::ValuesIn(getSortedFileList(ANALYZER_TEST_DIR)));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}