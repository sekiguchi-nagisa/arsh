#include "../test_common.h"

#include <fstream>

#include "analyzer.h"

#ifndef BIN_PATH
#error require BIN_PATH
#endif

#ifndef EXEC_TEST_DIR
#error require EXEC_TEST_DIR
#endif

using namespace ydsh::lsp;
using namespace ydsh;

TEST(ASTCtxTest, base) {
  Source src("/dummy", 1, "", 0);
  auto ctx = std::make_unique<ASTContext>(src);
  auto *handle = ctx->getScope()->find("COMP_HOOK");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(hasFlag(handle->attr(), FieldAttribute::GLOBAL));
  ASSERT_EQ(0, handle->getModID());
  handle = ctx->getScope()->find("TRUE");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(hasFlag(handle->attr(), FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY));
  ASSERT_EQ(0, handle->getModID());
  ASSERT_TRUE(ctx->getPool().getDiscardPoint().typeIdOffset <= UINT8_MAX);
}

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
    IndexMap indexMap;
    std::string tempFileName;
    auto tmpFile = this->createTempFilePtr(tempFileName, "");
    NodeDumper dumper(tmpFile.get());
    auto src = man.update(GetParam(), 0, std::move(content));
    AnalyzerAction action;
    action.dumper.reset(&dumper);
    auto index = buildIndex(man, indexMap, action, *src);
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
  ret.erase(std::remove_if(ret.begin(), ret.end(),
                           [](const std::string &v) {
                             StringRef ref = v;
                             return ref.contains("mod") || ref.contains("subcmd") ||
                                    ref.contains("shctl") || ref.contains("complete6");
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}