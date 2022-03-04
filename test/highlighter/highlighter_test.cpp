#include "gtest/gtest.h"

#include "emitter.h"
#include "parser.h"

using namespace ydsh;

class TokenCollector : public TokenEmitter {
private:
  std::vector<std::pair<HightlightTokenClass, std::string>> tokens;

public:
  explicit TokenCollector(StringRef ref) : TokenEmitter(ref) {}

  auto take() && { return std::move(this->tokens); }

private:
  void emit(HightlightTokenClass tokenClass, Token token) override {
    this->tokens.emplace_back(tokenClass, this->source.substr(token.pos, token.size).toString());
  }
};

static auto lex(StringRef ref) {
  std::string content = ref.toString();
  if (content.empty() || content.back() != '\n') {
    content += '\n';
  }
  TokenCollector collector(content);
  Lexer lexer("dummy", ByteBuffer(content.c_str(), content.c_str() + content.size()), nullptr);
  Parser parser(lexer);
  lexer.setCommentStore(makeObserver(collector));
  parser.setTracker(&collector);
  while (parser && !parser.hasError()) {
    parser();
  }
  return std::move(collector).take();
}

struct EmitterTest : public ::testing::Test {
  void compare(HightlightTokenClass expectClass, const char *expect,
               const std::pair<HightlightTokenClass, std::string> &pair) {
    ASSERT_EQ(expectClass, pair.first);
    ASSERT_EQ(expect, pair.second);
  }
};

TEST_F(EmitterTest, case1) {
  auto ret = lex("echo hello$@[0] 1>&2 # this is a comment");
  ASSERT_EQ(8, ret.size());
  this->compare(HightlightTokenClass::COMMAND, "echo", ret[0]);
  this->compare(HightlightTokenClass::COMMAND_ARG, "hello", ret[1]);
  this->compare(HightlightTokenClass::VARIABLE, "$@", ret[2]);
  this->compare(HightlightTokenClass::NONE, "[", ret[3]);
  this->compare(HightlightTokenClass::NUMBER, "0", ret[4]);
  this->compare(HightlightTokenClass::NONE, "]", ret[5]);
  this->compare(HightlightTokenClass::REDIRECT, "1>&2", ret[6]);
  this->compare(HightlightTokenClass::COMMENT, "# this is a comment", ret[7]);

  ret = lex("var a = 3.4");
  ASSERT_EQ(4, ret.size());
  this->compare(HightlightTokenClass::KEYWORD, "var", ret[0]);
  this->compare(HightlightTokenClass::NONE, "a", ret[1]);
  this->compare(HightlightTokenClass::OPERATOR, "=", ret[2]);
  this->compare(HightlightTokenClass::NUMBER, "3.4", ret[3]);

  ret = lex("assert $/a+/i =~ 'aAa'");
  ASSERT_EQ(4, ret.size());
  this->compare(HightlightTokenClass::KEYWORD, "assert", ret[0]);
  this->compare(HightlightTokenClass::REGEX, "$/a+/i", ret[1]);
  this->compare(HightlightTokenClass::OPERATOR, "=~", ret[2]);
  this->compare(HightlightTokenClass::STRING, "'aAa'", ret[3]);

  ret = lex("assert %'int' is Signal");
  ASSERT_EQ(4, ret.size());
  this->compare(HightlightTokenClass::KEYWORD, "assert", ret[0]);
  this->compare(HightlightTokenClass::SIGNAL, "%'int'", ret[1]);
  this->compare(HightlightTokenClass::OPERATOR, "is", ret[2]);
  this->compare(HightlightTokenClass::TYPE, "Signal", ret[3]);

  ret = lex("@($f(!$false))");
  ASSERT_EQ(7, ret.size());
  this->compare(HightlightTokenClass::NONE, "@(", ret[0]);
  this->compare(HightlightTokenClass::VARIABLE, "$f", ret[1]);
  this->compare(HightlightTokenClass::NONE, "(", ret[2]);
  this->compare(HightlightTokenClass::OPERATOR, "!", ret[3]);
  this->compare(HightlightTokenClass::VARIABLE, "$false", ret[4]);
  this->compare(HightlightTokenClass::NONE, ")", ret[5]);
  this->compare(HightlightTokenClass::NONE, ")", ret[6]);

  ret = lex("coproc ls *");
  ASSERT_EQ(3, ret.size());
  this->compare(HightlightTokenClass::KEYWORD, "coproc", ret[0]);
  this->compare(HightlightTokenClass::COMMAND, "ls", ret[1]);
  this->compare(HightlightTokenClass::COMMAND_ARG, "*", ret[2]);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}