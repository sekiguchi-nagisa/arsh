
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
    auto ret = analyzer.analyze(*src, action);
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
                       auto range = toRange(*src, conflict.symbol.getToken());
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

TEST_F(RenameTest, global1) {
  // rename global without conflict
  const char *content = R"(
var aaaa = 1234;
echo $aaaa;
$aaaa++
"${aaaa}"
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, 1));
  ASSERT_NO_FATAL_FAILURE(
      this->rename(Request{.modId = 1, .line = 1, .character = 4}, "var",
                   {{1, "(1:4~1:8)"}, {1, "(2:6~2:10)"}, {1, "(3:1~3:5)"}, {1, "(4:3~4:7)"}}));

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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}