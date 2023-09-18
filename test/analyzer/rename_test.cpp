
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
    auto src = this->srcMan.findById(req.getModId());
    ASSERT_TRUE(src);
    auto pos = toTokenPos(src->getContent(), req.toPosition());
    ASSERT_TRUE(pos.hasValue());

    SymbolRequest sr = {
        .modId = req.getModId(),
        .pos = pos.unwrap(),
    };

    auto s = validateRename(this->indexes, sr, newName, nullptr);
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}