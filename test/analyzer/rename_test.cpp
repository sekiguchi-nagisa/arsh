
#include "gtest/gtest.h"

#include "analyzer.h"
#include "indexer.h"
#include "rename.h"

using namespace ydsh::lsp;

struct Request {
  unsigned short modId;
  int line;
  int character;

  ydsh::ModId getModId() const { return static_cast<ydsh::ModId>(this->modId); }

  Position toPosition() const {
    return {
        .line = this->line,
        .character = this->character,
    };
  }
};

struct Result {
  ydsh::ModId modId;
  std::string rangeStr;
  std::string newName;

  Result(ydsh::ModId modId, std::string &&rangeStr, std::string &&newName)
      : modId(modId), rangeStr(rangeStr), newName(newName) {}

  Result(unsigned short modId, std::string &&rangeStr, std::string &&newName)
      : Result(static_cast<ydsh::ModId>(modId), std::move(rangeStr), std::move(newName)) {}

  Result(unsigned short modId, std::string &&rangeStr) : Result(modId, std::move(rangeStr), "") {}
};

struct Conflict {
  ydsh::ModId modId;
  std::string rangeStr;

  Conflict(ydsh::ModId modId, std::string &&rangeStr)
      : modId(modId), rangeStr(std::move(rangeStr)) {}

  Conflict(unsigned short modId, std::string &&rangeStr)
      : Conflict(static_cast<ydsh::ModId>(modId), std::move(rangeStr)) {}
};

class RenameTest : public ::testing::Test {
private:
  ydsh::SysConfig sysConfig;
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
    ASSERT_EQ(modId, ydsh::toUnderlying(ret->getModId()));
  }

  void rename(Request req, const char *newName, RenameValidationStatus status) {
    this->renameImpl(req, newName, status, nullptr);
  }

  void renameWithConflict(Request req, const char *newName, Conflict &&expect) {
    ydsh::Optional<Conflict> actual;
    this->renameImpl(req, newName, RenameValidationStatus::NAME_CONFLICT,
                     [&](const RenameResult &ret) {
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
                     [&](const RenameResult &ret) {
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
                  const std::function<void(const RenameResult &)> &consumer) {
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
importenv AAA
exportenv BBB = $OSTYPE
CCC=@@@ DDD=^^^^ {
  echo $CCC
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

  // with conflict
  ASSERT_NO_FATAL_FAILURE(this->renameWithConflict(Request{.modId = 1, .line = 1, .character = 12},
                                                   "DDD", {1, "(3:8~3:11)"}));
  ASSERT_NO_FATAL_FAILURE(this->rename(Request{.modId = 1, .line = 2, .character = 12}, "PATH",
                                       RenameValidationStatus::NAME_CONFLICT));
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}