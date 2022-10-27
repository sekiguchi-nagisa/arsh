
#include "gtest/gtest.h"

#include <constant.h>

#include "analyzer.h"
#include "indexer.h"
#include "symbol.h"

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

struct Loc {
  unsigned short modId;
  std::string rangeStr;

  Loc(unsigned short modId, std::string &&rangeStr) : modId(modId), rangeStr(std::move(rangeStr)) {}

  Loc(unsigned short modId, const char *str) : modId(modId), rangeStr(str) {}
};

class IndexTest : public ::testing::Test {
protected:
  ydsh::SysConfig sysConfig;
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

  void findDecl(const Request &req, const std::vector<Loc> &expected) {
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
      auto range = toRange(*retSrc, result.decl.getToken());
      ASSERT_TRUE(range.hasValue());

      actual.push_back(DeclResult{
          .modId = result.decl.getModId(),
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
      auto range = toRange(*retSrc, result.symbol.getToken());
      ASSERT_TRUE(range.hasValue());

      actual.push_back(RefsResult{
          .modId = result.symbol.getModId(),
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
      range = toRange(*src, value.request.getToken());
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
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 2}}, {{modId, "(1:22~1:24)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 3}}, {{modId, "(1:22~1:24)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 4}}, {{modId, "(1:22~1:24)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 5}}, std::vector<Loc>()));

  // reference
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 2, .character = 3}}, std::vector<Loc>()));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 22}},
                     {{modId, "(1:22~1:24)"},  // itself
                      {modId, "(2:2~2:4)"}})); // $e
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
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, {{modId, "(1:9~1:20)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 16}}, {{modId, "(2:2~2:4)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 6, .character = 11}},
                     {{modId, "(6:10~6:12)"},    // itself
                      {modId, "(6:26~6:28)"}})); // echo $a
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 7, .character = 12}},
                     {{modId, "(7:10~7:12)"},    // itself
                      {modId, "(7:26~7:28)"}})); // echo $a
}

TEST_F(IndexTest, scope3) {
  unsigned short modId;
  const char *content = "A=23 $A";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 0, .character = 0}},
                     {{modId, "(0:0~0:1)"},    // itself
                      {modId, "(0:5~0:7)"}})); // $A
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
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 0}}, {{modId, "(2:0~2:2)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 0}}, {{modId, "(1:4~1:5)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 4}},
                     {{modId, "(1:4~1:5)"},    // itself
                      {modId, "(4:0~4:2)"}})); // $a(234)
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 1}},
                     {{modId, "(2:0~2:2)"},    // itself
                      {modId, "(3:0~3:2)"}})); // $a + 34
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
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 2}}, std::vector<Loc>()));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, std::vector<Loc>()));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 28}}, {{modId, "(2:0~2:4)"}}));

  // reference
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 2}},
                     {{modId, "(2:0~2:4)"},      // itself
                      {modId, "(2:25~2:29)"},    // hoge;
                      {modId, "(3:0~3:4)"},      // hoge a b
                      {modId, "(3:11~3:15)"},    // $(hoge)
                      {modId, "(3:20~3:24)"}})); // "$(hoge)"
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
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 22}}, {{modId, "(1:8~1:16)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 12}},
                     {{modId, "(1:8~1:16)"},     // itself
                      {modId, "(2:16~2:24)"}})); // new StrArray()
}

TEST_F(IndexTest, globalImport) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
function EEE() for Int {}  # invalid method definition
typedef FFF() { typedef GGG = Error; }
function HHH() for FFF {}
)");

  unsigned short modId;
  auto content = format(R"(
source %s
$AAA + $BBB()
CCC
new [DDD]()
23.EEE()  # invalid method
new FFF.GGG('34')
new FFF().HHH()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 0, .symbolSize = 8}));
  ASSERT_EQ(1, modId);

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 1}}, {{2, "(2:4~2:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 9}}, {{2, "(3:9~3:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 1}}, {{2, "(4:0~4:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 7}}, {{2, "(5:8~5:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 7}}, {{2, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 8}}, {{2, "(7:24~7:27)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 7}}, {{2, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 13}}, {{2, "(8:9~8:12)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 2, .character = 5}},
                     {{2, "(2:4~2:7)"},    // itself
                      {2, "(3:30~3:34)"},  // return $AAA;
                      {1, "(2:0~2:4)"}})); // $AAA + $BBB()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 3, .character = 10}},
                     {{2, "(3:9~3:12)"},    // itself
                      {2, "(4:8~4:12)"},    // $BBB()
                      {1, "(2:7~2:11)"}})); // $AAA + $BBB()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 4, .character = 3}},
                     {{2, "(4:0~4:3)"},    // itself
                      {2, "(5:21~5:24)"},  // typeof(CCC)
                      {1, "(3:0~3:3)"}})); // CCC
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 5, .character = 9}},
                     {{2, "(5:8~5:11)"},   // itself
                      {1, "(4:5~4:8)"}})); // new [DDD]()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 7, .character = 11}},
                     {{2, "(7:8~7:11)"},   // itself
                      {2, "(8:19~8:22)"},  // function HHH() for FFF {}
                      {1, "(6:4~6:7)"},    // new FFF.GGG('34')
                      {1, "(7:4~7:7)"}})); // new FFF.HH()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 7, .character = 24}},
                     {{2, "(7:24~7:27)"},   // itself
                      {1, "(6:8~6:11)"}})); // new FFF.GGG('34')
}

TEST_F(IndexTest, namedImport) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
function EEE() : Int for Int { return 34; }  #  invalid method definition
typedef FFF() { var value = 34; }
function HHH() for FFF {}
)");

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
$mod.AAA + $mod.BBB()
mod 2>&1 > /dev/null CCC 34
new [mod.DDD]()
23.EEE()  # invalid method call
new mod.FFF().value
new mod.FFF().HHH()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 1, .symbolSize = 15}));
  ASSERT_EQ(1, modId);

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 13}}, {{modId, "(1:3~1:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 7}}, {{modId, "(1:3~1:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 8}}, {{2, "(2:4~2:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 19}}, {{2, "(3:9~3:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 23}}, {{2, "(4:0~4:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 10}}, {{2, "(5:8~5:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 10}}, {{2, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 18}}, {{2, "(7:20~7:25)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 10}}, {{2, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 17}}, {{2, "(8:9~8:12)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 6}},
                     {{modId, "(1:3~1:6)"}, // itself
                      {modId, "(2:0~2:4)"},
                      {modId, "(2:11~2:15)"},
                      {modId, "(3:0~3:3)"},
                      {modId, "(4:5~4:8)"},
                      {modId, "(6:4~6:7)"},
                      {modId, "(7:4~7:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 2, .character = 5}}, {{2, "(2:4~2:7)"}, // itself
                                                                     {2, "(3:30~3:34)"},
                                                                     {1, "(2:5~2:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 3, .character = 10}}, {{2, "(3:9~3:12)"}, // itself
                                                                      {2, "(4:8~4:12)"},
                                                                      {1, "(2:16~2:19)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 4, .character = 3}}, {{2, "(4:0~4:3)"}, // itself
                                                                     {2, "(5:21~5:24)"},
                                                                     {1, "(3:21~3:24)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 5, .character = 9}}, {{2, "(5:8~5:11)"}, // itself
                                                                     {1, "(4:9~4:12)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 7, .character = 10}},
                     {{2, "(7:8~7:11)"},    // itself
                      {2, "(8:19~8:22)"},   // function HHH() for FFF {}
                      {1, "(6:8~6:11)"},    // new mod.FFF().value
                      {1, "(7:8~7:11)"}})); // new mod.FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 7, .character = 24}}, {{2, "(7:20~7:25)"}, // itself
                                                                      {1, "(6:14~6:19)"}}));

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 8, .character = 12}},
                     {{2, "(8:9~8:12)"},     // itself
                      {1, "(7:14~7:17)"}})); // new mod.FFF().HHH()
}

TEST_F(IndexTest, namedImportInlined) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
function EEE() : Int for Int { return $this; }  # invalid method definition
typedef FFF() { var value = 34; }
function HHH() for FFF {}
)");

  fileName =
      tempFileFactory.createTempFile("inlined.ds", format("source %s inlined", fileName.c_str()));

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
$mod.AAA + $mod.BBB()
mod CCC
new [mod.DDD]()
34.EEE()  # invalid method call
new mod.FFF().value
new mod.FFF().HHH()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 1, .symbolSize = 15}));
  ASSERT_EQ(1, modId);

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 13}}, {{modId, "(1:3~1:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 7}}, {{modId, "(1:3~1:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 8}}, {{3, "(2:4~2:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 19}}, {{3, "(3:9~3:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 5}}, {{3, "(4:0~4:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 10}}, {{3, "(5:8~5:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 10}}, {{3, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 18}}, {{3, "(7:20~7:25)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 10}}, {{3, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 17}}, {{3, "(8:9~8:12)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 6}},
                     {{modId, "(1:3~1:6)"}, // itself
                      {modId, "(2:0~2:4)"},
                      {modId, "(2:11~2:15)"},
                      {modId, "(3:0~3:3)"},
                      {modId, "(4:5~4:8)"},
                      {modId, "(6:4~6:7)"},
                      {modId, "(7:4~7:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 2, .character = 5}}, {{3, "(2:4~2:7)"}, // itself
                                                                     {3, "(3:30~3:34)"},
                                                                     {1, "(2:5~2:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 3, .character = 10}}, {{3, "(3:9~3:12)"}, // itself
                                                                      {3, "(4:8~4:12)"},
                                                                      {1, "(2:16~2:19)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 4, .character = 3}}, {{3, "(4:0~4:3)"}, // itself
                                                                     {3, "(5:21~5:24)"},
                                                                     {1, "(3:4~3:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 5, .character = 9}}, {{3, "(5:8~5:11)"}, // itself
                                                                     {1, "(4:9~4:12)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 7, .character = 10}},
                     {{3, "(7:8~7:11)"},    // itself
                      {3, "(8:19~8:22)"},   // function HHH() for FFF {}
                      {1, "(6:8~6:11)"},    // new mod.FFF().value
                      {1, "(7:8~7:11)"}})); // new mod.FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 7, .character = 24}}, {{3, "(7:20~7:25)"}, // itself
                                                                      {1, "(6:14~6:19)"}}));

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 8, .character = 12}},
                     {{3, "(8:9~8:12)"},     // itself
                      {1, "(7:14~7:17)"}})); // new mod.FFF().HHH()
}

TEST_F(IndexTest, inlinedImport) {
  ydsh::TempFileFactory tempFileFactory("ydsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB() : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
function EEE() : Int for Int { return $this; }    # invalid method definition
typedef FFF() { var value = 34; }
function HHH() for FFF {}
)");

  fileName =
      tempFileFactory.createTempFile("inlined.ds", format("source %s inlined", fileName.c_str()));

  unsigned short modId;
  auto content = format(R"(
source %s
$AAA + $BBB()
CCC
new [DDD]()
90.EEE()  # invalid method
new FFF().value
new FFF().HHH()
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 0, .symbolSize = 8}));
  ASSERT_EQ(1, modId);

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 1}}, {{3, "(2:4~2:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 9}}, {{3, "(3:9~3:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 1}}, {{3, "(4:0~4:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 7}}, {{3, "(5:8~5:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 6}}, {{3, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 12}}, {{3, "(7:20~7:25)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 6}}, {{3, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 13}}, {{3, "(8:9~8:12)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 2, .character = 5}}, {{3, "(2:4~2:7)"},   // itself
                                                                     {3, "(3:30~3:34)"}, //
                                                                     {1, "(2:0~2:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 3, .character = 10}}, {{3, "(3:9~3:12)"}, // itself
                                                                      {3, "(4:8~4:12)"},
                                                                      {1, "(2:7~2:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 4, .character = 3}}, {{3, "(4:0~4:3)"}, // itself
                                                                     {3, "(5:21~5:24)"},
                                                                     {1, "(3:0~3:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 5, .character = 9}}, {{3, "(5:8~5:11)"},   // itself
                                                                     {1, "(4:5~4:8)"}})); //
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 7, .character = 10}},
                     {{3, "(7:8~7:11)"},   // itself
                      {3, "(8:19~8:22)"},  // function HHH() for FFF {}
                      {1, "(6:4~6:7)"},    // new mod.FFF().value
                      {1, "(7:4~7:7)"}})); // new mod.FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 7, .character = 23}}, {{3, "(7:20~7:25)"}, // itself
                                                                      {1, "(6:10~6:15)"}}));

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 8, .character = 12}},
                     {{3, "(8:9~8:12)"},     // itself
                      {1, "(7:10~7:13)"}})); // new FFF().HHH()
}

TEST_F(IndexTest, udc_overwrite) {
  unsigned short modId;
  const char *content = R"E(
echo() {}
echo hello
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 2}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 0}}, {{modId, "(1:0~1:4)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 2}},
                     {{modId, "(1:0~1:4)"}, {modId, "(2:0~2:4)"}}));
}

TEST_F(IndexTest, udtype) {
  unsigned short modId;
  const char *content = R"E(
    typedef APIError : Error
    typedef AAA($e : APIError) {
      let error = $e
      import-env HOME
    }
    var a = new AAA(new APIError("hello"))
    $a.error
    $a.HOME
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 6, .symbolSize = 14}));

  // definition
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 6, .character = 27}}, // APIError
                     {{modId, "(1:12~1:20)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 6, .character = 19}}, // AAA
                     {{modId, "(2:12~2:15)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 3, .character = 20}}, // $e
                     {{modId, "(2:16~2:18)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 8, .character = 10}}, // AAA.HOME
                     {{modId, "(4:17~4:21)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 17}}, // APIError
                     {{modId, "(1:12~1:20)"},                                           // itself
                      {modId, "(2:21~2:29)"},    // $e : APIError
                      {modId, "(6:24~6:32)"}})); // new APIError
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 18}}, // $e
                     {{modId, "(2:16~2:18)"},                                           // itself
                      {modId, "(3:18~3:20)"}})); // let error = $e
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId,
              .position = {.line = 2, .character = 15}}, // typedef AAA($e : APIError)
      {{modId, "(2:12~2:15)"},                           // itself
       {modId, "(6:16~6:19)"}}));                        // var a = new AAA(new APIError("hello"))
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 3, .character = 11}}, // let error = $e
      {{modId, "(3:10~3:15)"},                                           // itself
       {modId, "(7:7~7:12)"}}));                                         // $a.error
}

TEST_F(IndexTest, udtypeRec) {
  unsigned short modId;
  const char *content = R"E(
    typedef AAA($n : AAA!) {
      let next = $n
      typedef Next = Option<typeof(new AAA(new AAA!()))>
    }
    new AAA(new AAA.Next()).next
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 4, .symbolSize = 12}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 50}}, // new AAA!()
      {{modId, "(1:12~1:15)"}}));                                        // typedef AAA($n : AAA!)
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 5, .character = 23}}, // AAA.Next
                     {{modId, "(3:14~3:18)"}})); // typedef Next

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, // typedef AAA($n : AAA!)
      {{modId, "(1:12~1:15)"},                                           // itself
       {modId, "(1:21~1:24)"},                                           // $n : AAA!
       {modId, "(3:39~3:42)"},                                           // new AAA(new AAA!())
       {modId, "(3:47~3:50)"},                                           // new AAA!()
       {modId, "(5:8~5:11)"},     //     new AAA(new AAA.Next()).next
       {modId, "(5:16~5:19)"}})); //     new AAA.Next()
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId,
              .position = {.line = 3,
                           .character = 17}}, // typedef Next = Option<typeof(new AAA(new AAA!()))>
      {{modId, "(3:14~3:18)"},                // itself
       {modId, "(5:20~5:24)"}}));             //     new AAA(new AAA.Next()).next
}

TEST_F(IndexTest, method) {
  unsigned short modId;
  const char *content = R"E(typedef BigInt(a : Int) { var v = $a; }
    function factorial() : Int for BigInt {
        return $this.v == 0 ? 1 : $this.v * new BigInt($this.v - 1).factorial()
    }
    new BigInt(23).factorial()
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 16}));

  // definition ( function factorial() : Int for BigInt )
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId,
              .position = {.line = 2, .character = 70}}, // new BigInt($this.v - 1).factorial()
      {{modId, "(1:13~1:22)"}}));                        // function factorial() : Int for BigInt
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId,
              .position = {.line = 4, .character = 24}}, // new BigInt(23).factorial()
      {{modId, "(1:13~1:22)"}}));                        // function factorial() : Int for Int
  /**
   * this
   */
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 17}}, // $this.v == 0
      {{modId, "(2:15~2:20)"}}));                                        // $this
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 37}}, // $this.v * new
      {{modId, "(2:15~2:20)"}}));                                        // $this
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 58}}, // new BigInt($this.v - 1)
      {{modId, "(2:15~2:20)"}}));                                        // $this

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId,
              .position = {.line = 1, .character = 17}}, // function factorial() : Int for BigInt
      {{modId, "(1:13~1:22)"},                           // itself
       {modId, "(2:68~2:77)"},                           // ($this.v - 1).factorial()
       {modId, "(4:19~4:28)"}}));                        // new BigInt(23).factorial()
  /**
   * this
   */
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 19}}, // $this
                     {{modId, "(2:15~2:20)"},                                           // itself
                      {modId, "(2:34~2:39)"},    // $this.v * new BigInt($this.v - 1)
                      {modId, "(2:55~2:60)"}})); // new BigInt($this.v - 1)
}

TEST_F(IndexTest, methodOverride) {
  unsigned short modId;
  const char *content = R"E(typedef ANY : Error; typedef INT : ANY; typedef STR : ANY
    function print() for ANY {}
    function print() for INT {}
    new INT().print()
    (new INT() as ANY).print()
    new STR().print()
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 16}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 18}}, // new INT().print()
      {{modId, "(2:13~2:18)"}})); // function print() for INT {}
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId,
              .position = {.line = 4, .character = 26}}, // (new INT() as ANY).print()
      {{modId, "(1:13~1:18)"}}));                        // function print() for ANY {}
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 19}}, // new STR().print()
      {{modId, "(1:13~1:18)"}})); // function print() for INT {}

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 18}}, // function print() for ANY
      {{modId, "(1:13~1:18)"},                                           // itself
       {modId, "(4:23~4:28)"},    // (new INT() as ANY).print()
       {modId, "(5:14~5:19)"}})); // new STR().print()
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 2, .character = 18}}, // function print() for INT
      {{modId, "(2:13~2:18)"},                                           // itself
       {modId, "(3:14~3:19)"}}));                                        // new INT().print()
}

TEST_F(IndexTest, upvar) {
  unsigned short modId;
  const char *content = R"E({
    var value = 3433
    function() => {
      function() -> {
        $value++
        var value = 34
        $value++
      }
      $value++
    }
    $value++
})E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 6}));

  // defintion
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 13}}, {{modId, "(1:8~1:13)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 14}}, {{modId, "(5:12~5:17)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 8, .character = 12}}, {{modId, "(1:8~1:13)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 10, .character = 10}}, {{modId, "(1:8~1:13)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 8}}, // var value = 3433
      {{modId, "(1:8~1:13)"},                                           // itself
       {modId, "(4:8~4:14)"},
       {modId, "(8:6~8:12)"},
       {modId, "(10:4~10:10)"}}));

  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 5, .character = 16}}, // var value = 34
      {{modId, "(5:12~5:17)"},                                           // itself
       {modId, "(6:8~6:14)"}}));
}

TEST_F(IndexTest, backrefFunc) {
  unsigned short modId;
  const char *content = R"E(
  function AAA() {
    $BBB()
  }
  function BBB() {
    $AAA()
  }
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 4}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, {{modId, "(1:11~1:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 8}}, {{modId, "(4:11~4:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 12}}, {{modId, "(4:11~4:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 5}}, {{modId, "(1:11~1:14)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, // function AAA()
      {{modId, "(1:11~1:14)"},                                           // itself
       {modId, "(5:4~5:8)"}}));

  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 4, .character = 12}}, // function BBB()
      {{modId, "(4:11~4:14)"},                                           // itself
       {modId, "(2:4~2:8)"}}));
}

TEST_F(IndexTest, backrefMethod) {
  unsigned short modId;
  const char *content = R"E(typedef INT() {}
  function AAA() for INT {
    $this.BBB()
  }
  function BBB() for INT {
    $this.AAA()
  }
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 9}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, {{modId, "(1:11~1:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 13}}, {{modId, "(4:11~4:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 12}}, {{modId, "(4:11~4:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 12}}, {{modId, "(1:11~1:14)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, // function AAA()
      {{modId, "(1:11~1:14)"},                                           // itself
       {modId, "(5:10~5:13)"}}));

  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 4, .character = 12}}, // function BBB()
      {{modId, "(4:11~4:14)"},                                           // itself
       {modId, "(2:10~2:13)"}}));
}

TEST_F(IndexTest, backrefUDC) {
  unsigned short modId;
  const char *content = R"E(
  AAA() {
    BBB 34
  }
  # comment
  BBB() {
    AAA 45
  }
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 4}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 4}}, {{modId, "(1:2~1:5)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 4}}, {{modId, "(5:2~5:5)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 3}}, {{modId, "(5:2~5:5)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 7}}, {{modId, "(1:2~1:5)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 5}}, // AAA()
                     {{modId, "(1:2~1:5)"},                                            // itself
                      {modId, "(6:4~6:7)"}}));

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 5, .character = 3}}, // BBB()
                     {{modId, "(5:2~5:5)"},                                            // itself
                      {modId, "(2:4~2:7)"}}));
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
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 7}}, std::vector<Loc>()));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 6}}, std::vector<Loc>()));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 6}}, std::vector<Loc>()));
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
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 8}}, std::vector<Loc>()));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 2, .character = 12}}, std::vector<Loc>()));
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
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 1}}, {{modId, "(0:0~0:4)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 2}}, std::vector<Loc>()));
}

TEST_F(IndexTest, invalidField) {
  unsigned short modId;
  const char *content = R"(
typedef AAA($a : Int) {
  var value = $a
  var next = new AAA(12).value
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 3, .symbolSize = 5}));
}

TEST_F(IndexTest, invalidBackref) {
  unsigned short modId;
  const char *content = R"(
  function AAA() {
    $BBB()
  }
  $AAA()
  function BBB() {
    $AAA()
  }
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 4}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 8}}, std::vector<Loc>()));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 5, .character = 13}}, // BBB()
                     {{modId, "(5:11~5:14)"}}));                                        // itself
}

TEST_F(IndexTest, hover) {
  // variable or function
  ASSERT_NO_FATAL_FAILURE(this->hover("let A = 34\n$A", 1, "```ydsh\nlet A : Int\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("import-env HOME\n$HOME", 1, "```ydsh\nimportenv HOME : String\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("export-env ZZZ = 'hoge'\n$ZZZ", 1, "```ydsh\nexportenv ZZZ : String\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("function hoge($s : Int) {}\n$hoge", 1,
                                      "```ydsh\nfunction hoge(s : Int) : Void\n```"));

  // user-defined command
  ASSERT_NO_FATAL_FAILURE(this->hover("hoge(){}\nhoge", 1, "```ydsh\nhoge() : Bool\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("usage() : Nothing { throw 34; }\nusage", 1, "```ydsh\nusage() : Nothing\n```"));

  // user-defined type
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef App : OutOfRangeError\n34 is\nApp", 2,
                                      "```ydsh\ntypedef App : OutOfRangeError\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("typedef AppError : Error; typedef API : AppError\n34 is\nAPI", 2,
                  "```ydsh\ntypedef API : AppError\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("typedef Interval { var begin = 34; }; var a = new Interval();\n$a",
                  Position{.line = 1, .character = 0}, "```ydsh\nvar a : Interval\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef Interval(s : Int) { var n = $s; let value = new "
                                      "Interval!(); }\nvar a = new Interval();",
                                      Position{.line = 1, .character = 15}, R"(```ydsh
typedef Interval(s : Int) {
    var n : Int
    let value : Interval!
}
```)"));
  ASSERT_NO_FATAL_FAILURE(this->hover(
      "typedef Interval { var value = new Interval!(); }; var a = new Interval();\n$a.value",
      Position{.line = 1, .character = 3}, "```ydsh\nvar value : Interval! for Interval\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover(
      "typedef Interval { var value = new Interval!(); }; var aaa = new [[Interval]]();\n$aaa",
      Position{.line = 1, .character = 2}, "```ydsh\nvar aaa : [[Interval]]\n```"));

  // user-defined method
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef INT(a : Int) { var v = $a; }\n"
                                      "function value():Int for INT { return $this.v; }\n"
                                      "new INT(12).value()",
                                      Position{.line = 2, .character = 13},
                                      "```ydsh\nfunction value() : Int for INT\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("function value():Int for String { \nreturn $this.size(); }",
                                      Position{.line = 1, .character = 8},
                                      "```ydsh\nlet this : String\n```"));
}

TEST_F(IndexTest, hoverBuiltin) {
  // builtin variable or type alias
  ASSERT_NO_FATAL_FAILURE(this->hover("$?", 0, "```ydsh\nvar ? : Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("hoge() { \n$@;}", 1, "```ydsh\nlet @ : [String]\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$YDSH_VERSION", 0,
                                      "```ydsh\nconst YDSH_VERSION = '" X_INFO_VERSION_CORE "'"
                                      "\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("$true is\nBoolean", 1, "```ydsh\ntypedef Boolean = Bool\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("$SCRIPT_NAME", 0, "```ydsh\nconst SCRIPT_NAME = '/dummy_5'\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$SCRIPT_DIR", 0, "```ydsh\nconst SCRIPT_DIR = '/'\n```"));

  // builtin command
  ASSERT_NO_FATAL_FAILURE(
      this->hover(":", 0,
                  "```md\n"
                  ":: : \n"
                  "    Null command.  Always success (exit status is 0).\n```"));

  // builtin tuple or method
  ASSERT_NO_FATAL_FAILURE(this->hover("var a = (34, $false, '');$a._2\n$a._2",
                                      Position{.line = 1, .character = 3},
                                      "```ydsh\nvar _2 : String for (Int, Bool, String)\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("''.size();[1].size()\n[0].size()",
                                      Position{.line = 1, .character = 5},
                                      "```ydsh\nfunction size() : Int for [Int]\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("''.slice(0)", Position{.line = 0, .character = 5},
                  "```ydsh\nfunction slice(p0 : Int, p1 : Int!) : String for String\n```"));
}

TEST_F(IndexTest, hoverMod) {
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

TEST_F(IndexTest, hoverConst) {
  ASSERT_NO_FATAL_FAILURE(this->hover("$TRUE", 0, "```ydsh\nconst TRUE = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$True", 0, "```ydsh\nconst True = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$true", 0, "```ydsh\nconst true = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$FALSE", 0, "```ydsh\nconst FALSE = false\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$False", 0, "```ydsh\nconst False = false\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$false", 0, "```ydsh\nconst false = false\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("$ON_ASSERT", 0, "```ydsh\nconst ON_ASSERT = 4\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$ON_ERR", 0, "```ydsh\nconst ON_ERR = 2\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$ON_EXIT", 0, "```ydsh\nconst ON_EXIT = 1\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("$SIGHUP", 0, "```ydsh\nconst SIGHUP = signal(1)\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$SIGKILL", 0, "```ydsh\nconst SIGKILL = signal(9)\n```"));
}

TEST_F(IndexTest, docSymbol) {
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::VAR));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::LET));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::IMPORT_ENV));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::EXPORT_ENV));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::MOD));
  ASSERT_EQ(SymbolKind::Constant, toSymbolKind(DeclSymbol::Kind::CONST));
  ASSERT_EQ(SymbolKind::Constant, toSymbolKind(DeclSymbol::Kind::MOD_CONST));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::FUNC));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::CMD));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::BUILTIN_CMD));
  ASSERT_EQ(SymbolKind::Method, toSymbolKind(DeclSymbol::Kind::METHOD));
  ASSERT_EQ(SymbolKind::Constructor, toSymbolKind(DeclSymbol::Kind::CONSTRUCTOR));
  ASSERT_EQ(SymbolKind::Class, toSymbolKind(DeclSymbol::Kind::TYPE_ALIAS));
  ASSERT_EQ(SymbolKind::Class, toSymbolKind(DeclSymbol::Kind::ERROR_TYPE_DEF));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}