
#ifndef ARSH_TEST_ANALYZER_INDEX_TEST_HPP
#define ARSH_TEST_ANALYZER_INDEX_TEST_HPP

#include "gtest/gtest.h"

#include "analyzer.h"
#include "indexer.h"
#include "symbol.h"

#include "../test_common.h"

using namespace arsh::lsp;
using namespace arsh;

struct IndexSize {
  unsigned int declSize;
  unsigned int symbolSize;
};

struct Request {
  unsigned short modId;
  Position position;
};

struct DeclResult {
  unsigned short modId;
  Range range;
};

struct RefsResult {
  unsigned short modId;
  Range range;
};

struct Loc {
  unsigned short modId;
  std::string rangeStr;

  Loc(unsigned short modId, std::string &&rangeStr) : modId(modId), rangeStr(std::move(rangeStr)) {}

  Loc(unsigned short modId, const char *str) : modId(modId), rangeStr(str) {}
};

class IndexTest : public ::testing::Test {
protected:
  SysConfig sysConfig;
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
    SymbolIndexer indexer(this->sysConfig, this->indexes);
    action.pass.reset(&indexer);
    Analyzer analyzer(this->sysConfig, this->srcMan, this->archives);
    auto ret = analyzer.analyze(src, action);
    ASSERT_TRUE(ret);
    modId = toUnderlying(ret->getModId());
  }

  void doAnalyze(const char *content, unsigned short &modId, const IndexSize &size) {
    ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId));
    auto index = this->indexes.find(ModId{modId});
    ASSERT_TRUE(index);
    ASSERT_EQ(size.declSize, index->decls.size());
    ASSERT_EQ(size.symbolSize, index->symbols.size());
  }

  void findDecl(const Request &req, const std::vector<Loc> &expected) {
    auto src = this->srcMan.findById(ModId{req.modId});
    ASSERT_TRUE(src);
    auto pos = toTokenPos(src->getContent(), req.position);
    ASSERT_TRUE(pos.hasValue());

    SymbolRequest sr = {
        .modId = ModId{req.modId},
        .pos = pos.unwrap(),
    };

    std::vector<DeclResult> actual;
    findDeclaration(this->indexes, sr, [&](const FindDeclResult &result) {
      auto retSrc = this->srcMan.findById(result.decl.getModId());
      ASSERT_TRUE(retSrc);
      auto range = retSrc->toRange(result.decl.getToken());
      ASSERT_TRUE(range.hasValue());

      actual.push_back(DeclResult{
          .modId = toUnderlying(result.decl.getModId()),
          .range = range.unwrap(),
      });
    });

    ASSERT_EQ(expected.size(), actual.size());
    for (unsigned int i = 0; i < actual.size(); i++) {
      ASSERT_EQ(expected[i].modId, actual[i].modId);
      ASSERT_EQ(expected[i].rangeStr, actual[i].range.toString());
    }
  }

  void findDecl(const Request &req, const std::vector<DeclResult> &expected) {
    std::vector<Loc> locs;
    for (auto &e : expected) {
      locs.emplace_back(e.modId, e.range.toString());
    }
    this->findDecl(req, locs);
  }

  void findRefs(const Request &req, const std::vector<RefsResult> &expected) {
    std::vector<Loc> locs;
    for (auto &e : expected) {
      locs.emplace_back(e.modId, e.range.toString());
    }
    this->findRefs(req, locs);
  }

  void findRefs(const Request &req, const std::vector<Loc> &expected) {
    auto src = this->srcMan.findById(ModId{req.modId});
    ASSERT_TRUE(src);
    auto pos = toTokenPos(src->getContent(), req.position);
    ASSERT_TRUE(pos.hasValue());

    SymbolRequest sr = {
        .modId = ModId{req.modId},
        .pos = pos.unwrap(),
    };

    std::vector<RefsResult> actual;
    findAllReferences(this->indexes, sr, [&](const FindRefsResult &result) {
      if (result.symbol.getModId() == BUILTIN_MOD_ID) {
        return;
      }
      auto retSrc = this->srcMan.findById(result.symbol.getModId());
      ASSERT_TRUE(retSrc);
      auto range = retSrc->toRange(result.symbol.getToken());
      ASSERT_TRUE(range.hasValue());

      actual.push_back(RefsResult{
          .modId = toUnderlying(result.symbol.getModId()),
          .range = range.unwrap(),
      });
    });

    ASSERT_EQ(expected.size(), actual.size());
    for (unsigned int i = 0; i < actual.size(); i++) {
      ASSERT_EQ(expected[i].modId, actual[i].modId);
      ASSERT_EQ(expected[i].rangeStr, actual[i].range.toString());
    }
  }

  void hover(const char *source, int line, const std::string &expected) {
    this->hover(source, Position{.line = line, .character = 0}, expected);
  }

  void hover(const char *source, Position position, const std::string &expected) {
    unsigned short modId = 0;
    ASSERT_NO_FATAL_FAILURE(this->doAnalyze(source, modId));
    ASSERT_TRUE(modId != 0);

    auto src = this->srcMan.findById(ModId{modId});
    ASSERT_TRUE(src);
    auto pos = toTokenPos(src->getContent(), position);
    ASSERT_TRUE(pos.hasValue());

    SymbolRequest sr = {
        .modId = ModId{modId},
        .pos = pos.unwrap(),
    };

    std::string actual;
    Optional<Range> range;
    findDeclaration(this->indexes, sr, [&](const FindDeclResult &value) {
      actual = generateHoverContent(this->srcMan, this->indexes, *src, value);
      range = src->toRange(value.request.getToken());
    });

    ASSERT_TRUE(range.hasValue());
    ASSERT_EQ(expected, actual);
  }
};

#endif // ARSH_TEST_ANALYZER_INDEX_TEST_HPP
