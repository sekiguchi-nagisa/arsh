
#include "gtest/gtest.h"

#include "analyzer.h"
#include "indexer.h"
#include "rename.h"

#include "../test_common.h"

using namespace arsh::lsp;

struct Request {
  unsigned short modId;
  int line;
  int character;

  arsh::ModId getModId() const { return static_cast<arsh::ModId>(this->modId); }

  Position toPosition() const {
    return {
        .line = this->line,
        .character = this->character,
    };
  }
};

struct Result {
  arsh::ModId modId;
  std::string rangeStr;
  std::string newName;

  Result(arsh::ModId modId, std::string &&rangeStr, std::string &&newName)
      : modId(modId), rangeStr(rangeStr), newName(newName) {}

  Result(unsigned short modId, std::string &&rangeStr, std::string &&newName)
      : Result(static_cast<arsh::ModId>(modId), std::move(rangeStr), std::move(newName)) {}

  Result(unsigned short modId, std::string &&rangeStr) : Result(modId, std::move(rangeStr), "") {}
};

struct Conflict {
  arsh::ModId modId;
  std::string rangeStr;

  Conflict(arsh::ModId modId, std::string &&rangeStr)
      : modId(modId), rangeStr(std::move(rangeStr)) {}

  Conflict(unsigned short modId, std::string &&rangeStr)
      : Conflict(static_cast<arsh::ModId>(modId), std::move(rangeStr)) {}
};

class RenameTest : public ::testing::Test {
private:
  arsh::SysConfig sysConfig;
  SourceManager srcMan;
  ModuleArchives archives;
  SymbolIndexes indexes;
  unsigned int idCount{0};

public:
  void doAnalyze(const char *content, unsigned short modId) {
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
    ASSERT_EQ(modId, arsh::toUnderlying(ret->getModId()));
  }

  void rename(Request req, const char *newName, RenameValidationStatus status) {
    this->renameImpl(req, newName, status, nullptr);
  }

  void renameWithConflict(Request req, const char *newName, Conflict &&expect) {
    arsh::Optional<Conflict> actual;
    this->renameImpl(req, newName, RenameValidationStatus::NAME_CONFLICT,
                     [&](const DeclSymbol &, const RenameResult &ret) {
                       ASSERT_FALSE(ret);
                       auto &conflict = ret.asErr();
                       auto src = this->srcMan.findById(conflict.symbol.getModId());
                       ASSERT_TRUE(src);
                       auto range = src->toRange(conflict.symbol.getToken());
                       ASSERT_TRUE(range.hasValue());
                       actual = Conflict(conflict.symbol.getModId(), range.unwrap().toString());
                     });
    ASSERT_TRUE(actual.hasValue());
    ASSERT_EQ(expect.modId, actual.unwrap().modId);
    ASSERT_EQ(expect.rangeStr, actual.unwrap().rangeStr);
  }

  void rename(Request req, const char *newName, std::vector<Result> &&expect) {
    for (auto &e : expect) {
      if (e.newName.empty()) {
        e.newName = newName;
      }
    }

    std::vector<Result> actual;
    this->renameImpl(req, newName, RenameValidationStatus::CAN_RENAME,
                     [&](const DeclSymbol &, const RenameResult &ret) {
                       ASSERT_TRUE(ret);
                       auto &target = ret.asOk();
                       auto edit = target.toTextEdit(this->srcMan);
                       actual.emplace_back(target.symbol.getModId(), edit.range.toString(),
                                           std::move(edit.newText));
                     });
    ASSERT_EQ(expect.size(), actual.size());
    size_t size = expect.size();
    for (size_t i = 0; i < size; i++) {
      SCOPED_TRACE("at " + std::to_string(i));
      ASSERT_EQ(expect[i].newName, actual[i].newName);
      ASSERT_EQ(expect[i].modId, actual[i].modId);
      ASSERT_EQ(expect[i].rangeStr, actual[i].rangeStr);
    }
  }

private:
  void renameImpl(Request req, const char *newName, RenameValidationStatus status,
                  const ValidateRenameConsumer &consumer) {
    auto src = this->srcMan.findById(req.getModId());
    ASSERT_TRUE(src);
    auto pos = toTokenPos(src->getContent(), req.toPosition());
    ASSERT_TRUE(pos.hasValue());

    SymbolRequest sr = {
        .modId = req.getModId(),
        .pos = pos.unwrap(),
    };

    auto s = validateRename(this->indexes, sr, newName, consumer);
    ASSERT_EQ(status, s);
  }
};

TEST_F(RenameTest, invalid1) {
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze("12342341", 1));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 0, .character = 2}, "var",
                                       RenameValidationStatus::INVALID_SYMBOL));

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze("var aaa = 34", 2));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 0, .character = 5}, "123",
                                       RenameValidationStatus::INVALID_NAME));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 0, .character = 5}, "aaa",
                                       RenameValidationStatus::DO_NOTHING));

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze("$OSTYPE\n$?", 3));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 0, .character = 2}, "var",
                                       RenameValidationStatus::BUILTIN));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 1, .character = 1}, "var",
                                       RenameValidationStatus::BUILTIN));

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze("AAA() {}", 4));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 4, .line = 0, .character = 1}, "+aaa\x01",
                                       RenameValidationStatus::INVALID_NAME));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 4, .line = 0, .character = 1}, "AAA",
                                       RenameValidationStatus::DO_NOTHING));

  const char *content = R"(
typedef AAA() {}
function get() : AAA for AAA {
  return \
$this
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 5));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 5, .line = 4, .character = 2}, "var",
                                       RenameValidationStatus::BUILTIN));
}

TEST_F(RenameTest, invalid2) {
  const char *content = R"(echo
cat << EOF|echo|echo
this is a comment
EOF
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 7}, "var",
                                       RenameValidationStatus::INVALID_SYMBOL));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 8}, "var",
                                       RenameValidationStatus::INVALID_SYMBOL));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 9}, "var",
                                       RenameValidationStatus::INVALID_SYMBOL));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 10}, "var",
                                       RenameValidationStatus::INVALID_SYMBOL));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 11}, "var",
                                       RenameValidationStatus::BUILTIN));
}

TEST_F(RenameTest, invalid3) {
  const char *content = R"(
123.toFloat()
(34,)._0
echo $0
echo ${11}
23 is Int
[34].size()
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 5}, "var",
                                       RenameValidationStatus::BUILTIN));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 6}, "_var",
                                       RenameValidationStatus::BUILTIN));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 3, .character = 5}, "var",
                                       RenameValidationStatus::BUILTIN));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 4, .character = 7}, "var",
                                       RenameValidationStatus::INVALID_SYMBOL));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 6}, "Int",
                                       RenameValidationStatus::BUILTIN));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 6, .character = 8}, "size22",
                                       RenameValidationStatus::BUILTIN));
}

TEST_F(RenameTest, global1) {
  // rename global without conflict
  const char *content = R"({ var var = 34; }
var aaaa = 1234;
echo $aaaa;
$aaaa++
{ "${aaaa}" }
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 1, .character = 4}, "var",
                   {{1, "(1:4~1:8)"}, {1, "(2:6~2:10)"}, {1, "(3:1~3:5)"}, {1, "(4:5~4:9)"}}));

  // rename global with conflict
  content = R"(
var AAA = 34;
var bbb = ""
echo ${bbb}
{ var ccc = $false; }
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 2));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 1, .character = 4},
                                                   "bbb", {2, "(2:4~2:7)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 2, .character = 4},
                                                   "AAA", {2, "(1:4~1:7)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 2, .character = 4},
                                                   "ccc", {2, "(4:6~4:9)"}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 2, .character = 4}, "TRUE",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 2, .character = 4}, "HOME",
                                       RenameValidationStatus::NAME_CONFLICT));
}

TEST_F(RenameTest, local1) {
  // rename local without conflict
  const char *content = R"({ var var = 1234; }
{ var aaa = 34;
echo "$aaa";
 { var bbb = ''; }
}
{ var var = 'helll'; }
{{ var aaa = 34; }}
var ccc = $false
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 6}, "var",
                                       {{1, "(1:6~1:9)"}, {1, "(2:7~2:10)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 3, .character = 8}, "var", {{1, "(3:7~3:10)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 0, .character = 8}, "aaa", {{1, "(0:6~0:9)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 0, .character = 8}, "ccc", {{1, "(0:6~0:9)"}}));

  // rename local with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 7},
                                                   "bbb", {1, "(3:7~3:10)"}));
}

TEST_F(RenameTest, local2) {
  const char *content = R"(
function ff(
pp : String) {
  for a in $pp {
    try { $a[1]; }
    catch $e {
      $pp
      $e.show()
    }
  }
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  // function param
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 1}, "var",
                                       {{1, "(2:0~2:2)"}, {1, "(3:12~3:14)"}, {1, "(6:7~6:9)"}}));
  // for-in
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 3, .character = 6}, "var",
                                       {{1, "(3:6~3:7)"}, {1, "(4:11~4:12)"}}));
  // catch
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 11}, "var",
                                       {{1, "(5:11~5:12)"}, {1, "(7:7~7:8)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 6, .character = 7},
                                                   "ff", {1, "(1:9~1:11)"}));
}

TEST_F(RenameTest, upvar) {
  const char *content = R"(
var aaa = 1
{
  $aaa
  var aaa = 12
  function() => {
    var bbb = {
    $aaa; }
  }
}
$aaa
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  // rename hidden global
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 6}, "var",
                                       {{1, "(1:4~1:7)"}, {1, "(3:3~3:6)"}, {1, "(10:1~10:4)"}}));
  // rename upvar
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 7, .character = 6}, "var",
                                       {{1, "(4:6~4:9)"}, {1, "(7:5~7:8)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 6},
                                                   "bbb", {1, "(6:8~6:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 6, .character = 9},
                                                   "aaa", {1, "(1:4~1:7)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 4, .character = 7},
                                                   "bbb", {1, "(6:8~6:11)"}));
}

TEST_F(RenameTest, ifLet) {
  const char *content = R"(
var ret = 34 as Any
if let aaa = $ret as? Int {
  $aaa
  { var bbb = $aaa; }
} elif let aaa = $ret as? String {
  $aaa
  { var bbb = $aaa; }
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 8}, "var",
                                       {{1, "(2:7~2:10)"}, {1, "(3:3~3:6)"}, {1, "(4:15~4:18)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 6, .character = 4}, "cccc",
                                       {{1, "(5:11~5:14)"}, {1, "(6:3~6:6)"}, {1, "(7:15~7:18)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 6},
                                                   "aaa", {1, "(2:7~2:10)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 2, .character = 9},
                                                   "bbb", {1, "(4:8~4:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 6, .character = 5},
                                                   "bbb", {1, "(7:8~7:11)"}));
}

TEST_F(RenameTest, udcParam) {
  const char *content = R"(
[<CLI(name: 'example')>]
typedef Param() {}
fff($pp: Param) {
  echo $pp
  var aaa = $pp
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 14}, "var",
                                       {{1, "(3:5~3:7)"}, {1, "(4:8~4:10)"}, {1, "(5:13~5:15)"}}));
  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 4, .character = 8},
                                                   "aaa", {1, "(5:6~5:9)"}));
}

TEST_F(RenameTest, env) {
  const char *content = R"(
importenv AAA; importenv ZZZ
exportenv BBB = $OSTYPE
CCC=@@@ DDD=^^^^ {
  echo $CCC $PATH
  "$BBB"
  importenv DDD : \
  $AAA
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 11}, "var",
                                       {{1, "(1:10~1:13)"}, {1, "(7:3~7:6)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 11}, "var",
                                       {{1, "(2:10~2:13)"}, {1, "(5:4~5:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 3, .character = 1}, "var",
                                       {{1, "(3:0~3:3)"}, {1, "(4:8~4:11)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 3, .character = 10}, "var", {{1, "(3:8~3:11)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 3, .character = 10}, "IFS", {{1, "(3:8~3:11)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 3, .character = 10}, "ZZZ", {{1, "(3:8~3:11)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 12},
                                                   "DDD", {1, "(3:8~3:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 12}, "PATH",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 3, .character = 10}, "PATH",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 3, .character = 10}, "AAA",
                                       RenameValidationStatus::NAME_CONFLICT));
}

TEST_F(RenameTest, field1) {
  const char *content = R"(  var dummy = "234"; var result = 345
typedef AAA() {
  var aaa = 345
  let bbb = 99 + $result; 34 is Boolean
  let ccc = { $aaa + $bbb + $PATH; }
  typedef TYPE = typeof($ccc)
}
var aaa = new AAA()
$aaa.aaa
$aaa.bbb
$aaa.ccc is AAA.TYPE
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 7}, "AAA",
                                       {{1, "(2:6~2:9)"}, {1, "(4:15~4:18)"}, {1, "(8:5~8:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 3, .character = 6}, "DDDD",
                                       {{1, "(3:6~3:9)"}, {1, "(4:22~4:25)"}, {1, "(9:5~9:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 4, .character = 7}, "EEEE",
                                       {{1, "(4:6~4:9)"}, {1, "(5:25~5:28)"}, {1, "(10:5~10:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 12}, "Bool",
                                       {{1, "(5:10~5:14)"}, {1, "(10:16~10:20)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 2, .character = 7}, "OSTYPE",
                   {{1, "(2:6~2:9)"},
                    {1, "(4:15~4:18)"},
                    {1, "(8:5~8:8)"}})); // rename to builtin variable names (allow shadowing)
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 2, .character = 7}, "dummy",
                   {{1, "(2:6~2:9)"},
                    {1, "(4:15~4:18)"},
                    {1, "(8:5~8:8)"}})); // rename to global variable names (allow shadowing)

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 2, .character = 8},
                                                   "bbb", {1, "(3:6~3:9)"}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 8}, "PATH",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 3, .character = 8},
                                                   "result", {1, "(0:25~0:31)"}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 11}, "Boolean",
                                       RenameValidationStatus::NAME_CONFLICT));
}

TEST_F(RenameTest, field2) {
  const char *content = R"(
var dummy = "234";
let result = 345
typedef AAA {
  var aaa: Int
  let bbb: String
}
var aaa = new AAA(23, 'hello')
$aaa.aaa
$aaa.bbb
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 4, .character = 8}, "AAA",
                                       {{1, "(4:6~4:9)"}, {1, "(8:5~8:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 8}, "dummy",
                                       {{1, "(5:6~5:9)"}, {1, "(9:5~9:8)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 4, .character = 8},
                                                   "bbb", {1, "(5:6~5:9)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 5, .character = 8},
                                                   "aaa", {1, "(4:6~4:9)"}));
}

TEST_F(RenameTest, type1) {
  const char *content = R"(
typedef AAA = Int
{
  34 is AAA
  typedef BBB = String
  34 as BBB
}
34 is AAA
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 9}, "T",
                                       {{1, "(1:8~1:11)"}, {1, "(3:8~3:11)"}, {1, "(7:6~7:9)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 10}, "T",
                                       {{1, "(4:10~4:13)"}, {1, "(5:8~5:11)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 10}, "Bool",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 10}, "Boolean",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 9},
                                                   "BBB", {1, "(4:10~4:13)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 4, .character = 11},
                                                   "AAA", {1, "(1:8~1:11)"}));
}

TEST_F(RenameTest, type2) {
  const char *content = R"(
typedef AAA : Error
typedef BBB() { new BBB(); }
{
  34 is AAA
  typedef BBB = String
  34 as BBB
}
34 is AAA
new BBB()
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 9}, "T",
                                       {{1, "(1:8~1:11)"}, {1, "(4:8~4:11)"}, {1, "(8:6~8:9)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 9}, "T",
                                       {{1, "(2:8~2:11)"}, {1, "(2:20~2:23)"}, {1, "(9:4~9:7)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 10}, "UnixFD",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 10}, "Error",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 9},
                                                   "BBB", {1, "(2:8~2:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 2, .character = 9},
                                                   "AAA", {1, "(1:8~1:11)"}));
}

TEST_F(RenameTest, method1) {
  const char *content = R"(
typedef AAA : Error
[<CLI>] typedef BBB() {}

function size() : Int for AAA {
  return 1 + $this.size() + $this.get().size();
}

function get() : String for AAA {
  return $this.message()
}

function get() : String for BBB {
  return $this.name()
}

new AAA().size()
new AAA().get()
new BBB().get()
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 4, .character = 11}, "size2",
                   {{1, "(4:9~4:13)"}, {1, "(5:19~5:23)"}, {1, "(16:10~16:14)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 8, .character = 10}, "size2",
                   {{1, "(8:9~8:12)"}, {1, "(5:34~5:37)"}, {1, "(17:10~17:13)"}})); // backward ref
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 12, .character = 11}, "size",
                                       {{1, "(12:9~12:12)"}, {1, "(18:10~18:13)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 8, .character = 10},
                                                   "size", {1, "(4:9~4:13)"}));
}

TEST_F(RenameTest, method2) {
  const char *content = R"(
typedef Error1 : Error
typedef Error2 : Error1
[<CLI>] typedef AAA() {}
function get() for Error1 {}
function take() for Error1 {}
function get() for Error2{}
function get() for AAA {}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  // with conflict (override original methods)
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 4, .character = 10}, "message",
                   RenameValidationStatus::NAME_CONFLICT)); // override builtin method
  ASSERT_NO_FATAL_FAILURE(
      this->renameWithConflict(Request{.modId = 1, .line = 6, .character = 10}, "take",
                               {1, "(5:9~5:13)"})); // override user-defined method
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 6, .character = 10}, "show",
                                       RenameValidationStatus::NAME_CONFLICT)); // override builtin
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 7, .character = 10}, "parse",
                                       RenameValidationStatus::NAME_CONFLICT)); // override builtin
}

TEST_F(RenameTest, func) {
  const char *content = R"(
function aaa() {
  $bbb()
}
function bbb() {
  $aaa()
}
$aaa()
$bbb()
{ var ccccc = 34; }
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 11}, "size",
                                       {{1, "(1:9~1:12)"}, {1, "(5:3~5:6)"}, {1, "(7:1~7:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 5}, "d1234",
                                       {{1, "(4:9~4:12)"}, {1, "(2:3~2:6)"}, {1, "(8:1~8:4)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 10}, "OSTYPE",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 10},
                                                   "bbb", {1, "(4:9~4:12)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 10},
                                                   "ccccc", {1, "(9:6~9:11)"}));
}

TEST_F(RenameTest, udc) {
  const char *content = R"(
fff && ls
fff() {
  ggg  # backward ref
}
ggg() {
  fff
}
fff
ggg && ps
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 2}, "size",
                                       {{1, "(2:0~2:3)"}, {1, "(6:2~6:5)"}, {1, "(8:0~8:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(
      Request{.modId = 1, .line = 2, .character = 2}, "1234",
      {{1, "(2:0~2:3)", "\\1234"}, {1, "(6:2~6:5)", "\\1234"}, {1, "(8:0~8:3)", "\\1234"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 3, .character = 3}, "importenv",
                                       {{1, "(5:0~5:3)", "\\importenv"},
                                        {1, "(3:2~3:5)", "\\importenv"},
                                        {1, "(9:0~9:3)", "\\importenv"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 3, .character = 3}, "import-env",
                                       {{1, "(5:0~5:3)", "\\import-env"},
                                        {1, "(3:2~3:5)", "\\import-env"},
                                        {1, "(9:0~9:3)", "\\import-env"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 1}, "echo",
                                       RenameValidationStatus::NAME_CONFLICT)); // builtin
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 1}, "ls",
                                       RenameValidationStatus::NAME_CONFLICT)); // external
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 1}, "ps",
                                       RenameValidationStatus::NAME_CONFLICT)); // external
}

TEST_F(RenameTest, namedArg1) {
  const char *content = R"(
function ff(aa: Int): Int { return $aa + { var bbb=234; $bbb; }; }
typedef Interval(bb: Int, ee: Int) {
  let begin = $bb; typedef dist = Int
  let end = $ee
}
function dist(other: Interval): Int for Interval {
  return ($other.end - this.end) + { var value = 34; $value } +
          $other.begin - $this.begin
}

$ff($aa: 124)
new Interval($bb: 0, $ee: 100).dist(
  $other: new Interval($ee: 800, $bb: 2))
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 1, .character = 36}, "ddd",
                                       {
                                           {1, "(1:12~1:14)"},
                                           {1, "(1:36~1:38)"},
                                           {1, "(11:5~11:7)"},
                                       }));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 27}, "dist",
                                       {
                                           {1, "(2:26~2:28)"},
                                           {1, "(4:13~4:15)"},
                                           {1, "(12:22~12:24)"},
                                           {1, "(13:24~13:26)"},
                                       }));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 6, .character = 17}, "AAAA",
                                       {
                                           {1, "(6:14~6:19)"},
                                           {1, "(7:11~7:16)"},
                                           {1, "(8:11~8:16)"},
                                           {1, "(13:3~13:8)"},
                                       }));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 37},
                                                   "bbb", {1, "(1:47~1:50)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 2, .character = 18},
                                                   "begin", {1, "(3:6~3:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 6, .character = 16},
                                                   "value", {1, "(7:41~7:46)"}));
}

TEST_F(RenameTest, source1) {
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod.ds", "");

  auto content = format(R"(
function aaa() {}
var bbb = 34
typedef AAA = Int
typedef BBB() {}
typedef CCC : Error
fff() {}

source %s as
  mod
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  // var name conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 11},
                                                   "mod", {1, "(9:2~9:5)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 2, .character = 6},
                                                   "mod", {1, "(9:2~9:5)"}));

  // type name conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 3, .character = 9},
                                                   "mod", {1, "(9:2~9:5)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 4, .character = 9},
                                                   "mod", {1, "(9:2~9:5)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 5, .character = 9},
                                                   "mod", {1, "(9:2~9:5)"}));

  // cmd name conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 6, .character = 2},
                                                   "mod", {1, "(9:2~9:5)"}));
}

TEST_F(RenameTest, source2) {
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod.ds", "");

  auto content = format(R"(
function aaa() {}
var bbb = 34
typedef AAA = Int
typedef BBB() {}
typedef CCC : Error
fff() {}

source %s as
  mod
$mod
34 is mod
mod 34 && ls
source %s as
  mod2
)",
                        fileName.c_str(), fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 9, .character = 2}, "size",
                   {{1, "(9:2~9:5)"}, {1, "(10:1~10:4)"}, {1, "(11:6~11:9)"}, {1, "(12:0~12:3)"}}));

  // var name conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 2},
                                                   "aaa", {1, "(1:9~1:12)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 2},
                                                   "bbb", {1, "(2:4~2:7)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 2},
                                                   "mod2", {1, "(14:2~14:6)"}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 9, .character = 2}, "OSTYPE",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 9, .character = 2}, "export-env",
                                       RenameValidationStatus::INVALID_NAME));

  // type name conflict
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 9, .character = 2}, "Int",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 2},
                                                   "AAA", {1, "(3:8~3:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 2},
                                                   "BBB", {1, "(4:8~4:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 2},
                                                   "CCC", {1, "(5:8~5:11)"}));

  // command name conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 2},
                                                   "fff", {1, "(6:0~6:3)"}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 9, .character = 2}, "ls",
                                       RenameValidationStatus::NAME_CONFLICT));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 9, .character = 2}, "command",
                                       RenameValidationStatus::NAME_CONFLICT));

  // conflict with keyword
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 9, .character = 2}, "while",
                                       RenameValidationStatus::KEYWORD));
}

TEST_F(RenameTest, import1) { // for global imported index
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod.ds", R"(
var AAA = 34;
typedef _Int = Bool
)");

  auto content = format(R"(
var DDD = $false
{ var BBB = 34;
$BBB; }
source %s
function CCC(){}
$CCC()
typedef TTT(){}
234 is TTT
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 8}, "AAA",
                                       {{1, "(2:6~2:9)"}, {1, "(3:1~3:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 7, .character = 10}, "_Int",
                                       {{1, "(7:8~7:11)"}, {1, "(8:7~8:10)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 6},
                                                   "AAA", {2, "(1:4~1:7)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 6, .character = 2},
                                                   "AAA", {2, "(1:4~1:7)"}));
}

TEST_F(RenameTest, import2) { // for named imported index
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod.ds", R"(
var AAA = 34;
typedef _Int = Bool
)");

  auto content = format(R"(
var DDD = $false
{ var BBB = 34;
$BBB; }
source %s as mod; $mod.AAA
function CCC(){}
$CCC()
typedef TTT(){}
234 is TTT
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 8}, "AAA",
                                       {{1, "(2:6~2:9)"}, {1, "(3:1~3:4)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 1, .character = 6}, "AAA", {{1, "(1:4~1:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 6, .character = 2}, "AAA",
                                       {{1, "(5:9~5:12)"}, {1, "(6:1~6:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 7, .character = 10}, "_Int",
                                       {{1, "(7:8~7:11)"}, {1, "(8:7~8:10)"}}));
}

TEST_F(RenameTest, import3) { // for inlined imported index
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName3 = tempFileFactory.createTempFile("mod3.ds", R"(
var EEE = 34
eee() {}
)");

  auto fileName2 = tempFileFactory.createTempFile("mod2.ds", R"(
var  XXX = 23
function _ZZZ() {}
fff() {}
)");

  auto fileName1 = tempFileFactory.createTempFile(
      "mod1.ds", format(R"(
source %s inlined
var AAA = 34;
typedef _Int = Bool
source %s
source %s
)",
                        fileName2.c_str(), fileName2.c_str(), fileName3.c_str()));

  auto content = format(R"(
var DDD = $false
{ var BBB = 34;
$BBB; }
source %s
function CCC(){}
$CCC()
typedef TTT(){}
234 is TTT
ggg() {}
)",
                        fileName1.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 10}, "_ZZZ",
                                       {{1, "(5:9~5:12)"}, {1, "(6:1~6:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 8}, "XXX",
                                       {{1, "(2:6~2:9)"}, {1, "(3:1~3:4)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 9, .character = 1}, "_ZZZ", {{1, "(9:0~9:3)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 9, .character = 1}, "eee", {{1, "(9:0~9:3)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 1, .character = 6}, "EEE", {{1, "(1:4~1:7)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 6},
                                                   "AAA", {2, "(2:4~2:7)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 6},
                                                   "XXX", {3, "(1:5~1:8)"}));

  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 1},
                                                   "fff", {3, "(3:0~3:3)"}));
}

TEST_F(RenameTest, import4) { // for inlined imported index
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName3 = tempFileFactory.createTempFile("mod3.ds", R"(
var EEE = 34
eee() {}
)");

  auto fileName2 = tempFileFactory.createTempFile("mod2.ds", R"(
var  XXX = 23
function _ZZZ() {}
fff() {}
)");

  auto fileName1 = tempFileFactory.createTempFile(
      "mod1.ds", format(R"(
source %s inlined
var AAA = 34;
typedef _Int = Bool
source %s
source %s
)",
                        fileName2.c_str(), fileName2.c_str(), fileName3.c_str()));

  auto content = format(R"(
var DDD = $false
{ var BBB = 34;
$BBB; }
source %s inlined
function CCC(){}
$CCC()
typedef TTT(){}
234 is TTT
ggg() {}
)",
                        fileName1.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 5, .character = 10}, "_ZZZ",
                                       {{1, "(5:9~5:12)"}, {1, "(6:1~6:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 8}, "XXX",
                                       {{1, "(2:6~2:9)"}, {1, "(3:1~3:4)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 9, .character = 1}, "_ZZZ", {{1, "(9:0~9:3)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 9, .character = 1}, "eee", {{1, "(9:0~9:3)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 1, .character = 6}, "EEE", {{1, "(1:4~1:7)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 6},
                                                   "AAA", {2, "(2:4~2:7)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 6},
                                                   "XXX", {3, "(1:5~1:8)"}));

  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 1},
                                                   "fff", {3, "(3:0~3:3)"}));
}

TEST_F(RenameTest, globalImported) {
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod1.ds", R"(
var EEE = 34
{ var WWW : Int? }
eee() {}
typedef TTT : Error
function size(aaa:Int) : Int for TTT { return 234; }
function fff(bbb:Int) {}
typedef GGG(ccc:Int) { var size = 34; }
)");

  auto content = format(R"(
var  DDD = ''
ddd() {}
typedef DDD = Int
{ var CCC = 34; }
source %s
{ var FFF = 34; }
$EEE
eee
new TTT().size($aaa:11)
$fff($bbb:22)
new GGG($ccc:33).size
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 1, .character = 5}, "CCC",
                                       {{2, "(1:4~1:7)"}, {1, "(7:1~7:4)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 2, .line = 2, .character = 7}, "CCC", {{2, "(2:6~2:9)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 3, .character = 1}, "fff",
                                       {{2, "(3:0~3:3)"}, {1, "(8:0~8:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 4, .character = 9}, "UUU",
                                       {{2, "(4:8~4:11)"}, {2, "(5:33~5:36)"}, {1, "(9:4~9:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 5, .character = 9}, "empty",
                                       {{2, "(5:9~5:13)"}, {1, "(9:10~9:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 5, .character = 16}, "DDD",
                                       {{2, "(5:14~5:17)"}, {1, "(9:16~9:19)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 6, .character = 9}, "ggg",
                                       {{2, "(6:9~6:12)"}, {1, "(10:1~10:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 6, .character = 14}, "aaa",
                                       {{2, "(6:13~6:16)"}, {1, "(10:6~10:9)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 7, .character = 13}, "FFF",
                                       {{2, "(7:12~7:15)"}, {1, "(11:9~11:12)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 7, .character = 30}, "length",
                                       {{2, "(7:27~7:31)"}, {1, "(11:17~11:21)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 1, .character = 4},
                                                   "DDD", {1, "(1:5~1:8)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 1, .character = 4},
                                                   "FFF", {1, "(6:6~6:9)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 3, .character = 2},
                                                   "ddd", {1, "(2:0~2:3)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 4, .character = 10},
                                                   "DDD", {1, "(3:8~3:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 6, .character = 10},
                                                   "DDD", {1, "(1:5~1:8)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 6, .character = 14},
                                                   "fff", {2, "(6:9~6:12)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 7, .character = 13},
                                                   "size", {2, "(5:9~5:13)"}));
}

TEST_F(RenameTest, inlinedImported) {
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod1.ds", R"(
var EEE = 34
{ var WWW : Int? }
eee() {}
typedef TTT : Error
function size(aaa:Int) : Int for TTT { return 234; }
function fff(bbb:Int) {}
typedef GGG(_ccc:Int) { var size = 34; }
)");

  auto fileName2 = tempFileFactory.createTempFile("mod2.ds", format(R"(
source %s inlined
)",
                                                                    fileName.c_str()));

  auto content = format(R"(
var  DDD = ''
ddd() {}
typedef DDD = Int
{ var CCC = 34; }
source %s
{ var FFF = 34; }
$EEE
eee
new TTT().size($aaa:0)
$fff($bbb:112)
new GGG($_ccc:1).size
)",
                        fileName2.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 1, .character = 5}, "CCC",
                                       {{3, "(1:4~1:7)"}, {1, "(7:1~7:4)"}}));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 3, .line = 2, .character = 7}, "CCC", {{3, "(2:6~2:9)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 3, .character = 1}, "fff",
                                       {{3, "(3:0~3:3)"}, {1, "(8:0~8:3)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 4, .character = 9}, "UUU",
                                       {{3, "(4:8~4:11)"}, {3, "(5:33~5:36)"}, {1, "(9:4~9:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 5, .character = 9}, "empty",
                                       {{3, "(5:9~5:13)"}, {1, "(9:10~9:14)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 5, .character = 16}, "DDD",
                                       {{3, "(5:14~5:17)"}, {1, "(9:16~9:19)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 6, .character = 9}, "ggg",
                                       {{3, "(6:9~6:12)"}, {1, "(10:1~10:4)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 6, .character = 15}, "_eee",
                                       {{3, "(6:13~6:16)"}, {1, "(10:6~10:9)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 7, .character = 13}, "TTT",
                                       {{3, "(7:12~7:16)"}, {1, "(11:9~11:13)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 3, .line = 7, .character = 30}, "length",
                                       {{3, "(7:28~7:32)"}, {1, "(11:17~11:21)"}}));

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 3, .line = 1, .character = 4},
                                                   "DDD", {1, "(1:5~1:8)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 3, .line = 1, .character = 4},
                                                   "FFF", {1, "(6:6~6:9)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 3, .line = 3, .character = 2},
                                                   "ddd", {1, "(2:0~2:3)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 3, .line = 4, .character = 10},
                                                   "DDD", {1, "(3:8~3:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 3, .line = 6, .character = 10},
                                                   "DDD", {1, "(1:5~1:8)"}));
}

TEST_F(RenameTest, namedImported) {
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod1.ds", R"(
var EEE = 34
{ var WWW : Int? }
eee() {}
typedef TTT : Error
function size(_aaa:Int) : Int for TTT { return 234; }
function fff(bbb:Int) {}
typedef GGG(ccc:Int) { let size = 34; }
)");

  auto content = format(R"(
var  DDD = ''
ddd() {}
typedef DDD = Int
{ var CCC = 34; }
source %s as mod
{ var FFF = 34; }
$mod.EEE
mod eee
new mod.TTT().size($_aaa: 12)
$mod.fff($bbb: -222)
new mod.GGG($ccc:0).size
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  // rename field access via module
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 1, .character = 5}, "CCC",
                                       {{2, "(1:4~1:7)"}, {1, "(7:5~7:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 1, .character = 5}, "DDD",
                                       {{2, "(1:4~1:7)"}, {1, "(7:5~7:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 1, .character = 5}, "FFF",
                                       {{2, "(1:4~1:7)"}, {1, "(7:5~7:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 1, .character = 5}, "mod",
                                       {{2, "(1:4~1:7)"}, {1, "(7:5~7:8)"}}));

  // rename sub-command
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 3, .character = 1}, "fff",
                                       {{2, "(3:0~3:3)"}, {1, "(8:4~8:7)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 3, .character = 1}, "ddd",
                                       {{2, "(3:0~3:3)"}, {1, "(8:4~8:7)"}}));

  // rename type field access via mod
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 4, .character = 9}, "UUU",
                                       {{2, "(4:8~4:11)"}, {2, "(5:34~5:37)"}, {1, "(9:8~9:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 4, .character = 9}, "DDD",
                                       {{2, "(4:8~4:11)"}, {2, "(5:34~5:37)"}, {1, "(9:8~9:11)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 4, .character = 9}, "mod",
                                       {{2, "(4:8~4:11)"}, {2, "(5:34~5:37)"}, {1, "(9:8~9:11)"}}));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 5, .character = 9}, "empty",
                                       {{2, "(5:9~5:13)"}, {1, "(9:14~9:18)"}}));

  // rename method call (function via module)
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 6, .character = 9}, "ggg",
                                       {{2, "(6:9~6:12)"}, {1, "(10:5~10:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 6, .character = 9}, "CCC",
                                       {{2, "(6:9~6:12)"}, {1, "(10:5~10:8)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 6, .character = 9}, "mod",
                                       {{2, "(6:9~6:12)"}, {1, "(10:5~10:8)"}}));

  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 7, .character = 29}, "length",
                                       {{2, "(7:27~7:31)"}, {1, "(11:20~11:24)"}}));

  // rename named argument
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 5, .character = 17}, "mod",
                                       {{2, "(5:14~5:18)"}, {1, "(9:20~9:24)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 6, .character = 13}, "WWW",
                                       {{2, "(6:13~6:16)"}, {1, "(10:10~10:13)"}}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 7, .character = 14}, "GGG",
                                       {{2, "(7:12~7:15)"}, {1, "(11:13~11:16)"}}));
}

TEST_F(RenameTest, importModSymbol1) {
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod.ds", R"(
var AAA = 34;
typedef _Int = Bool
)");

  auto fileName2 = tempFileFactory.createTempFile("mod2.ds", format(R"(
source %s as \
mod
)",
                                                                    fileName.c_str()));

  auto content = format(R"(
var DDD = $false
{ var BBB = 34;
$BBB; }
source %s
$mod
function CCC(){}
$CCC()
typedef TTT(){}
fff() {}
)",
                        fileName2.c_str());

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  // rename
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 2, .line = 2, .character = 1}, "EEE",
                                       {{2, "(2:0~2:3)"}, {1, "(5:1~5:4)"}}));

  // rename with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 2, .character = 1},
                                                   "CCC", {1, "(6:9~6:12)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 2, .character = 1},
                                                   "DDD", {1, "(1:4~1:7)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 2, .character = 1},
                                                   "TTT", {1, "(8:8~8:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 2, .line = 2, .character = 1},
                                                   "fff", {1, "(9:0~9:3)"}));
}

TEST_F(RenameTest, importModSymbol2) {
  arsh::TempFileFactory tempFileFactory("arsh_rename");
  auto fileName = tempFileFactory.createTempFile("mod.ds", R"(
var AAA = 34;
typedef _Int = Bool
)");

  auto fileName2 = tempFileFactory.createTempFile("mod2.ds", format(R"(
source %s as \
mod
)",
                                                                    fileName.c_str()));

  auto content = format(R"(
var DDD = $false
{ var BBB = 34;
$BBB; }
source %s
$mod
function CCC(){}
$CCC()
typedef TTT(){}
fff() {}
)",
                        fileName2.c_str());

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), 1));

  // rename
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 1, .character = 5}, "EEE", {{1, "(1:4~1:7)"}}));

  // conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 5},
                                                   "mod", {2, "(2:0~2:3)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 6, .character = 11},
                                                   "mod", {2, "(2:0~2:3)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 8, .character = 9},
                                                   "mod", {2, "(2:0~2:3)"}));
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 9, .character = 2},
                                                   "mod", {2, "(2:0~2:3)"}));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}