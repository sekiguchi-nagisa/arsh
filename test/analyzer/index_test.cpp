
#include "gtest/gtest.h"

#include "analyzer.h"
#include "indexer.h"

using namespace ydsh::lsp;

struct IndexSize {
  unsigned int declSize;
  unsigned int symbolSize;
};

class IndexTest : public ::testing::Test {
protected:
  SourceManager srcMan;
  ModuleArchives archives;
  SymbolIndexes indexes;
  unsigned int idCount{0};

public:
  void doAnalyze(const char *content, unsigned short &modId) {
    std::string path = "/dummy_";
    path += std::to_string(++this->idCount);
    auto src = this->srcMan.update(path, 0, content);
    ASSERT_TRUE(src);
    AnalyzerAction action;
    SymbolIndexer indexer(this->indexes);
    action.consumer.reset(&indexer);
    auto ret = analyze(this->srcMan, this->archives, action, *src);
    ASSERT_TRUE(ret);
    modId = ret->getModID();
  }

  void doAnalyze(const char *content, unsigned short &modId, const IndexSize &size) {
    ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId));
    auto *index = this->indexes.find(modId);
    ASSERT_TRUE(index);
    ASSERT_EQ(size.declSize, index->getDecls().size());
    ASSERT_EQ(size.symbolSize, index->getSymbols().size());
  }
};

static std::pair<unsigned short, const DeclSymbol *> findDeclaration2(const SymbolIndexes &indexes,
                                                                      SymbolRequest req) {
  unsigned short id = 0;
  const DeclSymbol *decl = nullptr;
  findDeclaration(indexes, req, [&](const FindDeclResult &ret) {
    id = ret.declModId;
    decl = &ret.decl;
  });
  return {id, decl};
}

TEST_F(IndexTest, scope1) {
  unsigned short modId;
  const char *content = R"(
try { $false; } catch($e) {
  $e;
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

  auto ret = toTokenPos(content, Position{.line = 2, .character = 3});
  ASSERT_TRUE(ret.hasValue());
  auto pair = findDeclaration2(this->indexes, {.modId = modId, .pos = ret.unwrap()});
  ASSERT_TRUE(pair.second);
  ASSERT_EQ(modId, pair.first);
  ASSERT_EQ(2, pair.second->getRefs().size());
  ASSERT_EQ(DeclSymbol::Kind::VAR, pair.second->getKind());
  ASSERT_EQ(23, pair.second->getPos());

  ret = toTokenPos(content, Position{.line = 2, .character = 4});
  ASSERT_TRUE(ret.hasValue());
  pair = findDeclaration2(this->indexes, {.modId = modId, .pos = ret.unwrap()});
  ASSERT_TRUE(pair.second);
  ASSERT_EQ(modId, pair.first);
  ASSERT_EQ(2, pair.second->getRefs().size());
  ASSERT_EQ(DeclSymbol::Kind::VAR, pair.second->getKind());
  ASSERT_EQ(23, pair.second->getPos());
}

TEST_F(IndexTest, scope2) {
  unsigned short modId;
  const char *content = R"(
function assertArray(
  $x : Array<String>,
  $y : Array<String>) {

  assert diff \
    <(for $a in $x { echo $a; }) \
    <(for $a in $y { echo $a; })
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 9}));

  auto ret = toTokenPos(content, Position{.line = 1, .character = 13});
  ASSERT_TRUE(ret.hasValue());
  auto *decl = this->indexes.findDecl({.modId = modId, .pos = ret.unwrap()});
  ASSERT_TRUE(decl);
  ASSERT_EQ(DeclSymbol::Kind::FUNC, decl->getKind());
  ASSERT_EQ(1, decl->getRefs().size());
  ASSERT_EQ(10, decl->getPos());

  ret = toTokenPos(content, Position{.line = 2, .character = 3});
  ASSERT_TRUE(ret.hasValue());
  decl = this->indexes.findDecl({.modId = modId, .pos = ret.unwrap()});
  ASSERT_TRUE(decl);
  ASSERT_EQ(DeclSymbol::Kind::VAR, decl->getKind());
  ASSERT_EQ(2, decl->getRefs().size());
}

TEST_F(IndexTest, scope3) {
  unsigned short modId;
  const char *content = "A=23 $A";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}