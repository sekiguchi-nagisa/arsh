#include "gtest/gtest.h"

#include "emitter.h"
#include "parser.h"

using namespace ydsh;

class TokenCollector : public TokenEmitter {
private:
  std::vector<std::pair<HighlightTokenClass, std::string>> tokens;

public:
  explicit TokenCollector(StringRef ref) : TokenEmitter(ref) {}

  auto take() && { return std::move(this->tokens); }

private:
  void emit(HighlightTokenClass tokenClass, Token token) override {
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
  void compare(HighlightTokenClass expectClass, const char *expect,
               const std::pair<HighlightTokenClass, std::string> &pair) {
    ASSERT_EQ(expectClass, pair.first);
    ASSERT_EQ(expect, pair.second);
  }
};

TEST_F(EmitterTest, case1) {
  auto ret = lex("echo hello$@[0] 1>&2 # this is a comment");
  ASSERT_EQ(8, ret.size());
  this->compare(HighlightTokenClass::COMMAND, "echo", ret[0]);
  this->compare(HighlightTokenClass::COMMAND_ARG, "hello", ret[1]);
  this->compare(HighlightTokenClass::VARIABLE, "$@", ret[2]);
  this->compare(HighlightTokenClass::NONE, "[", ret[3]);
  this->compare(HighlightTokenClass::NUMBER, "0", ret[4]);
  this->compare(HighlightTokenClass::NONE, "]", ret[5]);
  this->compare(HighlightTokenClass::REDIRECT, "1>&2", ret[6]);
  this->compare(HighlightTokenClass::COMMENT, "# this is a comment", ret[7]);

  ret = lex("var a = 3.4");
  ASSERT_EQ(4, ret.size());
  this->compare(HighlightTokenClass::KEYWORD, "var", ret[0]);
  this->compare(HighlightTokenClass::NONE, "a", ret[1]);
  this->compare(HighlightTokenClass::OPERATOR, "=", ret[2]);
  this->compare(HighlightTokenClass::NUMBER, "3.4", ret[3]);

  ret = lex("assert $/a+/i =~ 'aAa'");
  ASSERT_EQ(4, ret.size());
  this->compare(HighlightTokenClass::KEYWORD, "assert", ret[0]);
  this->compare(HighlightTokenClass::REGEX, "$/a+/i", ret[1]);
  this->compare(HighlightTokenClass::OPERATOR, "=~", ret[2]);
  this->compare(HighlightTokenClass::STRING, "'aAa'", ret[3]);

  ret = lex("assert %'int' is Signal");
  ASSERT_EQ(4, ret.size());
  this->compare(HighlightTokenClass::KEYWORD, "assert", ret[0]);
  this->compare(HighlightTokenClass::SIGNAL, "%'int'", ret[1]);
  this->compare(HighlightTokenClass::OPERATOR, "is", ret[2]);
  this->compare(HighlightTokenClass::TYPE, "Signal", ret[3]);

  ret = lex("@($f(!$false))");
  ASSERT_EQ(7, ret.size());
  this->compare(HighlightTokenClass::NONE, "@(", ret[0]);
  this->compare(HighlightTokenClass::VARIABLE, "$f", ret[1]);
  this->compare(HighlightTokenClass::NONE, "(", ret[2]);
  this->compare(HighlightTokenClass::OPERATOR, "!", ret[3]);
  this->compare(HighlightTokenClass::VARIABLE, "$false", ret[4]);
  this->compare(HighlightTokenClass::NONE, ")", ret[5]);
  this->compare(HighlightTokenClass::NONE, ")", ret[6]);

  ret = lex("coproc ls *");
  ASSERT_EQ(3, ret.size());
  this->compare(HighlightTokenClass::KEYWORD, "coproc", ret[0]);
  this->compare(HighlightTokenClass::COMMAND, "ls", ret[1]);
  this->compare(HighlightTokenClass::COMMAND_ARG, "*", ret[2]);
}

TEST_F(EmitterTest, case2) {
  TokenCollector collector("hello");
  collector(TokenKind::COMMAND, Token{.pos = 100, .size = 12});
  auto values = std::move(collector).take();
  ASSERT_TRUE(values.empty());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}