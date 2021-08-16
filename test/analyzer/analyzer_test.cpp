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

class ParseTest : public ::testing::Test {
protected:
  SourceManager srcMan;
  IndexMap indexMap;

public:
  void parse(const char *path, ModuleIndexPtr &index) {
    // read
    ASSERT_TRUE(path && *path);
    std::ifstream input(path);
    std::string content;
    ASSERT_FALSE(!input);
    for (std::string line; std::getline(input, line);) {
      content += line;
      content += '\n';
    }

    // parse
    auto src = this->srcMan.update(path, 0, std::move(content));
    ASSERT_TRUE(src);
    AnalyzerAction action;
    index = buildIndex(this->srcMan, this->indexMap, action, *src);
    ASSERT_TRUE(index);
  }
};

static const Node *findVarDecl(const Node &node, const char *varName) {
  switch (node.getNodeKind()) {
  case NodeKind::VarDecl: {
    auto &varDeclNode = cast<const VarDeclNode>(node);
    if (varDeclNode.getVarName() == varName) {
      return &node;
    }
    break;
  }
  case NodeKind::Source: {
    auto &sourceNode = cast<const SourceNode>(node);
    if (sourceNode.getNameInfo() && sourceNode.getNameInfo()->getName() == varName) {
      return &node;
    }
    break;
  }
  default:
    break;
  }
  return nullptr;
}

static const Node *findVarDecl(const std::vector<std::unique_ptr<Node>> &nodes,
                               const char *varName) {
  for (auto &e : nodes) {
    if (const Node * ret; (ret = findVarDecl(*e, varName))) {
      return ret;
    }
  }
  return nullptr;
}

TEST_F(ParseTest, case1) { // FIXME: replaced with goto-definition test case
  const char *path = EXEC_TEST_DIR "/base/mod1.ds";
  ModuleIndexPtr index;
  ASSERT_NO_FATAL_FAILURE(this->parse(path, index));
  ASSERT_TRUE(index);
  ASSERT_EQ(2, this->indexMap.size());
  ASSERT_TRUE(StringRef(this->srcMan.findById(2)->getPath()).contains("module1.ds"));

  auto *decl = findVarDecl(index->getNodes(), "c");
  ASSERT_TRUE(decl);
  ASSERT_TRUE(isa<VarDeclNode>(decl));

  decl = findVarDecl(index->getNodes(), "hello");
  ASSERT_FALSE(decl);

  decl = findVarDecl(index->getNodes(), "mod");
  ASSERT_TRUE(decl);
  ASSERT_TRUE(isa<SourceNode>(decl));

  //
  index = this->indexMap.find(*this->srcMan.findById(2));
  ASSERT_TRUE(index);
  this->indexMap.revert({2});
  index = this->indexMap.find(*this->srcMan.findById(2));
  ASSERT_FALSE(index);
  ASSERT_EQ(0, this->indexMap.size());
  path = EXEC_TEST_DIR "/base/mod2.ds";
  ASSERT_NO_FATAL_FAILURE(this->parse(path, index));
  ASSERT_TRUE(index);
  ASSERT_EQ(2, this->indexMap.size());
  ASSERT_TRUE(StringRef(this->srcMan.findById(2)->getPath()).contains("module1.ds"));

  decl = findVarDecl(index->getNodes(), "mod");
  ASSERT_TRUE(decl);
  ASSERT_TRUE(isa<SourceNode>(decl));
}

TEST_F(ParseTest, case2) { // FIXME: replaced with goto-definition test case
  const char *path = EXEC_TEST_DIR "/base/mod3.ds";
  ModuleIndexPtr index;
  ASSERT_NO_FATAL_FAILURE(this->parse(path, index));
  ASSERT_TRUE(index);
  ASSERT_EQ(3, this->indexMap.size());
  ASSERT_TRUE(StringRef(this->srcMan.findById(2)->getPath()).contains("module3.ds"));
  ASSERT_TRUE(StringRef(this->srcMan.findById(3)->getPath()).contains("module4.ds"));

  auto decl = findVarDecl(index->getNodes(), "mod");
  ASSERT_TRUE(decl);
  ASSERT_TRUE(isa<SourceNode>(decl));

  decl = findVarDecl(index->getNodes(), "mod1");
  ASSERT_TRUE(decl);
  ASSERT_TRUE(isa<SourceNode>(decl));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}