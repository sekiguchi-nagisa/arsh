
#include "gtest/gtest.h"

#include <constant.h>

#include "analyzer.h"
#include "hover.h"
#include "indexer.h"

#include "../test_common.h"

using namespace ydsh::lsp;

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
    Analyzer analyzer(this->srcMan, this->archives);
    auto ret = analyzer.analyze(*src, action);
    ASSERT_TRUE(ret);
    modId = ret->getModId();
  }

  void doAnalyze(const char *content, unsigned short &modId, const IndexSize &size) {
    ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId));
    auto index = this->indexes.find(modId);
    ASSERT_TRUE(index);
    ASSERT_EQ(size.declSize, index->getDecls().size());
    ASSERT_EQ(size.symbolSize, index->getSymbols().size());
  }

  void findDecl(const Request &req, const std::vector<DeclResult> &expected) {
    auto src = this->srcMan.findById(req.modId);
    ASSERT_TRUE(src);
    auto pos = toTokenPos(src->getContent(), req.position);
    ASSERT_TRUE(pos.hasValue());

    SymbolRequest sr = {
        .modId = req.modId,
        .pos = pos.unwrap(),
    };

    std::vector<DeclResult> actual;
    findDeclaration(this->indexes, sr, [&](const FindDeclResult &result) {
      auto retSrc = this->srcMan.findById(result.decl.getModId());
      ASSERT_TRUE(retSrc);
      auto range = toRange(retSrc->getContent(), result.decl.getToken());
      ASSERT_TRUE(range.hasValue());

      actual.push_back(DeclResult{
          .modId = result.decl.getModId(),
          .range = range.unwrap(),
      });
    });

    ASSERT_EQ(expected.size(), actual.size());
    for (unsigned int i = 0; i < actual.size(); i++) {
      ASSERT_EQ(expected[i].modId, actual[i].modId);
      ASSERT_EQ(expected[i].range.toString(), actual[i].range.toString());
    }
  }

  void findRefs(const Request &req, const std::vector<RefsResult> &expected) {
    auto src = this->srcMan.findById(req.modId);
    ASSERT_TRUE(src);
    auto pos = toTokenPos(src->getContent(), req.position);
    ASSERT_TRUE(pos.hasValue());

    SymbolRequest sr = {
        .modId = req.modId,
        .pos = pos.unwrap(),
    };

    std::vector<RefsResult> actual;
    findAllReferences(this->indexes, sr, [&](const FindRefsResult &result) {
      auto retSrc = this->srcMan.findById(result.symbol.getModId());
      ASSERT_TRUE(retSrc);
      auto range = toRange(retSrc->getContent(), result.symbol.getToken());
      ASSERT_TRUE(range.hasValue());

      actual.push_back(RefsResult{
          .modId = result.symbol.getModId(),
          .range = range.unwrap(),
      });
    });

    ASSERT_EQ(expected.size(), actual.size());
    for (unsigned int i = 0; i < actual.size(); i++) {
      ASSERT_EQ(expected[i].modId, actual[i].modId);
      ASSERT_EQ(expected[i].range.toString(), actual[i].range.toString());
    }
  }

  void hover(const char *source, int line, const std::string &expected) {
    this->hover(source, Position{.line = line, .character = 0}, expected);
  }

  void hover(const char *source, Position position, const std::string &expected) {
    unsigned short modId = 0;
    ASSERT_NO_FATAL_FAILURE(this->doAnalyze(source, modId));
    ASSERT_TRUE(modId != 0);

    auto src = this->srcMan.findById(modId);
    ASSERT_TRUE(src);
    auto pos = toTokenPos(src->getContent(), position);
    ASSERT_TRUE(pos.hasValue());

    SymbolRequest sr = {
        .modId = modId,
        .pos = pos.unwrap(),
    };

    std::string actual;
    ydsh::Optional<Range> range;
    findDeclaration(this->indexes, sr, [&](const FindDeclResult &value) {
      actual = generateHoverContent(this->srcMan, *src, value.decl);
      range = toRange(src->getContent(), value.request.getToken());
    });

    ASSERT_TRUE(range.hasValue());
    ASSERT_EQ(expected, actual);
  }
};

TEST_F(IndexTest, scope1) {
  unsigned short modId;
  const char *content = R"(
try { $false; } catch($e) {
  $e;
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 3}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 2, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = modId,
      .range = { .start = { .line = 1, .character = 22, }, .end = { .line = 1, .character = 24, }}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 3, }
  };
  result = {
    DeclResult{
      .modId = modId,
      .range = { .start = { .line = 1, .character = 22, }, .end = { .line = 1, .character = 24, }}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 4, }
  };
  result = {
    DeclResult{
      .modId = modId,
      .range = { .start = { .line = 1, .character = 22, }, .end = { .line = 1, .character = 24, }}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = {.line = 2, .character = 5, }
  };
  result = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // reference

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 3, }
  };
  std::vector<RefsResult> result2 = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 1, .character = 22, }
  };
  result2 = {
    RefsResult{
      .modId = modId,
      .range = { .start = { .line = 1, .character = 22, }, .end = { .line = 1, .character = 24, }}
    },
    RefsResult{
      .modId = modId,
      .range = { .start = { .line = 2, .character = 2, }, .end = { .line = 2, .character = 4, }}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
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
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 11}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 1, .character = 14, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 9}, .end = {.line = 1, .character = 20}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 6, .character = 16, }
  };
  result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 2}, .end = {.line = 2, .character = 4}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // references

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 6, .character = 11, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 6, .character = 10}, .end = {.line = 6, .character = 12}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 6, .character = 26}, .end = {.line = 6, .character = 28}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 7, .character = 12, }
  };
  result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 7, .character = 10}, .end = {.line = 7, .character = 12}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 7, .character = 26}, .end = {.line = 7, .character = 28}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, scope3) {
  unsigned short modId;
  const char *content = "A=23 $A";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

  // references

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 0, .character = 0, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 0, .character = 0}, .end = {.line = 0, .character = 1}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 0, .character = 5}, .end = {.line = 0, .character = 7}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, func1) {
  unsigned short modId;
  const char *content = R"(
var a = function (
$a : Int) =>
$a + 34
$a(234)
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 4}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 3, .character = 0, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 2}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 4, .character = 0, }
  };
  result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 4}, .end = {.line = 1, .character = 5}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // references

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 1, .character = 4, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 4}, .end = {.line = 1, .character = 5}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 2}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 1, }
  };
  result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 2}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 3, .character = 0}, .end = {.line = 3, .character = 2}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, cmd1) {
  unsigned short modId;
  const char *content = R"E(
hoge a b $(hoge)
hoge() { echo hello: $@; hoge; }
hoge a b $(hoge) "$(hoge)" # hoge
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 7}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 1, .character = 2, }
  };
  std::vector<DeclResult> result = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 1, .character = 14, }
  };
  result = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 28, }
  };
  result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 4}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // reference

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 2, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 4}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 25}, .end = {.line = 2, .character = 29}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 3, .character = 0}, .end = {.line = 3, .character = 4}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 3, .character = 11}, .end = {.line = 3, .character = 15}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 3, .character = 20}, .end = {.line = 3, .character = 24}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));
}

TEST_F(IndexTest, type1) {
  unsigned short modId;
  const char *content = R"E(typeof StrArray = Int; {
typedef StrArray = [String]
new [typeof(new StrArray())]()
}
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 22, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 8}, .end = {.line = 1, .character = 16}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // references

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 1, .character = 12, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 8}, .end = {.line = 1, .character = 16}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 16}, .end = {.line = 2, .character = 24}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, globalImportDef) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
)");

  unsigned short modId;
  auto content = format(R"(
source %s
$AAA + $BBB()
CCC
new [DDD]()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 0, .symbolSize = 4}));
  ASSERT_EQ(1, modId);

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 1, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = 2,
      .range = {.start = {.line = 2, .character = 4}, .end = {.line = 2, .character = 7}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 9, }
  };
  result = {
    DeclResult{
      .modId = 2,
      .range = {.start = {.line = 3, .character = 9}, .end = {.line = 3, .character = 12}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 3, .character = 1, }
  };
  result = {
    DeclResult{
      .modId = 2,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 3}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 4, .character = 7, }
  };
  result = {
    DeclResult{
      .modId = 2,
      .range = {.start = {.line = 5, .character = 8}, .end = {.line = 5, .character = 11}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));
}

TEST_F(IndexTest, globalImportRef) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
)");

  unsigned short modId;
  auto content = format(R"(
source %s
$AAA + $BBB()
CCC
new [DDD]()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 0, .symbolSize = 4}));
  ASSERT_EQ(1, modId);

  // references

  // clang-format off
  Request req = {
    .modId = 2,
    .position = { .line = 2, .character = 5, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 2, .character = 4}, .end = {.line = 2, .character = 7}}
    },
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 3, .character = 30}, .end = {.line = 3, .character = 34}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 4}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 2,
    .position = { .line = 3, .character = 10, }
  };
  result2 = {
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 3, .character = 9}, .end = {.line = 3, .character = 12}}
    },
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 4, .character = 8}, .end = {.line = 4, .character = 12}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 2, .character = 7}, .end = {.line = 2, .character = 11}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 2,
    .position = { .line = 4, .character = 3, }
  };
  result2 = {
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 3}}
    },
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 5, .character = 21}, .end = {.line = 5, .character = 24}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 3, .character = 0}, .end = {.line = 3, .character = 3}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 2,
    .position = { .line = 5, .character = 9, }
  };
  result2 = {
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 5, .character = 8}, .end = {.line = 5, .character = 11}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 4, .character = 5}, .end = {.line = 4, .character = 8}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, namedImportDef) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
)");

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
$mod.AAA + $mod.BBB()
mod 2>&1 > /dev/null CCC 34
new [mod.DDD]()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 1, .symbolSize = 9}));
  ASSERT_EQ(1, modId);

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 13, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 3}, .end = {.line = 1, .character = 6}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 4, .character = 7, }
  };
  result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 3}, .end = {.line = 1, .character = 6}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 8, }
  };
  result = {
    DeclResult{
      .modId = 2,
      .range = {.start = {.line = 2, .character = 4}, .end = {.line = 2, .character = 7}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 19, }
  };
  result = {
    DeclResult{
      .modId = 2,
      .range = {.start = {.line = 3, .character = 9}, .end = {.line = 3, .character = 12}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 3, .character = 23, }
  };
  result = {
    DeclResult{
      .modId = 2,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 3}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 4, .character = 10, }
  };
  result = {
    DeclResult{
      .modId = 2,
      .range = {.start = {.line = 5, .character = 8}, .end = {.line = 5, .character = 11}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));
}

TEST_F(IndexTest, namedImportRef) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
)");

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
$mod.AAA + $mod.BBB()
mod 2>&1 > /dev/null CCC 34
new [mod.DDD]()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 1, .symbolSize = 9}));
  ASSERT_EQ(1, modId);

  // references

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 1, .character = 6, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 3}, .end = {.line = 1, .character = 6}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 4}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 11}, .end = {.line = 2, .character = 15}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 3, .character = 0}, .end = {.line = 3, .character = 3}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 4, .character = 5}, .end = {.line = 4, .character = 8}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 2,
    .position = { .line = 2, .character = 5, }
  };
  result2 = {
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 2, .character = 4}, .end = {.line = 2, .character = 7}}
    },
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 3, .character = 30}, .end = {.line = 3, .character = 34}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 2, .character = 5}, .end = {.line = 2, .character = 8}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 2,
    .position = { .line = 3, .character = 10, }
  };
  result2 = {
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 3, .character = 9}, .end = {.line = 3, .character = 12}}
    },
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 4, .character = 8}, .end = {.line = 4, .character = 12}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 2, .character = 16}, .end = {.line = 2, .character = 19}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 2,
    .position = { .line = 4, .character = 3, }
  };
  result2 = {
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 3}}
    },
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 5, .character = 21}, .end = {.line = 5, .character = 24}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 3, .character = 21}, .end = {.line = 3, .character = 24}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 2,
    .position = { .line = 5, .character = 9, }
  };
  result2 = {
    RefsResult{
      .modId = 2,
      .range = {.start = {.line = 5, .character = 8}, .end = {.line = 5, .character = 11}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 4, .character = 9}, .end = {.line = 4, .character = 12}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, inlinedImportRef) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
)");

  fileName =
      tempFileFactory.createTempFile("inlined.ds", format("source %s inlined", fileName.c_str()));

  unsigned short modId;
  auto content = format(R"(
source %s
$AAA + $BBB()
CCC
new [DDD]()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 0, .symbolSize = 4}));
  ASSERT_EQ(1, modId);

  // references

  // clang-format off
  Request req = {
    .modId = 3,
    .position = { .line = 2, .character = 5, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 2, .character = 4}, .end = {.line = 2, .character = 7}}
    },
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 3, .character = 30}, .end = {.line = 3, .character = 34}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 4}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 3,
    .position = { .line = 3, .character = 10, }
  };
  result2 = {
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 3, .character = 9}, .end = {.line = 3, .character = 12}}
    },
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 4, .character = 8}, .end = {.line = 4, .character = 12}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 2, .character = 7}, .end = {.line = 2, .character = 11}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 3,
    .position = { .line = 4, .character = 3, }
  };
  result2 = {
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 3}}
    },
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 5, .character = 21}, .end = {.line = 5, .character = 24}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 3, .character = 0}, .end = {.line = 3, .character = 3}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 3,
    .position = { .line = 5, .character = 9, }
  };
  result2 = {
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 5, .character = 8}, .end = {.line = 5, .character = 11}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 4, .character = 5}, .end = {.line = 4, .character = 8}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, namedImportInlinedDef) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
)");

  fileName =
      tempFileFactory.createTempFile("inlined.ds", format("source %s inlined", fileName.c_str()));

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
$mod.AAA + $mod.BBB()
mod CCC
new [mod.DDD]()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 1, .symbolSize = 9}));
  ASSERT_EQ(1, modId);

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 13, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 3}, .end = {.line = 1, .character = 6}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 4, .character = 7, }
  };
  result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 3}, .end = {.line = 1, .character = 6}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 8, }
  };
  result = {
    DeclResult{
      .modId = 3,
      .range = {.start = {.line = 2, .character = 4}, .end = {.line = 2, .character = 7}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 19, }
  };
  result = {
    DeclResult{
      .modId = 3,
      .range = {.start = {.line = 3, .character = 9}, .end = {.line = 3, .character = 12}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 3, .character = 5, }
  };
  result = {
    DeclResult{
      .modId = 3,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 3}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 4, .character = 10, }
  };
  result = {
    DeclResult{
      .modId = 3,
      .range = {.start = {.line = 5, .character = 8}, .end = {.line = 5, .character = 11}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));
}

TEST_F(IndexTest, namedImportInlinedRef) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
)");

  fileName =
      tempFileFactory.createTempFile("inlined.ds", format("source %s inlined", fileName.c_str()));

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
$mod.AAA + $mod.BBB()
mod CCC
new [mod.DDD]()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 1, .symbolSize = 9}));
  ASSERT_EQ(1, modId);

  // references

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 1, .character = 6, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 3}, .end = {.line = 1, .character = 6}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 4}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 11}, .end = {.line = 2, .character = 15}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 3, .character = 0}, .end = {.line = 3, .character = 3}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 4, .character = 5}, .end = {.line = 4, .character = 8}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 3,
    .position = { .line = 2, .character = 5, }
  };
  result2 = {
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 2, .character = 4}, .end = {.line = 2, .character = 7}}
    },
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 3, .character = 30}, .end = {.line = 3, .character = 34}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 2, .character = 5}, .end = {.line = 2, .character = 8}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 3,
    .position = { .line = 3, .character = 10, }
  };
  result2 = {
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 3, .character = 9}, .end = {.line = 3, .character = 12}}
    },
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 4, .character = 8}, .end = {.line = 4, .character = 12}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 2, .character = 16}, .end = {.line = 2, .character = 19}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 3,
    .position = { .line = 4, .character = 3, }
  };
  result2 = {
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 3}}
    },
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 5, .character = 21}, .end = {.line = 5, .character = 24}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 3, .character = 4}, .end = {.line = 3, .character = 7}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));

  // clang-format off
  req = {
    .modId = 3,
    .position = { .line = 5, .character = 9, }
  };
  result2 = {
    RefsResult{
      .modId = 3,
      .range = {.start = {.line = 5, .character = 8}, .end = {.line = 5, .character = 11}}
    },
    RefsResult{
      .modId = 1,
      .range = {.start = {.line = 4, .character = 9}, .end = {.line = 4, .character = 12}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, invalidVar) {
  unsigned short modId;
  const char *content = R"(
let aaa = (throw 34)   # not allow 'Nothing'
34 + $aaa
var bbb = (34 as Void)  # not allow 'Void'
var ccc = 34
var ccc = $ccc
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 7, }
  };
  std::vector<DeclResult> result = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 3, .character = 6, }
  };
  result = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // references

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 1, .character = 6, }
  };
  std::vector<RefsResult> result2 = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, inlinedImportDef) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
)");

  fileName =
      tempFileFactory.createTempFile("inlined.ds", format("source %s inlined", fileName.c_str()));

  unsigned short modId;
  auto content = format(R"(
source %s
$AAA + $BBB()
CCC
new [DDD]()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 0, .symbolSize = 4}));
  ASSERT_EQ(1, modId);

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 1, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = 3,
      .range = {.start = {.line = 2, .character = 4}, .end = {.line = 2, .character = 7}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 9, }
  };
  result = {
    DeclResult{
      .modId = 3,
      .range = {.start = {.line = 3, .character = 9}, .end = {.line = 3, .character = 12}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 3, .character = 1, }
  };
  result = {
    DeclResult{
      .modId = 3,
      .range = {.start = {.line = 4, .character = 0}, .end = {.line = 4, .character = 3}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 4, .character = 7, }
  };
  result = {
    DeclResult{
      .modId = 3,
      .range = {.start = {.line = 5, .character = 8}, .end = {.line = 5, .character = 11}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));
}

TEST_F(IndexTest, udc_overwrite) {
  unsigned short modId;
  const char *content = R"E(
echo() {}
echo hello
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 0, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 0}, .end = {.line = 1, .character = 4}}
    }
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // references

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 1, .character = 2, }
  };
  std::vector<RefsResult> result2 = {
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 1, .character = 0}, .end = {.line = 1, .character = 4}}
    },
    RefsResult{
      .modId = modId,
      .range = {.start = {.line = 2, .character = 0}, .end = {.line = 2, .character = 4}}
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, invalidFunc) {
  unsigned short modId;
  const char *content = R"(
var b = 34;
function func($a : Nothing) : Int { return 34 + $func(34); }  # not allow 'Nothing'
$b + $func(34)
{ function gg() : Int { return 34; } }
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 3, .character = 8, }
  };
  std::vector<DeclResult> result = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // references

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 2, .character = 12, }
  };
  std::vector<RefsResult> result2 = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, invalidUdc) {
  unsigned short modId;
  const char *content = R"(hoge() {}
hoge() { eval echo hello; }  # already defined
hoge eval 34
{ f() {} }
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 3}));

  // definition

  // clang-format off
  Request req = {
    .modId = modId,
    .position = { .line = 2, .character = 1, }
  };
  std::vector<DeclResult> result = {
    DeclResult{
      .modId = modId,
      .range = {.start = {.line = 0, .character = 0}, .end = {.line = 0, .character = 4}},
    },
  };
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findDecl(req, result));

  // references

  // clang-format off
  req = {
    .modId = modId,
    .position = { .line = 1, .character = 2, }
  };
  std::vector<RefsResult> result2 = {};
  // clang-format on
  ASSERT_NO_FATAL_FAILURE(this->findRefs(req, result2));
}

TEST_F(IndexTest, hover) {
  ASSERT_NO_FATAL_FAILURE(this->hover("let A = 34\n$A", 1, "```ydsh\nlet A : Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$?", 0, "```ydsh\nvar ? : Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$YDSH_VERSION", 0,
                                      "```ydsh\nconst YDSH_VERSION = '" X_INFO_VERSION_CORE "'"
                                      "\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("import-env HOME\n$HOME", 1, "```ydsh\nimport-env HOME : String\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("export-env ZZZ = 'hoge'\n$ZZZ", 1, "```ydsh\nexport-env ZZZ : String\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("function hoge($s : Int) {}\n$hoge", 1,
                                      "```ydsh\nfunction hoge($s : Int) : Void\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$true is\nBool", 1, "```ydsh\ntypedef Bool = Boolean\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover(":", 0,
                  "```md\n"
                  ":: : \n"
                  "    Null command.  Always success (exit status is 0).\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("hoge(){}\nhoge", 1, "```ydsh\nhoge()\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("$SCRIPT_NAME", 0, "```ydsh\nconst SCRIPT_NAME = '/dummy_10'\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$SCRIPT_DIR", 0, "```ydsh\nconst SCRIPT_DIR = '/'\n```"));

  // source
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile(X_INFO_VERSION_CORE "_.ds",
                                                 R"(
var AAA = 'hello'
)");
  std::string src = "source ";
  src += fileName;
  src += " as mod\n$mod";
  ASSERT_NO_FATAL_FAILURE(
      this->hover(src.c_str(), 1, format("```ydsh\nsource %s as mod\n```", fileName.c_str())));

  src = "source ";
  src += tempFileFactory.getTempDirName();
  src += "/";
  int chars = src.size() + 5;
  src += "${YDSH_VERSION}_.ds";
  ASSERT_NO_FATAL_FAILURE(this->hover(src.c_str(), Position{.line = 0, .character = chars},
                                      "```ydsh\nconst YDSH_VERSION = '" X_INFO_VERSION_CORE
                                      "'\n```"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}