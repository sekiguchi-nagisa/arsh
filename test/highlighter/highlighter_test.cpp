#include "gtest/gtest.h"

#include "factory.h"

using namespace ydsh;
using namespace highlighter;

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
  tokenizeAndEmit(collector, "dummy");
  return std::move(collector).take();
}

struct EmitterTest : public ::testing::Test {
  void compare(HighlightTokenClass expectClass, const char *expect,
               const std::pair<HighlightTokenClass, std::string> &pair) {
    ASSERT_EQ(expectClass, pair.first);
    ASSERT_EQ(expect, pair.second);
  }

  void tokenize(FormatterFactory &factory, StringRef ref, std::ostream &output) {
    factory.setSource(ref);

    auto ret = factory.create(output);
    ASSERT_TRUE(ret);
    auto formatter = std::move(ret).take();
    ASSERT_TRUE(formatter);
    tokenizeAndEmit(*formatter, "<dummy>");
    formatter->finalize();
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

TEST_F(EmitterTest, color) {
  constexpr auto c1 = styleRule("");
  static_assert(!c1.text);
  static_assert(!c1.background);
  static_assert(!c1.border);
  static_assert(!c1.bold);
  static_assert(!c1.italic);
  static_assert(!c1.underline);

  constexpr auto c2 = styleRule("          ");
  static_assert(!c2.text);
  static_assert(!c2.background);
  static_assert(!c2.border);
  static_assert(!c2.bold);
  static_assert(!c2.italic);
  static_assert(!c2.underline);

  constexpr auto c3 = styleRule("  bold  italic  underline bg:");
  static_assert(!c3.text);
  static_assert(!c3.background);
  static_assert(!c3.border);
  static_assert(c3.bold);
  static_assert(c3.italic);
  static_assert(c3.underline);

  constexpr auto c4 = styleRule("#123456  bold border:#000000 italic  underline  bg:#fbd");
  static_assert(c4.text);
  static_assert(c4.text.red == 0x12);
  static_assert(c4.text.green == 0x34);
  static_assert(c4.text.blue == 0x56);
  static_assert(c4.background);
  static_assert(c4.background.red == 0xFF);
  static_assert(c4.background.green == 0xbb);
  static_assert(c4.background.blue == 0xdd);
  static_assert(c4.border);
  static_assert(c4.border.red == 0);
  static_assert(c4.border.green == 0);
  static_assert(c4.border.blue == 0);
  static_assert(c4.bold);
  static_assert(c4.italic);
  static_assert(c4.underline);
}

TEST_F(EmitterTest, style) {
  auto *style = findStyle("darcula");
  ASSERT_TRUE(style);
  ASSERT_STREQ("darcula", style->getName());
  ASSERT_TRUE(style->find(HighlightTokenClass::KEYWORD));

  style = findStyle("null");
  ASSERT_TRUE(style);
  ASSERT_STREQ("null", style->getName());
  ASSERT_FALSE(style->find(HighlightTokenClass::KEYWORD));

  style = findStyle("not found ");
  ASSERT_FALSE(style);
}

TEST_F(EmitterTest, factory) {
  FormatterFactory factory;
  factory.setFormatName("fjriejfoie");
  auto ret = factory.create(std::cerr);
  ASSERT_FALSE(ret);
  ASSERT_EQ("unsupported formatter: fjriejfoie", ret.asErr());

  factory = FormatterFactory();
  factory.setStyleName("freafref");
  ret = factory.create(std::cerr);
  ASSERT_FALSE(ret);
  ASSERT_EQ("unsupported style: freafref", ret.asErr());
}

TEST_F(EmitterTest, nullFormatter) {
  std::stringstream stream;
  std::string content = R"(#!/usr/bin/env ydsh
  function sum($a : Int) : Int for Int {
    return $this + $a
  }

  123456.sum($@.size() + $#)  # this is a comment

)";

  FormatterFactory factory;
  factory.setFormatName("null");

  ASSERT_NO_FATAL_FAILURE(this->tokenize(factory, content, stream));

  ASSERT_EQ(content, stream.str());
}

TEST_F(EmitterTest, ansiFormatter) {
  std::stringstream stream;
  std::string content = R"(#!/usr/bin/env ydsh
# this is a comment
)";

  FormatterFactory factory;
  factory.setFormatName("ansi");
  factory.setStyleName("darcula");

  ASSERT_NO_FATAL_FAILURE(this->tokenize(factory, content, stream));

  const char *expected = "\033[38;2;128;128;128m#!/usr/bin/env ydsh\033[0m\n"
                         "\033[38;2;128;128;128m# this is a comment\033[0m\n";
  ASSERT_EQ(expected, stream.str());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}