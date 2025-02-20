
#include "gtest/gtest.h"

#include <config.h>
#include <format_signature.h>

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
    ASSERT_EQ(size.declSize, index->getDecls().size());
    ASSERT_EQ(size.symbolSize, index->getSymbols().size());
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
      Request{.modId = modId, .position = {.line = 2, .character = 4}}, std::vector<Loc>()));

  // reference
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 3}},
                     {{modId, "(1:22~1:24)"},  // itself
                      {modId, "(2:2~2:4)"}})); // $e
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 22}},
                     {{modId, "(1:22~1:24)"},  // itself
                      {modId, "(2:2~2:4)"}})); // $e
}

static std::string toString(const ScopeInterval &interval) {
  std::string value = "(";
  value += std::to_string(interval.beginPos);
  value += ", ";
  value += std::to_string(interval.endPos);
  value += ")";
  return value;
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
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 13}));

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
      this->findRefs(Request{.modId = modId, .position = {.line = 7, .character = 10}},
                     {{modId, "(7:10~7:12)"},    // itself
                      {modId, "(7:26~7:28)"}})); // echo $a

  // scope
  auto index = this->indexes.find(static_cast<ModId>(modId));
  ASSERT_TRUE(index);
  auto &scopes = index->getScopes();
  ASSERT_EQ(4, scopes.size());
  for (unsigned int i = 0; i < scopes.size() - 1; i++) {
    auto &cur = scopes[i];
    auto &next = scopes[i + 1];

    SCOPED_TRACE("i=" + std::to_string(i) + ", cur=" + toString(cur) + ". next=" + toString(next));
    ASSERT_TRUE(cur.beginPos <= next.beginPos);
    ASSERT_TRUE(cur != next);
  }
  for (unsigned int i = 0; i < scopes.size(); i++) {
    auto &base = scopes[0];
    auto &cur = scopes[i];
    SCOPED_TRACE("i=" + std::to_string(i) + ", base=" + toString(base) + ". cur=" + toString(cur));
    ASSERT_TRUE(base.isIncluding(cur));
  }
  ASSERT_TRUE(scopes[1].isIncluding(scopes[2]));
  ASSERT_TRUE(scopes[1].isIncluding(scopes[3]));
  ASSERT_FALSE(scopes[1].isIncluding(scopes[0]));
  ASSERT_FALSE(scopes[2].isIncluding(scopes[0]));
  ASSERT_FALSE(scopes[2].isIncluding(scopes[1]));
  ASSERT_FALSE(scopes[2].isIncluding(scopes[3]));
  ASSERT_FALSE(scopes[3].isIncluding(scopes[2]));
  ASSERT_FALSE(scopes[3].isIncluding(scopes[1]));
  ASSERT_FALSE(scopes[3].isIncluding(scopes[0]));
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

TEST_F(IndexTest, scope4) {
  unsigned short modId;
  const char *content = "A=23 B=99 echo $A$B";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 5}));

  // definition
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 0, .character = 15}},
                     {{modId, "(0:0~0:1)"}})); // A=23
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 0, .character = 16}},
                     {{modId, "(0:0~0:1)"}})); // A=23
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 0, .character = 17}},
                     {{modId, "(0:5~0:6)"}})); // B=99
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 0, .character = 18}},
                     {{modId, "(0:5~0:6)"}})); // B=99
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 0, .character = 19}}, std::vector<Loc>()));

  // reference
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 0, .character = 0}},
                     {{modId, "(0:0~0:1)"},      // itself
                      {modId, "(0:15~0:17)"}})); // $A

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 0, .character = 5}},
                     {{modId, "(0:5~0:6)"},      // itself
                      {modId, "(0:17~0:19)"}})); // $B
}

TEST_F(IndexTest, func1) {
  unsigned short modId;
  const char *content = R"(
var a = function (
$a : Int) =>
$a + 34
$a(234)
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 5}));

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

TEST_F(IndexTest, cmd2) {
  unsigned short modId;
  const char *content = R"E(
true
if(echo && echo) {
  echo
}

)E";
  // for builtin command
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 0, .symbolSize = 4}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 2}},
                     {{modId, "(1:0~1:4)"}})); // true
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 4}},
                     {{modId, "(2:3~2:7)"},    // echo && echo
                      {modId, "(2:11~2:15)"},  // && echo
                      {modId, "(3:2~3:6)"}})); // echo
}

TEST_F(IndexTest, type1) {
  unsigned short modId;
  const char *content = R"E(typedef StrArray = Int; {
typedef StrArray = [String]
new [typeof(new StrArray())]()
}
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 5}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 22}}, {{modId, "(1:8~1:16)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 12}},
                     {{modId, "(1:8~1:16)"},     // itself
                      {modId, "(2:16~2:24)"}})); // new StrArray()
}

TEST_F(IndexTest, namedArgFunc) {
  unsigned short modId;
  const char *content = R"E(
function fff(aa: Int, _bb: String) : String{
  return $aa + {
  $_bb; }
}
$fff($_bb: "ew45", $aa: 12)
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 3, .symbolSize = 11}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, {{modId, "(1:13~1:15)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 11}}, {{modId, "(1:13~1:15)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 19}}, {{modId, "(1:13~1:15)"}}));

  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 24}}, {{modId, "(1:22~1:25)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 5}}, {{modId, "(1:22~1:25)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 6}}, {{modId, "(1:22~1:25)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 9}},
                     {
                         {modId, "(1:13~1:15)"}, // itself
                         {modId, "(2:9~2:12)"},  // $aa
                         {modId, "(5:19~5:22)"}, // $aa:
                     }));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 5, .character = 7}},
                     {
                         {modId, "(1:22~1:25)"}, // itself
                         {modId, "(3:2~3:6)"},   // $_bb
                         {modId, "(5:5~5:9)"},   // $_bb:
                     }));
}

TEST_F(IndexTest, namedArgMethod) {
  unsigned short modId;
  const char *content = R"E(
typedef AAA: Error
function format(value: String): String for AAA {
  return $this.name() + ": " +
  $value
}
new AAA('').format(
$value: 'hey')
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 4, .symbolSize = 13}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 5}}, {{modId, "(2:16~2:21)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 2}}, {{modId, "(2:16~2:21)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 7, .character = 4}},
                     {
                         {modId, "(2:16~2:21)"}, // itself
                         {modId, "(4:2~4:8)"},   // $value
                         {modId, "(7:0~7:6)"},   // $value:
                     }));
}

TEST_F(IndexTest, namedArgBuiltinMethod) {
  unsigned short modId;
  const char *content = R"E(
"234".slice($end:444, $start: 2222)
"2345".slice($start:345, $end: 333)
[234].slice($end:444, $start: 2222)
['ss'].slice($start:345, $end: 333)
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 0, .symbolSize = 12}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 25}},
                     {
                         {modId, "(1:22~1:28)"}, // $start: 2222
                         {modId, "(2:13~2:19)"}, // $start:345
                     }));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 26}},
                     {
                         {modId, "(1:12~1:16)"}, // $end:444
                         {modId, "(2:25~2:29)"}, // $end: 333
                     }));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 3, .character = 25}},
                     {
                         {modId, "(3:22~3:28)"}, // $start: 2222
                         {modId, "(4:13~4:19)"}, // $start:345
                     }));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 4, .character = 26}},
                     {
                         {modId, "(3:12~3:16)"}, // $end:444
                         {modId, "(4:25~4:29)"}, // $end: 333
                     }));
}

TEST_F(IndexTest, namedArgExplictConstructor) {
  unsigned short modId;
  const char *content = R"E(
typedef Interval(aaa:Int, bbb:Int) {
  let begin = $aaa
  let end = $bbb
}
new Interval($bbb: 34, $aaa: 2)
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 12}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 26}}, {{modId, "(1:17~1:20)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 16}}, {{modId, "(1:17~1:20)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 18}}, {{modId, "(1:17~1:20)"}}));

  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 28}}, {{modId, "(1:26~1:29)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 13}}, {{modId, "(1:26~1:29)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 13}}, {{modId, "(1:26~1:29)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 5, .character = 25}},
                     {
                         {modId, "(1:17~1:20)"}, // itself
                         {modId, "(2:14~2:18)"}, // $aaa
                         {modId, "(5:23~5:27)"}, // $aaa:
                     }));

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 5, .character = 14}},
                     {
                         {modId, "(1:26~1:29)"}, // itself
                         {modId, "(3:12~3:16)"}, // $bbb
                         {modId, "(5:13~5:17)"}, // $bbb:
                     }));
}

TEST_F(IndexTest, namedArgImplicitConstructor) {
  unsigned short modId;
  const char *content = R"E(
typedef Interval {
  let begin : Int
  let end : Int
}
var aa = new Interval($end: 34, $begin: 2)
new Interval($begin:34, $end: 2345)
$aa.begin +
$aa.end
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 4, .symbolSize = 16}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 36}}, {{modId, "(2:6~2:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 16}}, {{modId, "(2:6~2:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 8}}, {{modId, "(2:6~2:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 23}}, {{modId, "(3:6~3:9)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 25}}, {{modId, "(3:6~3:9)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 8, .character = 6}}, {{modId, "(3:6~3:9)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 8}},
                     {
                         {modId, "(2:6~2:11)"},  // itself
                         {modId, "(5:32~5:38)"}, // $begin: 2
                         {modId, "(6:13~6:19)"}, // $begin:34
                         {modId, "(7:4~7:9)"},   // $aa.begin
                     }));

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 3, .character = 8}},
                     {
                         {modId, "(3:6~3:9)"},   // itself
                         {modId, "(5:22~5:26)"}, // $end: 34
                         {modId, "(6:24~6:28)"}, // $end: 2345
                         {modId, "(8:4~8:7)"},   // $aa.end
                     }));
}

TEST_F(IndexTest, globalImport) {
  TempFileFactory tempFileFactory("arsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB(aa: Int?) : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
function EEE() for Int {}  # invalid method definition
typedef FFF(bb:String?) { typedef GGG = Error; }
function HHH(cc: Int?) for FFF {}
)");

  unsigned short modId;
  auto content = format(R"(
source %s
$AAA + $BBB($aa: 2345)
CCC
new [DDD]()
23.EEE()  # invalid method
new FFF.GGG('34')
new FFF($bb:'').HHH($cc:22)
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 0, .symbolSize = 11}));
  ASSERT_EQ(1, modId);

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 1}}, {{2, "(2:4~2:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 9}}, {{2, "(3:9~3:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 14}}, {{2, "(3:13~3:15)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 1}}, {{2, "(4:0~4:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 7}}, {{2, "(5:8~5:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 6}}, {{2, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 8}}, {{2, "(7:34~7:37)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 6}}, {{2, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 9}}, {{2, "(7:12~7:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 18}}, {{2, "(8:9~8:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 22}}, {{2, "(8:13~8:15)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 2, .character = 5}},
                     {{2, "(2:4~2:7)"},    // itself
                      {2, "(3:38~3:42)"},  // return $AAA;
                      {1, "(2:0~2:4)"}})); // $AAA + $BBB()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 3, .character = 10}},
                     {{2, "(3:9~3:12)"},    // itself
                      {2, "(4:8~4:12)"},    // $BBB()
                      {1, "(2:7~2:11)"}})); // $AAA + $BBB()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 3, .character = 13}},
                     {{2, "(3:13~3:15)"},    // itself
                      {1, "(2:12~2:15)"}})); // $AAA + $BBB($aa: 2345)
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 4, .character = 2}},
                     {{2, "(4:0~4:3)"},    // itself
                      {2, "(5:21~5:24)"},  // typeof(CCC)
                      {1, "(3:0~3:3)"}})); // CCC
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 5, .character = 9}},
                     {{2, "(5:8~5:11)"},   // itself
                      {1, "(4:5~4:8)"}})); // new [DDD]()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 7, .character = 10}},
                     {{2, "(7:8~7:11)"},   // itself
                      {2, "(8:27~8:30)"},  // function HHH() for FFF {}
                      {1, "(6:4~6:7)"},    // new FFF.GGG('34')
                      {1, "(7:4~7:7)"}})); // new FFF.HH()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 7, .character = 13}},
                     {{2, "(7:12~7:14)"},   // itself
                      {1, "(7:8~7:11)"}})); // new FFF($bb:'')
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 7, .character = 36}},
                     {{2, "(7:34~7:37)"},   // itself
                      {1, "(6:8~6:11)"}})); // new FFF.GGG('34')
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 8, .character = 14}},
                     {{2, "(8:13~8:15)"},    // itself
                      {1, "(7:20~7:23)"}})); // new FFF($bb:'').HHH($cc:22)
}

TEST_F(IndexTest, namedImport) {
  TempFileFactory tempFileFactory("arsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB(aa:Int) : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
function EEE() : Int for Int { return 34; }  #  invalid method definition
typedef FFF(bb:Int?) { var value = 34; }
function HHH(cc:Int?) for FFF {}
)");

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
$mod.AAA + $mod.BBB($aa:44)
mod 2>&1 > /dev/null CCC 34
new [mod.DDD]()
23.EEE()  # invalid method call
new mod.FFF($bb:32).value
new mod.FFF($bb:11).HHH($cc:2)
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 1, .symbolSize = 19}));
  ASSERT_EQ(1, modId);

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 13}}, {{modId, "(1:3~1:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 7}}, {{modId, "(1:3~1:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 7}}, {{2, "(2:4~2:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 18}}, {{2, "(3:9~3:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 22}}, {{2, "(3:13~3:15)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 23}}, {{2, "(4:0~4:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 10}}, {{2, "(5:8~5:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 10}}, {{2, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 13}}, {{2, "(7:12~7:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 22}}, {{2, "(7:27~7:32)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 10}}, {{2, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 22}}, {{2, "(8:9~8:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 26}}, {{2, "(8:13~8:15)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 5}},
                     {{modId, "(1:3~1:6)"}, // itself
                      {modId, "(2:0~2:4)"},
                      {modId, "(2:11~2:15)"},
                      {modId, "(3:0~3:3)"},
                      {modId, "(4:5~4:8)"},
                      {modId, "(6:4~6:7)"},
                      {modId, "(7:4~7:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 2, .character = 5}}, {{2, "(2:4~2:7)"}, // itself
                                                                     {2, "(3:36~3:40)"},
                                                                     {1, "(2:5~2:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 3, .character = 10}}, {{2, "(3:9~3:12)"}, // itself
                                                                      {2, "(4:8~4:12)"},
                                                                      {1, "(2:16~2:19)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 3, .character = 14}},
                     {{2, "(3:13~3:15)"},    // itself
                      {1, "(2:20~2:23)"}})); // $mod.BBB($aa:44)
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 4, .character = 2}}, {{2, "(4:0~4:3)"}, // itself
                                                                     {2, "(5:21~5:24)"},
                                                                     {1, "(3:21~3:24)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 5, .character = 9}}, {{2, "(5:8~5:11)"}, // itself
                                                                     {1, "(4:9~4:12)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 7, .character = 10}},
                     {{2, "(7:8~7:11)"},    // itself
                      {2, "(8:26~8:29)"},   // function HHH() for FFF {}
                      {1, "(6:8~6:11)"},    // new mod.FFF().value
                      {1, "(7:8~7:11)"}})); // new mod.FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 7, .character = 12}},
                     {{2, "(7:12~7:14)"},    // itself
                      {1, "(6:12~6:15)"},    // new mod.FFF($bb:32).value
                      {1, "(7:12~7:15)"}})); // new mod.FFF($bb:11).HHH()
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 2, .position = {.line = 7, .character = 30}}, {{2, "(7:27~7:32)"}, // itself
                                                                      {1, "(6:20~6:25)"}}));

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 8, .character = 11}},
                     {{2, "(8:9~8:12)"},     // itself
                      {1, "(7:20~7:23)"}})); // new mod.FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 2, .position = {.line = 8, .character = 14}},
                     {{2, "(8:13~8:15)"},    // itself
                      {1, "(7:24~7:27)"}})); // new mod.FFF($bb:11).HHH($cc:2)
}

TEST_F(IndexTest, namedImportInlined) {
  TempFileFactory tempFileFactory("arsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB(aa:Int) : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
function EEE() : Int for Int { return $this; }  # invalid method definition
typedef FFF(bb:Int) { var value = 34; }
function HHH(cc:Int) for FFF {}
)");

  fileName =
      tempFileFactory.createTempFile("inlined.ds", format("source %s inlined", fileName.c_str()));

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
$mod.AAA + $mod.BBB($aa:0)
mod CCC
new [mod.DDD]()
34.EEE()  # invalid method call
new mod.FFF($bb:11).value
new mod.FFF($bb:22).HHH($cc:33)
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 1, .symbolSize = 19}));
  ASSERT_EQ(1, modId);

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 13}}, {{modId, "(1:3~1:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 7}}, {{modId, "(1:3~1:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 7}}, {{3, "(2:4~2:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 18}}, {{3, "(3:9~3:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 21}}, {{3, "(3:13~3:15)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 5}}, {{3, "(4:0~4:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 10}}, {{3, "(5:8~5:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 10}}, {{3, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 22}}, {{3, "(7:26~7:31)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 10}}, {{3, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 12}}, {{3, "(7:12~7:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 21}}, {{3, "(8:9~8:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 25}}, {{3, "(8:13~8:15)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 5}},
                     {{modId, "(1:3~1:6)"}, // itself
                      {modId, "(2:0~2:4)"},
                      {modId, "(2:11~2:15)"},
                      {modId, "(3:0~3:3)"},
                      {modId, "(4:5~4:8)"},
                      {modId, "(6:4~6:7)"},
                      {modId, "(7:4~7:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 2, .character = 5}}, {{3, "(2:4~2:7)"}, // itself
                                                                     {3, "(3:36~3:40)"},
                                                                     {1, "(2:5~2:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 3, .character = 10}}, {{3, "(3:9~3:12)"}, // itself
                                                                      {3, "(4:8~4:12)"},
                                                                      {1, "(2:16~2:19)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 3, .character = 14}},
                     {{3, "(3:13~3:15)"},    // itself
                      {1, "(2:20~2:23)"}})); // $mod.BBB($aa:0)
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 4, .character = 2}}, {{3, "(4:0~4:3)"}, // itself
                                                                     {3, "(5:21~5:24)"},
                                                                     {1, "(3:4~3:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 5, .character = 9}}, {{3, "(5:8~5:11)"}, // itself
                                                                     {1, "(4:9~4:12)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 7, .character = 10}},
                     {{3, "(7:8~7:11)"},    // itself
                      {3, "(8:25~8:28)"},   // function HHH() for FFF {}
                      {1, "(6:8~6:11)"},    // new mod.FFF().value
                      {1, "(7:8~7:11)"}})); // new mod.FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 7, .character = 13}},
                     {{3, "(7:12~7:14)"},    // itself
                      {1, "(6:12~6:15)"},    // new mod.FFF($bb:11).value
                      {1, "(7:12~7:15)"}})); // new mod.FFF($bb:22).HHH($cc:33)
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 7, .character = 30}}, {{3, "(7:26~7:31)"}, // itself
                                                                      {1, "(6:20~6:25)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 8, .character = 11}},
                     {{3, "(8:9~8:12)"},     // itself
                      {1, "(7:20~7:23)"}})); // new mod.FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 8, .character = 14}},
                     {{3, "(8:13~8:15)"},    // itself
                      {1, "(7:24~7:27)"}})); // new mod.FFF($bb:22).HHH($cc:33)
}

TEST_F(IndexTest, inlinedImport) {
  TempFileFactory tempFileFactory("arsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
var _AAA = 34
var AAA = $_AAA
function BBB(aa:Int) : Int { return $AAA; }
CCC() { $BBB(); }
typedef DDD = typeof(CCC)
function EEE() : Int for Int { return $this; }    # invalid method definition
typedef FFF(bb:Int) { var value = 34; }
function HHH(cc:Int) for FFF {}
)");

  fileName =
      tempFileFactory.createTempFile("inlined.ds", format("source %s inlined", fileName.c_str()));

  unsigned short modId;
  auto content = format(R"(
source %s
$AAA + $BBB($aa:11)
CCC
new [DDD]()
90.EEE()  # invalid method
new FFF($bb:22).value
new FFF($bb:33).HHH($cc:44)
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 0, .symbolSize = 12}));
  ASSERT_EQ(1, modId);

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 1}}, {{3, "(2:4~2:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 9}}, {{3, "(3:9~3:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 13}}, {{3, "(3:13~3:15)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 1}}, {{3, "(4:0~4:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 7}}, {{3, "(5:8~5:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 6}}, {{3, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 10}}, {{3, "(7:12~7:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 20}}, {{3, "(7:26~7:31)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 6}}, {{3, "(7:8~7:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 18}}, {{3, "(8:9~8:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 22}}, {{3, "(8:13~8:15)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 2, .character = 5}}, {{3, "(2:4~2:7)"},   // itself
                                                                     {3, "(3:36~3:40)"}, //
                                                                     {1, "(2:0~2:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 3, .character = 10}}, {{3, "(3:9~3:12)"}, // itself
                                                                      {3, "(4:8~4:12)"},
                                                                      {1, "(2:7~2:11)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 3, .character = 13}},
                     {{3, "(3:13~3:15)"},    // itself
                      {1, "(2:12~2:15)"}})); // $BBB($aa:11)
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 4, .character = 2}}, {{3, "(4:0~4:3)"}, // itself
                                                                     {3, "(5:21~5:24)"},
                                                                     {1, "(3:0~3:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 5, .character = 9}}, {{3, "(5:8~5:11)"},   // itself
                                                                     {1, "(4:5~4:8)"}})); //
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 7, .character = 10}},
                     {{3, "(7:8~7:11)"},   // itself
                      {3, "(8:25~8:28)"},  // function HHH() for FFF {}
                      {1, "(6:4~6:7)"},    // new mod.FFF().value
                      {1, "(7:4~7:7)"}})); // new mod.FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 7, .character = 13}},
                     {{3, "(7:12~7:14)"},   // itself
                      {1, "(6:8~6:11)"},    // new mod.FFF($bb:22).value
                      {1, "(7:8~7:11)"}})); // new mod.FFF($bb:33).HHH($cc:44)
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = 3, .position = {.line = 7, .character = 28}}, {{3, "(7:26~7:31)"}, // itself
                                                                      {1, "(6:16~6:21)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 8, .character = 11}},
                     {{3, "(8:9~8:12)"},     // itself
                      {1, "(7:16~7:19)"}})); // new FFF().HHH()
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = 3, .position = {.line = 8, .character = 14}},
                     {{3, "(8:13~8:15)"},    // itself
                      {1, "(7:20~7:23)"}})); // new mod.FFF($bb:33).HHH($cc:44)
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
      let error = $e; $error;
      import-env HOME
    }
    var a = new AAA(new APIError("hello"))
    $a.error
    $a.HOME
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 6, .symbolSize = 16}));

  // definition
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 6, .character = 27}}, // APIError
                     {{modId, "(1:12~1:20)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 6, .character = 18}}, // AAA
                     {{modId, "(2:12~2:15)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 3, .character = 19}}, // $e
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
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 17}}, // $e
                     {{modId, "(2:16~2:18)"},                                           // itself
                      {modId, "(3:18~3:20)"}})); // let error = $e
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId,
              .position = {.line = 2, .character = 14}}, // typedef AAA($e : APIError)
      {{modId, "(2:12~2:15)"},                           // itself
       {modId, "(6:16~6:19)"}}));                        // var a = new AAA(new APIError("hello"))
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 3, .character = 11}}, // let error = $e
      {{modId, "(3:10~3:15)"},                                           // itself
       {modId, "(3:22~3:28)"},                                           // $error
       {modId, "(7:7~7:12)"}}));                                         // $a.error
}

TEST_F(IndexTest, udtypeRec) {
  unsigned short modId;
  const char *content = R"E(
    typedef AAA($n : AAA?) {
      let next = $n
      typedef Next = Option<typeof(new AAA(new AAA?()))>
      23 is Next
    }
    new AAA(new AAA.Next()).next
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 4, .symbolSize = 13}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 49}}, // new AAA?()
      {{modId, "(1:12~1:15)"}}));                                        // typedef AAA($n : AAA?)
  ASSERT_NO_FATAL_FAILURE(
      this->findDecl(Request{.modId = modId, .position = {.line = 6, .character = 23}}, // AAA.Next
                     {{modId, "(3:14~3:18)"}})); // typedef Next

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 14}}, // typedef AAA($n : AAA?)
      {{modId, "(1:12~1:15)"},                                           // itself
       {modId, "(1:21~1:24)"},                                           // $n : AAA?
       {modId, "(3:39~3:42)"},                                           // new AAA(new AAA?())
       {modId, "(3:47~3:50)"},                                           // new AAA?()
       {modId, "(6:8~6:11)"},     //     new AAA(new AAA.Next()).next
       {modId, "(6:16~6:19)"}})); //     new AAA.Next()
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId,
              .position = {.line = 3,
                           .character = 17}}, // typedef Next = Option<typeof(new AAA(new AAA?()))>
      {{modId, "(3:14~3:18)"},                // itself
       {modId, "(4:12~4:16)"},                // 23 is Next
       {modId, "(6:20~6:24)"}}));             //     new AAA(new AAA.Next()).next
}

TEST_F(IndexTest, method) {
  unsigned short modId;
  const char *content = R"E(typedef BigInt(a : Int) { var v = $a; }
    function factorial() : Int for BigInt {
        return $this.v == 0 ? 1 : $this.v * new BigInt($this.v - 1).factorial()
    }
    new BigInt(23).factorial()
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 18}));

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

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 5, .symbolSize = 17}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 18}}, // new INT().print()
      {{modId, "(2:13~2:18)"}})); // function print() for INT {}
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId,
              .position = {.line = 4, .character = 26}}, // (new INT() as ANY).print()
      {{modId, "(1:13~1:18)"}}));                        // function print() for ANY {}
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 18}}, // new STR().print()
      {{modId, "(1:13~1:18)"}})); // function print() for INT {}

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 17}}, // function print() for ANY
      {{modId, "(1:13~1:18)"},                                           // itself
       {modId, "(4:23~4:28)"},    // (new INT() as ANY).print()
       {modId, "(5:14~5:19)"}})); // new STR().print()
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 2, .character = 17}}, // function print() for INT
      {{modId, "(2:13~2:18)"},                                           // itself
       {modId, "(3:14~3:19)"}}));                                        // new INT().print()
}

TEST_F(IndexTest, builtinSymbol) {
  unsigned short modId;
  const char *content = R"E(
'aaa'.chars()
($true as String).
chars() +
$OSTYPE
$@.shift()
$@.copy().shift()
(23,)._0
(231,)._0
$MODULE._fullname('aa')
_exit
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 15}));

  unsigned short modId2;
  content = R"(
$OSTYPE.
chars() is
String
['34'].shift()
(231,)._0
$MODULE._fullname('')
 _exit
  )";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId2, {.declSize = 1, .symbolSize = 8}));

  // reference
  // builtin variable
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 4, .character = 3}},
                     {{modId, "(4:0~4:7)"},     // $OSTYPE
                      {modId2, "(1:0~1:7)"}})); // $OSTYPE
  // builtin type
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId2, .position = {.line = 3, .character = 3}},
                     {{modId, "(2:10~2:16)"},   // ($true as String).
                      {modId2, "(3:0~3:6)"}})); // String
  // builtin method
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 8}},
                     {{modId, "(1:6~1:11)"},    // 'aaa'.chars()
                      {modId, "(3:0~3:5)"},     // chars()
                      {modId2, "(2:0~2:5)"}})); // chars() is
  // generic method
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 6, .character = 14}},
                     {{modId, "(5:3~5:8)"},      // $@.shift()
                      {modId, "(6:10~6:15)"},    // $@.copy().shift()
                      {modId2, "(4:7~4:12)"}})); // ['34'].shift()
  // tuple field (not lookup foreign module)
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 7, .character = 7}},
                     {{modId, "(7:6~7:8)"},    // (23,)._0
                      {modId, "(8:7~8:9)"}})); // (231,)._0
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId2, .position = {.line = 5, .character = 7}},
                     {{modId2, "(5:7~5:9)"}})); // (231,)._0
  // private builtin method
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId2, .position = {.line = 6, .character = 14}},
                     {{modId, "(9:8~9:17)"},     // $MODULE._fullname('aa')
                      {modId2, "(6:8~6:17)"}})); // $MODULE._fullname('')
  // private builtin command (__puts, _exit)
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 10, .character = 3}},
                     {{modId, "(10:0~10:5)"},   // _exit
                      {modId2, "(7:1~7:6)"}})); //  _exit
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

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 13}}, {{modId, "(1:8~1:13)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 6, .character = 13}}, {{modId, "(5:12~5:17)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 8, .character = 11}}, {{modId, "(1:8~1:13)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 10, .character = 9}}, {{modId, "(1:8~1:13)"}}));

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

TEST_F(IndexTest, udcParam) {
  unsigned short modId;
  const char *content = R"E(
[<CLI>]
typedef Param() {}
fff(pp : Param) {
  echo $pp
}
fff
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 3, .symbolSize = 7}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl( // typedef Param()
      Request{.modId = modId, .position = {.line = 3, .character = 13}}, {{modId, "(2:8~2:13)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl( // pp : Param
      Request{.modId = modId, .position = {.line = 4, .character = 9}}, {{modId, "(3:4~3:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl( // fff(pp : Param)
      Request{.modId = modId, .position = {.line = 6, .character = 1}}, {{modId, "(3:0~3:3)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 2, .character = 11}}, // typedef Param()
      {{modId, "(2:8~2:13)"},                                            // itself
       {modId, "(3:9~3:14)"}}));                                         // pp : Param

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 3, .character = 5}}, // pp
                     {{modId, "(3:4~3:6)"},     // pp : Param (itself)
                      {modId, "(4:7~4:10)"}})); // echo $pp

  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 3, .character = 2}}, // fff(pp : Param)
      {{modId, "(3:0~3:3)"},                                            // itself
       {modId, "(6:0~6:3)"}}));                                         // fff
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
      Request{.modId = modId, .position = {.line = 1, .character = 13}}, {{modId, "(1:11~1:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 7}}, {{modId, "(4:11~4:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 12}}, {{modId, "(4:11~4:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 5}}, {{modId, "(1:11~1:14)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 13}}, // function AAA()
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
      Request{.modId = modId, .position = {.line = 1, .character = 13}}, {{modId, "(1:11~1:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 2, .character = 12}}, {{modId, "(4:11~4:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 4, .character = 12}}, {{modId, "(4:11~4:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 12}}, {{modId, "(1:11~1:14)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 1, .character = 13}}, // function AAA()
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
      Request{.modId = modId, .position = {.line = 6, .character = 6}}, {{modId, "(1:2~1:5)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 4}}, // AAA()
                     {{modId, "(1:2~1:5)"},                                            // itself
                      {modId, "(6:4~6:7)"}}));

  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 5, .character = 3}}, // BBB()
                     {{modId, "(5:2~5:5)"},                                            // itself
                      {modId, "(2:4~2:7)"}}));
}

TEST_F(IndexTest, backrefNamedArg) {
  unsigned short modId;
  const char *content = R"E(
  type AAA {
    let begin: Int
    let end: Int
    var next: AAA?
  }
  function append(begin:Int, end:Int) for AAA {
    $this.next = new AAA($begin:$begin, $end:$double($v:$end))
  }
  function double($v:Int): Int { return $v*2; }
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 10, .symbolSize = 27}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 7, .character = 49}}, {{modId, "(9:11~9:17)"}}));

  // reference
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 7, .character = 48}},
                     {{modId, "(9:11~9:17)"},    // itself
                      {modId, "(7:45~7:52)"}})); // $double($v:$end)
}

TEST_F(IndexTest, hereDoc) {
  unsigned short modId;
  const char *content = R"E(
  cat << EOF & cat << 'EOF'
this is a pen1
EOF
this is a pen2
EOF
)E";

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 2, .symbolSize = 4}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 11}}, {{modId, "(1:9~1:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 1, .character = 26}}, {{modId, "(1:22~1:27)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 0}}, {{modId, "(1:9~1:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 5, .character = 2}}, {{modId, "(1:22~1:27)"}}));

  // references
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 11}},
                     {{modId, "(1:9~1:12)"}, {modId, "(3:0~3:3)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 25}},
                     {{modId, "(1:22~1:27)"}, {modId, "(5:0~5:3)"}}));
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
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 3}));

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
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 1, .symbolSize = 4}));

  // definition
  ASSERT_NO_FATAL_FAILURE(this->findDecl(
      Request{.modId = modId, .position = {.line = 3, .character = 8}}, std::vector<Loc>()));

  // references
  ASSERT_NO_FATAL_FAILURE(this->findRefs(
      Request{.modId = modId, .position = {.line = 2, .character = 12}}, std::vector<Loc>()));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 1, .character = 4}}, // var b
                     {{modId, "(1:4~1:5)"},                                            // itself
                      {modId, "(3:0~3:2)"}}));                                         // $b
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 19}}, // Nothing
                     {{modId, "(2:19~2:26)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->findRefs(Request{.modId = modId, .position = {.line = 2, .character = 32}},
                     {{modId, "(2:30~2:33)"}})); // Int
}

TEST_F(IndexTest, invalidUdc) {
  unsigned short modId;
  const char *content = R"(hoge() {}
hoge() { call echo hello; }  # already defined
hoge call 34
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
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 3, .symbolSize = 6}));
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
  ASSERT_NO_FATAL_FAILURE(this->hover("let A = 34\n$A", 1, "```arsh\nlet A: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("import-env HOME\n$HOME", 1, "```arsh\nimportenv HOME: String\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("export-env ZZZ = 'hoge'\n$ZZZ", 1, "```arsh\nexportenv ZZZ: String\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("function hoge($s : Int) {}\n$hoge", 1,
                                      "```arsh\nfunction hoge(s: Int): Void\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("function hoge($ss : Int) {\n$ss; }", 1, "```arsh\nvar ss: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("function hoge($_sss : Int) {}\n$hoge(\n$_sss: 34)", 2,
                                      "```arsh\nvar _sss: Int\n```"));

  // user-defined command
  ASSERT_NO_FATAL_FAILURE(this->hover("hoge(){}\nhoge", 1, "```arsh\nhoge(): Bool\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("usage() : Nothing { throw 34; }\nusage", 1, "```arsh\nusage(): Nothing\n```"));

  // user-defined error type
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef App : OutOfRangeError\n34 is\nApp", 2,
                                      "```arsh\ntype App: OutOfRangeError\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("type AppError : Error; type API : AppError\n34 is\nAPI", 2,
                                      "```arsh\ntype API: AppError\n```"));

  // user-defined type with explicit constructor
  ASSERT_NO_FATAL_FAILURE(
      this->hover("typedef Interval() { var begin = 34; }; var a = new Interval();\n$a",
                  Position{.line = 1, .character = 0}, "```arsh\nvar a: Interval\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef Interval(s : Int) { var n = $s; let value = new "
                                      "Interval?(); }\nvar a = new Interval();",
                                      Position{.line = 1, .character = 15}, R"(```arsh
type Interval(s: Int) {
    var n: Int
    let value: Interval?
}
```)"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA() { var begin = 34; }\nnew AAA().\nbegin",
                                      Position{.line = 2, .character = 1},
                                      "```arsh\nvar begin: Int for AAA\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA() { var begin = 34; \n$begin;}",
                                      Position{.line = 1, .character = 3},
                                      "```arsh\nvar begin: Int for AAA\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA() { typedef Type = Int; }\n23 is AAA.\nType",
                                      Position{.line = 2, .character = 3},
                                      "```arsh\ntype Type = Int for AAA\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA() { typedef Type = Int; 34 is \nType; }",
                                      Position{.line = 1, .character = 3},
                                      "```arsh\ntype Type = Int for AAA\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("typedef AAA($_sss : Int) {\n$_sss; }", 1, "```arsh\nvar _sss: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA($_sss : Int) {};\nnew AAA(\n$_sss: 111)", 2,
                                      "```arsh\nvar _sss: Int\n```"));

  // user-defined type with implicit constructor
  ASSERT_NO_FATAL_FAILURE(this->hover("type Interval { var n : Int; let next : Interval?; "
                                      "}\nvar aaaa = new Interval(2, $none);",
                                      Position{.line = 1, .character = 20}, R"(```arsh
type Interval(n: Int, next: Interval?) {
    var n: Int
    let next: Interval?
}
```)"));

  ASSERT_NO_FATAL_FAILURE(this->hover(
      "typedef Interval { var value : Interval?; }; var a = new Interval($none);\n$a.value",
      Position{.line = 1, .character = 3}, "```arsh\nvar value: Interval? for Interval\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover(
      "typedef Interval() { var value = new Interval?(); }; var aaa = new [[Interval]]();\n$aaa",
      Position{.line = 1, .character = 2}, "```arsh\nvar aaa: [[Interval]]\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("type Interval { var n:Int; let next: Interval?; }\n"
                                      "new Interval(\n$next: $None, $n: 1234)",
                                      Position{.line = 2, .character = 2},
                                      "```arsh\nlet next: Interval? for Interval\n```"));

  // user-defined method
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef INT(a : Int) { var v = $a; }\n"
                                      "function value():Int for INT { return $this.v; }\n"
                                      "new INT(12).value()",
                                      Position{.line = 2, .character = 13},
                                      "```arsh\nfunction value(): Int for INT\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("function value():Int for String { \nreturn $this.size(); }",
                                      Position{.line = 1, .character = 8},
                                      "```arsh\nlet this: String\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AppError : Error; \n"
                                      "function size(aaa: Int): Int for AppError { return\n$aaa; }",
                                      2, "```arsh\nvar aaa: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("typedef AppError : Error; \n"
                  "function size(aaa: Int?, $bbb: Int?): Int for AppError { return $aaa!; }\n"
                  "new AppError('').size(\n$bbb:333)",
                  3, "```arsh\nvar bbb: Int?\n```"));

  // here doc
  ASSERT_NO_FATAL_FAILURE(this->hover(R"(cat << EOF
this is a pennn""
EOF)",
                                      Position{.line = 0, .character = 9},
                                      "```md\nhere document start word\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover(R"(cat << EOF
this is a pennn""
EOF)",
                                      Position{.line = 2, .character = 1},
                                      "```md\nhere document start word\n```"));
}

TEST_F(IndexTest, hoverBuiltin) {
  // builtin type
  ASSERT_NO_FATAL_FAILURE(this->hover("34 is\nInt", 1, ""));
  ASSERT_NO_FATAL_FAILURE(this->hover("34 is\nError", 1, ""));
  ASSERT_NO_FATAL_FAILURE(this->hover("34 is\nShellExit", 1, ""));
  ASSERT_NO_FATAL_FAILURE(this->hover("34 is\nAssertionFailed", 1, ""));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("34 is\nArithmeticError", 1, "```arsh\ntype ArithmeticError: Error\n```"));

  // builtin variable or type alias
  ASSERT_NO_FATAL_FAILURE(this->hover("$?", 0, "```arsh\nvar ?: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("hoge() { \n$@;}", 1, "```arsh\nlet @: [String]\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$VERSION", 0,
                                      "```arsh\nconst VERSION = '" X_INFO_VERSION_CORE "'"
                                      "\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$true is\nBoolean", 1, "```arsh\ntype Boolean = Bool\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("$SCRIPT_NAME", 0, "```arsh\nconst SCRIPT_NAME = '/dummy_10'\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$SCRIPT_DIR", 0, "```arsh\nconst SCRIPT_DIR = '/'\n```"));

  // builtin command
  ASSERT_NO_FATAL_FAILURE(
      this->hover(":", 0,
                  "```md\n"
                  ":: : \n"
                  "    Null command.  Always success (exit status is 0).\n```"));

  // builtin tuple or method
  ASSERT_NO_FATAL_FAILURE(this->hover("var a = (34, $false, '');$a._2\n$a._2",
                                      Position{.line = 1, .character = 3},
                                      "```arsh\nvar _2: String for (Int, Bool, String)\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("''.size();[1].size()\n[0].size()",
                                      Position{.line = 1, .character = 5},
                                      "```arsh\nfunction size(): Int for [Int]\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("''.slice(0)", Position{.line = 0, .character = 5},
                  "```arsh\nfunction slice(start: Int, end: Int?): String for String\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("[23:(12,(34,56,))].\nclear()", Position{.line = 1, .character = 2},
                  "```arsh\nfunction clear(): Void for [Int : (Int, (Int, Int))]\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("'2345'.slice(\n$end:2345)", 1, "```arsh\nvar end: Int?\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("[1234].slice(\n$start:2345)", 1, "```arsh\nvar start: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("[1234].slice(\n$end:345,$start:2345)", 1, "```arsh\nvar end: Int?\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("['23':1234].remove(\n$key:'2')", 1, "```arsh\nvar key: String\n```"));
}

TEST_F(IndexTest, hoverMod) {
  // source
  TempFileFactory tempFileFactory("arsh_index");
  auto fileName = tempFileFactory.createTempFile(X_INFO_VERSION_CORE "_.ds",
                                                 R"(
var AAA = 'hello'
)");
  std::string src = "source ";
  src += fileName;
  src += " as mod\n$mod";
  ASSERT_NO_FATAL_FAILURE(
      this->hover(src.c_str(), 1, format("```arsh\nsource %s as mod\n```", fileName.c_str())));

  src = "source ";
  src += tempFileFactory.getTempDirName();
  src += "/";
  int chars = static_cast<int>(src.size()) + 5;
  src += "${VERSION}_.ds";
  ASSERT_NO_FATAL_FAILURE(this->hover(src.c_str(), Position{.line = 0, .character = chars},
                                      "```arsh\nconst VERSION = '" X_INFO_VERSION_CORE "'\n```"));
}

TEST_F(IndexTest, hoverConst) {
  ASSERT_NO_FATAL_FAILURE(this->hover("$TRUE", 0, "```arsh\nconst TRUE = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$True", 0, "```arsh\nconst True = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$true", 0, "```arsh\nconst true = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$FALSE", 0, "```arsh\nconst FALSE = false\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$False", 0, "```arsh\nconst False = false\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$false", 0, "```arsh\nconst false = false\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("$SIGHUP", 0, "```arsh\nconst SIGHUP = signal(1)\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$SIGKILL", 0, "```arsh\nconst SIGKILL = signal(9)\n```"));
}

TEST_F(IndexTest, hoverUsage1) {
  const char *src = R"([<CLI>]
typedef AAA() {
  [<Flag(short: "s", long: "status", help: "dump internal status")>]
  var s = $false

  [<Option(help: "specify output target", opt: $true)>]
  var output : String?

  [<Arg(required: $true)>]
  var files : [String]

  [<Arg()>]  # error
  var dest : String?
}
new AAA()
)";

  const char *out = R"(```arsh
type AAA() {
    var %name: String
    var s: Bool
    var output: String?
    var files: [String]
    var dest: String?
}
```

**command line**
```md
Usage:  [OPTIONS] FILES...

Options:
  -s, --status       dump internal status
  --output[=OUTPUT]  specify output target
  -h, --help         show this help message
```)";
  ASSERT_NO_FATAL_FAILURE(this->hover(src, {14, 6}, out));
}

TEST_F(IndexTest, hoverUsage2) {
  const char *src = R"([<CLI()>]
typedef Param() {
  [<Flag(short: "s", long: "status", help: "dump internal status")>]
  var s = $false

  [<Option(help: "specify output target", opt: $true, short: 'o', long: 'output')>]
  var output : String?

  [<Arg(required: $true)>]
  var files : [String]

  [<Arg()>]  # error
  var dest : String?
}

fff($p : Param) { echo $p; }
fff
)";

  const char *out = R"(```arsh
fff(): Bool
```

**command line**
```md
Usage: fff [OPTIONS] FILES...

Options:
  -s, --status                   dump internal status
  -o[OUTPUT], --output[=OUTPUT]  specify output target
  -h, --help                     show this help message
```)";
  ASSERT_NO_FATAL_FAILURE(this->hover(src, {16, 2}, out));
}

TEST_F(IndexTest, hoverUsage3) {
  const char *src = R"([<CLI(toplevel: $true)>]
type Param() {}
fff($p : Param) { echo $p; }
fff
)";

  const char *out = R"(```arsh
fff(): Bool
```

**command line**
```md
Usage: <$ARG0>

Options:
  -h, --help  show this help message
```)";
  ASSERT_NO_FATAL_FAILURE(this->hover(src, {3, 2}, out));
}

TEST_F(IndexTest, docSymbol1) {
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::VAR));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::LET));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::IMPORT_ENV));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::EXPORT_ENV));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::MOD));
  ASSERT_EQ(SymbolKind::Field, toSymbolKind(DeclSymbol::Kind::VAR, DeclSymbol::Attr::MEMBER));
  ASSERT_EQ(SymbolKind::Field, toSymbolKind(DeclSymbol::Kind::LET, DeclSymbol::Attr::MEMBER));
  ASSERT_EQ(SymbolKind::Field,
            toSymbolKind(DeclSymbol::Kind::IMPORT_ENV, DeclSymbol::Attr::MEMBER));
  ASSERT_EQ(SymbolKind::Field,
            toSymbolKind(DeclSymbol::Kind::EXPORT_ENV, DeclSymbol::Attr::MEMBER));
  ASSERT_EQ(SymbolKind::Constant, toSymbolKind(DeclSymbol::Kind::CONST));
  ASSERT_EQ(SymbolKind::Constant, toSymbolKind(DeclSymbol::Kind::MOD_CONST));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::FUNC));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::CMD));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::BUILTIN_CMD));
  ASSERT_EQ(SymbolKind::Method, toSymbolKind(DeclSymbol::Kind::METHOD));
  ASSERT_EQ(SymbolKind::Constructor, toSymbolKind(DeclSymbol::Kind::CONSTRUCTOR));
  ASSERT_EQ(SymbolKind::Class, toSymbolKind(DeclSymbol::Kind::TYPE_ALIAS));
  ASSERT_EQ(SymbolKind::Class, toSymbolKind(DeclSymbol::Kind::ERROR_TYPE_DEF));
  ASSERT_EQ(SymbolKind::String, toSymbolKind(DeclSymbol::Kind::HERE_START)); // normally unused
}

TEST_F(IndexTest, docSymbol2) {
  unsigned short modId;
  const char *content = R"(
importenv ZZZ : $HOME
function ggg(aaa:Int) {
  var bbb = 2345; return $aaa + $bbb
}
type Interval {
  let begin: Int
  let end: Int
 }
function dist(): Int for Interval {
  return $this.end - $this.begin
}
error(): Nothing {
  var msg = $@.join('');
  throw new Error("$msg")
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 11, .symbolSize = 27}));
  auto src = this->srcMan.findById(ModId{modId});
  ASSERT_TRUE(src);
  auto values = generateDocumentSymbols(this->indexes, *src);
  ASSERT_EQ(5, values.size());

  ASSERT_EQ("ZZZ", values[0].name);
  ASSERT_EQ(SymbolKind::Variable, values[0].kind);
  ASSERT_EQ("(1:10~1:13)", values[0].selectionRange.toString());
  ASSERT_EQ("(1:0~1:21)", values[0].range.toString());

  ASSERT_EQ("ggg", values[1].name);
  ASSERT_EQ(SymbolKind::Function, values[1].kind);
  ASSERT_EQ("(2:9~2:12)", values[1].selectionRange.toString());
  ASSERT_EQ("(2:0~4:1)", values[1].range.toString());
  ASSERT_TRUE(values[1].children.hasValue());
  ASSERT_EQ(2, values[1].children.unwrap().size());
  ASSERT_EQ("aaa", values[1].children.unwrap()[0].name);
  ASSERT_EQ(SymbolKind::Variable, values[1].children.unwrap()[0].kind);
  ASSERT_EQ("bbb", values[1].children.unwrap()[1].name);
  ASSERT_EQ(SymbolKind::Variable, values[1].children.unwrap()[1].kind);

  ASSERT_EQ("Interval", values[2].name);
  ASSERT_EQ(SymbolKind::Constructor, values[2].kind);
  ASSERT_EQ("(5:5~5:13)", values[2].selectionRange.toString());
  ASSERT_EQ("(5:0~8:2)", values[2].range.toString());
  ASSERT_TRUE(values[2].children.hasValue());
  ASSERT_EQ(2, values[2].children.unwrap().size());
  ASSERT_EQ("begin", values[2].children.unwrap()[0].name);
  ASSERT_EQ(SymbolKind::Field, values[2].children.unwrap()[0].kind);
  ASSERT_EQ("end", values[2].children.unwrap()[1].name);
  ASSERT_EQ(SymbolKind::Field, values[2].children.unwrap()[1].kind);

  ASSERT_EQ("dist", values[3].name);
  ASSERT_EQ(SymbolKind::Method, values[3].kind);
  ASSERT_EQ("(9:9~9:13)", values[3].selectionRange.toString());
  ASSERT_EQ("(9:0~11:1)", values[3].range.toString());
  ASSERT_TRUE(values[3].children.hasValue());
  ASSERT_EQ(0, values[3].children.unwrap().size());

  ASSERT_EQ("error", values[4].name);
  ASSERT_EQ(SymbolKind::Function, values[4].kind);
  ASSERT_EQ("(12:0~12:5)", values[4].selectionRange.toString());
  ASSERT_EQ("(12:0~15:1)", values[4].range.toString());
  ASSERT_TRUE(values[4].children.hasValue());
  ASSERT_EQ(1, values[4].children.unwrap().size());
  ASSERT_EQ("msg", values[4].children.unwrap()[0].name);
  ASSERT_EQ(SymbolKind::Variable, values[4].children.unwrap()[0].kind);
}

static std::string resolvePackedParamType(const Type &type) {
  if (isa<ArrayType>(type)) {
    return cast<ArrayType>(type).getElementType().getNameRef().toString();
  } else if (isa<MapType>(type)) {
    auto &mapType = cast<MapType>(type);
    return mapType.getKeyType().getNameRef().toString() + ";" +
           mapType.getValueType().getNameRef().toString();
  } else {
    return "";
  }
}

TEST_F(IndexTest, signature) {
  TypePool pool;

  std::vector<TypePool::Key> keys;
  for (auto &e : pool.getMethodMap()) {
    keys.push_back(e.first);
  }

  for (auto &key : keys) {
    auto &type = pool.get(key.id);
    SCOPED_TRACE("key(" + key.ref.toString() + ", " + type.getName() + ")");
    auto iter = pool.getMethodMap().find(key);
    ASSERT_TRUE(iter != pool.getMethodMap().end());
    unsigned int index = iter->second ? iter->second.handle()->getIndex() : iter->second.index();
    auto handle = pool.allocNativeMethodHandle(type, index);
    ASSERT_TRUE(handle);
    std::string expect = "function ";
    expect += key.ref;
    formatMethodSignature(type, *handle, expect, key.ref == OP_INIT);

    std::string actual = "function ";
    actual += key.ref;
    formatNativeMethodSignature(&nativeFuncInfoTable()[index], resolvePackedParamType(type),
                                actual);

    ASSERT_EQ(expect, actual);
  }
}

TEST_F(IndexTest, inheritance) {
  unsigned short modId;
  const char *content = R"(
typedef APIError: ArithmeticError
typedef APIError2 : APIError
typedef AAA() {}
[<CLI>]
typedef BBB() {}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 4, .symbolSize = 6}));

  const auto builtinIndex = this->indexes.find(BUILTIN_MOD_ID);
  ASSERT_TRUE(builtinIndex);
  const auto index = this->indexes.find(static_cast<ModId>(modId));
  ASSERT_TRUE(index);

  {
    auto *type = index->findBaseType(toQualifiedTypeName("APIError2", static_cast<ModId>(modId)));
    ASSERT_TRUE(type);
    ASSERT_EQ(toQualifiedTypeName("APIError", static_cast<ModId>(modId)), type->getValue());
    ASSERT_EQ(static_cast<ModId>(modId), type->resolveBelongedModId());

    type = index->findBaseType(toQualifiedTypeName("APIError", static_cast<ModId>(modId)));
    ASSERT_TRUE(type);
    ASSERT_EQ("ArithmeticError", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("ArithmeticError");
    ASSERT_TRUE(type);
    ASSERT_EQ("Error", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("Error");
    ASSERT_TRUE(type);
    ASSERT_EQ("Throwable", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("Throwable");
    ASSERT_FALSE(type);
  }

  {
    auto *type = index->findBaseType(toQualifiedTypeName("AAA", static_cast<ModId>(modId)));
    ASSERT_FALSE(type);
  }

  {
    auto *type = index->findBaseType(toQualifiedTypeName("BBB", static_cast<ModId>(modId)));
    ASSERT_TRUE(type);
    ASSERT_EQ("CLI", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("CLI");
    ASSERT_FALSE(type);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}