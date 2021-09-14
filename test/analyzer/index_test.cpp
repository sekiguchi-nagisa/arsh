
#include "gtest/gtest.h"

#include "analyzer.h"
#include "indexer.h"

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
};

TEST_F(IndexTest, scope1) {
  unsigned short modId;
  const char *content = R"(
try { $false; } catch($e) {
  $e;
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

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
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 9}));

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

TEST_F(IndexTest, cmd1) {
  unsigned short modId;
  const char *content = R"E(
hoge a b $(hoge)
hoge() { echo hello: $@; hoge; }
hoge a b $(hoge) "$(hoge)" # hoge
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 5}));

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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}