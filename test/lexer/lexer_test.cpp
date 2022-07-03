#include "gtest/gtest.h"

#include <lexer.h>

using namespace ydsh;

/**
 * ####################
 * #  Lv0: basic api  #
 * ####################
 */
static Lexer createLexer(const char *text) {
  ByteBuffer buf;
  buf.append(text, strlen(text));
  return Lexer("(string)", std::move(buf), nullptr);
}

TEST(LexerTest_Lv0, line) {
  auto lex = createLexer("0123\n567\n9");
  Token token{.pos = 0, .size = 1};
  Token lineToken = lex.getLineToken(token);
  ASSERT_EQ(0, lineToken.pos);
  ASSERT_EQ(5, lineToken.size);
  ASSERT_EQ("0123\n", lex.toTokenText(lineToken));

  token = Token{.pos = 3, .size = 1};
  lineToken = lex.getLineToken(token);
  ASSERT_EQ(0, lineToken.pos);
  ASSERT_EQ(5, lineToken.size);
  ASSERT_EQ("0123\n", lex.toTokenText(lineToken));

  token = Token{.pos = 4, .size = 1};
  lineToken = lex.getLineToken(token);
  ASSERT_EQ(0, lineToken.pos);
  ASSERT_EQ(5, lineToken.size);
  ASSERT_EQ("0123\n", lex.toTokenText(lineToken));

  token = Token{.pos = 5, .size = 1};
  lineToken = lex.getLineToken(token);
  ASSERT_EQ(5, lineToken.pos);
  ASSERT_EQ(4, lineToken.size);
  ASSERT_EQ("567\n", lex.toTokenText(lineToken));

  token = Token{.pos = 9, .size = 1};
  lineToken = lex.getLineToken(token);
  ASSERT_EQ(9, lineToken.pos);
  ASSERT_EQ(2, lineToken.size);
  ASSERT_EQ("9\n", lex.toTokenText(lineToken));

  token = Token{.pos = 10, .size = 10};
  lineToken = lex.getLineToken(token);
  ASSERT_EQ(9, lineToken.pos);
  ASSERT_EQ(2, lineToken.size);
  ASSERT_EQ("9\n", lex.toTokenText(lineToken));

  token = Token{.pos = 11, .size = 0};
  lineToken = lex.getLineToken(token);
  ASSERT_EQ(9, lineToken.pos);
  ASSERT_EQ(2, lineToken.size);
  ASSERT_EQ("9\n", lex.toTokenText(lineToken));

  token = Token{.pos = 11, .size = 1};
  lineToken = lex.getLineToken(token);
  ASSERT_EQ(9, lineToken.pos);
  ASSERT_EQ(2, lineToken.size);
  ASSERT_EQ("9\n", lex.toTokenText(lineToken));

  token = Token{.pos = 1000, .size = 10};
  lineToken = lex.getLineToken(token);
  ASSERT_EQ(9, lineToken.pos);
  ASSERT_EQ(2, lineToken.size);
  ASSERT_EQ("9\n", lex.toTokenText(lineToken));
}

/**
 * #####################################################
 * #  Lv1: statement token and Lexer::toTokenText().  ##
 * #####################################################
 */

class LexerTest_Lv1 : public ::testing::Test {
public:
  LexerPtr lexer;
  std::vector<std::pair<TokenKind, Token>> tokens;

public:
  LexerTest_Lv1() = default;

  ~LexerTest_Lv1() override = default;

  // for test
  virtual void initLexer(const char *text) {
    this->lexer = LexerPtr::create("(string)", ByteBuffer(text, text + strlen(text)), nullptr);
  }

  virtual void initLexer(const char *text, LexerMode mode) {
    this->initLexer(text);
    this->lexer->setLexerMode(mode);
  }

private:
  virtual void tokenize() {
    Token t;
    TokenKind k;
    do {
      k = this->lexer->nextToken(t);
      this->tokens.emplace_back(k, t);
    } while (k != TokenKind::EOS && k != TokenKind::INVALID);
  }

  static const char *toName(TokenKind kind) {
    const char *t[] = {
#define GEN_NAME(E, S) #E,
        EACH_TOKEN(GEN_NAME)
#undef GEN_NAME
    };
    return t[static_cast<unsigned int>(kind)];
  }

  virtual void assertKind(TokenKind expected, TokenKind actual) {
    ASSERT_STREQ(this->toName(expected), this->toName(actual));
  }

  virtual void assertToken(unsigned int index, TokenKind expectedKind, const char *expectedText) {
    ASSERT_TRUE(index < this->tokens.size());
    auto pair = this->tokens[index];
    this->assertKind(expectedKind, pair.first);

    std::string text = this->lexer->toTokenText(pair.second);
    ASSERT_STREQ(expectedText, text.c_str());
  }

public:
  virtual void assertTokens(std::vector<std::pair<TokenKind, const char *>> expectedList) {
    this->tokenize();

    const unsigned int size = expectedList.size();
    ASSERT_EQ(size, this->tokens.size());

    for (unsigned int i = 0; i < size; i++) {
      this->assertToken(i, expectedList[i].first, expectedList[i].second);
    }
  }

  virtual void assertLexerMode(LexerMode mode) {
    ASSERT_EQ(mode.toString(), this->lexer->getLexerMode().toString());
  }
};

typedef std::vector<std::pair<TokenKind, const char *>> ExpectedList;

void addPair(ExpectedList &) {}

template <typename... T>
void addPair(ExpectedList &list, TokenKind kind, const char *text, T &&...rest) {
  list.push_back(std::make_pair(kind, text));
  addPair(list, std::forward<T>(rest)...);
}

template <typename... T>
ExpectedList expect(TokenKind kind, const char *text, T &&...rest) {
  ExpectedList list;
  addPair(list, kind, text, std::forward<T>(rest)...);
  return list;
}

#define EXPECT(...) this->assertTokens(expect(__VA_ARGS__))

TEST_F(LexerTest_Lv1, assert_tok) {
  const char *text = "assert";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ASSERT, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, break_tok) {
  const char *text = "break";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::BREAK, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, catch_tok) {
  const char *text = "catch";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CATCH, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycPARAM));
}

TEST_F(LexerTest_Lv1, continue_tok) {
  const char *text = "continue";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CONTINUE, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, do_tok) {
  const char *text = "do";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DO, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, defer_tok) {
  const char *text = "defer";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DEFER, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, elif_tok1) {
  const char *text = "elif";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ELIF, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, elif_tok2) {
  const char *text = "elifA";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, text));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, else_tok1) {
  const char *text = "else";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ELSE, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, else_tok2) {
  const char *text = "else";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ELSE, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, exportenv_tok1) {
  const char *text = "export-env";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EXPORT_ENV, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, exportenv_tok2) {
  const char *text = "export-env";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EXPORT_ENV, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, finally_tok) {
  const char *text = "finally";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FINALLY, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, for_tok1) {
  const char *text = "for";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FOR, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, for_tok2) {
  const char *text = "for";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FOR, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, function_tok) {
  const char *text = "function";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FUNCTION, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, if_tok) {
  const char *text = "if";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IF, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, importenv_tok1) {
  const char *text = "import-env";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IMPORT_ENV, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, importenv_tok2) {
  const char *text = "importenv";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IMPORT_ENV, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, interface_tok) {
  const char *text = "interface";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INTERFACE, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, let_tok) {
  const char *text = "let";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LET, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, new_tok) {
  const char *text = "new";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::NEW, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, not_tok) {
  const char *text = "!";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::NOT, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, return_tok) {
  const char *text = "return";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RETURN, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, source_tok) {
  const char *text = "source";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SOURCE, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, try_tok) {
  const char *text = "try";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::TRY, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, throw_tok) {
  const char *text = "throw";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::THROW, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, alias_tok) {
  const char *text = "alias";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ALIAS, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, typedef_tok) {
  const char *text = "typedef";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::TYPEDEF, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, var_tok) {
  const char *text = "var";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::VAR, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, while_tok1) {
  const char *text = "while";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::WHILE, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, while_tok2) {
  const char *text = "while";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::WHILE, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, plus_tok1) {
  const char *text = "+";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::PLUS, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, plus_tok2) {
  const char *text = "+";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ADD, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, minus_tok1) {
  const char *text = "-";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::MINUS, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, minus_tok2) {
  const char *text = "-";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SUB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

/**
 * literal test
 */
// integer literal
TEST_F(LexerTest_Lv1, int_literal1) {
  const char *text = "0";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal2) {
  const char *text = "123408";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal3) {
  const char *text = "9";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal4) {
  const char *text = "759801";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal5) {
  const char *text = "0xabcdef0123456789ABCDEF";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal6) {
  const char *text = "014";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal7) {
  const char *text = "0O14";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

// invalid int literal
TEST_F(LexerTest_Lv1, invalid_int_literal1) {
  const char *text = "+23";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::PLUS, "+", TokenKind::INT_LITERAL, "23", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, invalid_int_literal2) {
  const char *text = "-23";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::MINUS, "-", TokenKind::INT_LITERAL, "23", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, invalid_int_literal3) {
  const char *text = "008";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INT_LITERAL, "00", TokenKind::INVALID, "8"));
}

// float literal
TEST_F(LexerTest_Lv1, float_literal1) {
  const char *text = "0.010964";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FLOAT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, float_literal2) {
  const char *text = "103.0109640";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FLOAT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, float_literal3) {
  const char *text = "0.010964e0";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FLOAT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, float_literal4) {
  const char *text = "12.010964E-102";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FLOAT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, float_literal5) {
  const char *text = "0.00";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FLOAT_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

// invalid float literal
TEST_F(LexerTest_Lv1, float_literal6) {
  const char *text = "0.010964e+01";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FLOAT_LITERAL, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, invalid_float_literal1) {
  const char *text = "0012.04e-78";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::INT_LITERAL, "0012", TokenKind::ACCESSOR, ".", TokenKind::INVALID, "0"));
}

// string literal
TEST_F(LexerTest_Lv1, string_literal1) {
  const char *text = "''";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_literal2) {
  const char *text = "'fhrωu4あ\t3\"5^*&!@#~AFG '";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_literal3) {
  const char *text = R"('\t\n\r\\')";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, "'\\t\\n\\r\\\\'", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_litearl4) {
  const char *text = "'\n\thoge\\\"'";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, estring_literal1) {
  const char *text = "$'\\''";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, "$'\\''", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, estring_literal2) {
  const char *text = "$'\\n'";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, "$'\\n'", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, estring_literal3) {
  const char *text = "$'\\\\'";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, "$'\\\\'", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, estring_literal4) {
  const char *text = "$'\\\\'";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, "$'\\\\'", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, estring_literal5) {
  const char *text = "$'\\'";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, "$'\\'", TokenKind::EOS, ""));
}

// invalid string literal
TEST_F(LexerTest_Lv1, invalid_string_literal) {
  const char *text = "'\\''";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::STRING_LITERAL, "'\\'", TokenKind::INVALID, "'"));
}

TEST_F(LexerTest_Lv1, string_expr1) {
  const char *text = R"("hello word")";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::OPEN_DQUOTE, "\"", TokenKind::STR_ELEMENT, "hello word",
                                 TokenKind::CLOSE_DQUOTE, "\"", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr2) {
  const char *text = R"("hello ${a} word")";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::OPEN_DQUOTE, "\"", TokenKind::STR_ELEMENT, "hello ",
                                 TokenKind::APPLIED_NAME, "${a}", TokenKind::STR_ELEMENT, " word",
                                 TokenKind::CLOSE_DQUOTE, "\"", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr3) {
  const char *text = R"("hello\"world")";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::OPEN_DQUOTE, "\"", TokenKind::STR_ELEMENT,
                                 "hello\\\"world", TokenKind::CLOSE_DQUOTE, "\"", TokenKind::EOS,
                                 ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr4) {
  const char *text = R"("hello\$world")";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::OPEN_DQUOTE, "\"", TokenKind::STR_ELEMENT,
                                 "hello\\$world", TokenKind::CLOSE_DQUOTE, "\"", TokenKind::EOS,
                                 ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr5) {
  const char *text = R"("\\")";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::OPEN_DQUOTE, "\"", TokenKind::STR_ELEMENT, "\\\\",
                                 TokenKind::CLOSE_DQUOTE, "\"", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr6) {
  const char *text = "\"\n\t\\$\\\\$(ls)hoge\"";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::OPEN_DQUOTE, "\"", TokenKind::STR_ELEMENT,
                                 "\n\t\\$\\\\", TokenKind::START_SUB_CMD, "$(", TokenKind::COMMAND,
                                 "ls", TokenKind::RP, ")", TokenKind::STR_ELEMENT, "hoge",
                                 TokenKind::CLOSE_DQUOTE, "\"", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, string_expr7) {
  const char *text = R"("hello$")";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::OPEN_DQUOTE, "\"", TokenKind::STR_ELEMENT, "hello",
                                 TokenKind::STR_ELEMENT, "$", TokenKind::CLOSE_DQUOTE, "\"",
                                 TokenKind::EOS, ""));
}

// regex literal
TEST_F(LexerTest_Lv1, regex1) {
  const char *text = "$/hoge/";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::REGEX_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, regex2) {
  const char *text = "$/ho\\/ge/";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::REGEX_LITERAL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, invalid_regex1) {
  const char *text = "$/ho/ge/";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::REGEX_LITERAL, "$/ho/ge", TokenKind::DIV, "/", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, invalid_regex2) {
  const char *text = "$/ho\nge/";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, "$"));
}

TEST_F(LexerTest_Lv1, subCmd1) {
  const char *text = "$(";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_SUB_CMD, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, subCmd2) {
  const char *text = "$(";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycDSTRING);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_SUB_CMD, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, subCmd3) {
  const char *text = "$(";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_SUB_CMD, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycCMD));
}

TEST_F(LexerTest_Lv1, interp1) {
  const char *text = "${";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_INTERP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycCMD));
}

TEST_F(LexerTest_Lv1, interp2) {
  const char *text = "${";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycDSTRING);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_INTERP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, inSub1) {
  const char *text = ">(";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_IN_SUB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, inSub2) {
  const char *text = ">(";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_IN_SUB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycCMD));
}

TEST_F(LexerTest_Lv1, outSub1) {
  const char *text = "<(";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_OUT_SUB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, outSub2) {
  const char *text = "<(";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::START_OUT_SUB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycCMD));
}

TEST_F(LexerTest_Lv1, atparen) {
  const char *text = "@(12\n";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::AT_PAREN, "@(", TokenKind::CMD_ARG_PART, "12", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycCMD, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

// applied name
TEST_F(LexerTest_Lv1, appliedName1) {
  const char *text = "$w10i_fArhue";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::APPLIED_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, appliedName2) {
  const char *text = "$__0";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::APPLIED_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, appliedName3) {
  const char *text = "$hoge";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::APPLIED_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, appliedName4) {
  const char *text = "$ab02";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycDSTRING);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::APPLIED_NAME, text, TokenKind::STR_ELEMENT, "\n", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, appliedName5) {
  const char *text = "${ab02}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycDSTRING);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::APPLIED_NAME, text, TokenKind::STR_ELEMENT, "\n", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, appliedName6) {
  const char *text = "$ab02";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::APPLIED_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, appliedName7) {
  const char *text = "${ab02}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::APPLIED_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, appliedName8) {
  const char *text = "$hello[";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::APPLIED_NAME_WITH_BRACKET, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
}

TEST_F(LexerTest_Lv1, appliedName9) {
  const char *text = "$hello(";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::APPLIED_NAME_WITH_PAREN, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
}

// special name
TEST_F(LexerTest_Lv1, specialName1) {
  const char *text = "$@";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName2) {
  const char *text = "$0";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName3) {
  const char *text = "$?";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName4) {
  const char *text = "$@";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycDSTRING);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::STR_ELEMENT, "\n", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, specialName5) {
  const char *text = "${0}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycDSTRING);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::STR_ELEMENT, "\n", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, specialName6) {
  const char *text = "$?";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, specialName7) {
  const char *text = "${@}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, specialName8) {
  const char *text = "$#";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName9) {
  const char *text = "$6";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName10) {
  const char *text = "${9}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, specialName11) {
  const char *text = "$2[";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME_WITH_BRACKET, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
}

TEST_F(LexerTest_Lv1, specialName12) {
  const char *text = "$?[";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SPECIAL_NAME_WITH_BRACKET, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
}

/**
 * brace test
 */
TEST_F(LexerTest_Lv1, LP1) {
  const char *text = "(";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, LP2) {
  const char *text = "(";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, LP3) {
  const char *text = "(";
  this->initLexer(text, yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, LP4) {
  const char *text = "(";
  this->initLexer(text, yycPARAM);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycPARAM, true}));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, RP1) {
  const char *text = ")";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RP, ")", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RP2) {
  const char *text = ")";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycSTMT);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RP3) {
  const char *text = ")";
  this->initLexer(text, yycEXPR);
  this->lexer->pushLexerMode(yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, RP4) {
  const char *text = ")";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RP5) {
  const char *text = ")";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycPARAM);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RP, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LB1) {
  const char *text = "[";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
}

TEST_F(LexerTest_Lv1, LB2) {
  const char *text = "[";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode({yycSTMT, true}));
}

TEST_F(LexerTest_Lv1, RB1) {
  const char *text = "]";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RB2) {
  const char *text = "]";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RB3) {
  const char *text = "]";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycSTMT);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RB, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LBC1) {
  const char *text = "{";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LBC, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, LBC2) {
  const char *text = "{";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LBC, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
  this->lexer->popLexerMode();
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, RBC1) {
  const char *text = "}";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RBC, "}", TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RBC2) {
  const char *text = "}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycSTMT);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RBC, text, TokenKind::EOS, ""););
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RBC3) {
  const char *text = "}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::RBC, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

/*
 * command token
 */
TEST_F(LexerTest_Lv1, CMD1) {
  const char *text = "\\assert";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, "\\assert", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD2) {
  const char *text = R"(\ \t\r\n\;\'\"\`\|\&\<\>\(\)\{\}\$\#\!\[\]\8)";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD3) {
  const char *text = "あ漢ω";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD4) {
  const char *text = "l\\";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, "l\\\n", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD5) {
  const char *text = "\\";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD6) {
  const char *text = "d#d";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD7) {
  const char *text = "d#";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD8) {
  const char *text = "d!!";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD9) {
  const char *text = "!hoge";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::NOT, "!", TokenKind::COMMAND, "hoge", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD10) {
  const char *text = "\\$true";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD11) {
  const char *text = "\\[\\]true";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD12) {
  const char *text = "/usr/bin/[";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD13) {
  const char *text = "/usr/bin/]";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG1) { // allow  '[' and ']'
  const char *text = "[[][";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG2) {
  const char *text = "abcd";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG3) {
  const char *text = "a2134:*\\:";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, "a2134:", TokenKind::GLOB_ZERO_OR_MORE,
                                 "*", TokenKind::CMD_ARG_PART, "\\:", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG4) {
  const char *text = "\\;あω𪗱";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG5) {
  const char *text = "??\\\n";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::GLOB_ANY, "?", TokenKind::GLOB_ANY, "?", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG6) {
  const char *text = "{}}{,";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::BRACE_OPEN, "{", TokenKind::BRACE_CLOSE, "}",
                                 TokenKind::BRACE_CLOSE, "}", TokenKind::BRACE_OPEN, "{",
                                 TokenKind::BRACE_SEP, ",", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG7) {
  const char *text = "hfeiru#fr";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG8) {
  const char *text = "ああ#";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG9) {
  const char *text = "!hoge";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG10) {
  const char *text = "qwwre!";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG11) {
  const char *text = "\\;q\\$wwre!";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG12) {
  const char *text = "q\\ w\\\twre!";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG13) {
  const char *text = "AA\\\n";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, "AA\\\n", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG14) {
  const char *text = "\\?\\?";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::CMD_ARG_PART, "\\?\\?", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG15) {
  const char *text = "\\?\\*\\??";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::CMD_ARG_PART, "\\?\\*\\?", TokenKind::GLOB_ANY, "?", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BRACE_SEQ1) {
  const char *text = "{0..9}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::BRACE_CHAR_SEQ, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BRACE_SEQ2) {
  const char *text = "{-00034..+45678..+000}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::BRACE_INT_SEQ, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BRACE_SEQ3) {
  const char *text = "{0..a..+9}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::BRACE_CHAR_SEQ, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BRACE_SEQ4) {
  const char *text = "{Z..a..-009}";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::BRACE_CHAR_SEQ, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, ENV_ASSIGN1) {
  const char *text = "_1Ab9=23";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::ENV_ASSIGN, "_1Ab9=", TokenKind::CMD_ARG_PART, "23", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, ENV_ASSIGN2) {
  const char *text = "@%abc=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ENV_ASSIGN, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, ENV_ASSIGN3) {
  const char *text = "\\0==";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(
      EXPECT(TokenKind::ENV_ASSIGN, "\\0=", TokenKind::CMD_ARG_PART, "=", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, ENV_ASSIGN4) {
  const char *text = "ho\\\n=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ENV_ASSIGN, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, ENV_ASSIGN5) {
  const char *text = "ho\\ 3=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ENV_ASSIGN, text, TokenKind::EOS, ""));
}

/**
 * test expr token in stmt mode.
 */
TEST_F(LexerTest_Lv1, COLON1) {
  const char *text = ":";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, COLON2) {
  const char *text = ":";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COLON, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, COMMA1) {
  const char *text = ",";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, COMMA2) {
  const char *text = ",";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMA, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MUL1) {
  const char *text = "*";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, MUL2) {
  const char *text = "*";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::MUL, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, DIV1) {
  const char *text = "/";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, DIV2) {
  const char *text = "/";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DIV, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MOD1) {
  const char *text = "%";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, MOD2) {
  const char *text = "%";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::MOD, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LT1) {
  const char *text = "<";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, "<"));
}

TEST_F(LexerTest_Lv1, LT2) {
  const char *text = "<";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LT, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, GT1) {
  const char *text = ">";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, ">"));
}

TEST_F(LexerTest_Lv1, GT2) {
  const char *text = ">";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::GT, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LE1) {
  const char *text = "<=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, "<"));
}

TEST_F(LexerTest_Lv1, LE2) {
  const char *text = "<=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LE, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, GE1) {
  const char *text = ">=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, ">"));
}

TEST_F(LexerTest_Lv1, GE2) {
  const char *text = ">=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::GE, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, EQ1) {
  const char *text = "==";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, "="));
}

TEST_F(LexerTest_Lv1, EQ2) {
  const char *text = "==";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EQ, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, NE1) {
  const char *text = "!=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::NOT, "!", TokenKind::INVALID, "="));
}

TEST_F(LexerTest_Lv1, NE2) {
  const char *text = "!=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::NE, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, AND1) {
  const char *text = "and";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, "and", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, AND2) {
  const char *text = "and";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::AND, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, AND3) {
  const char *text = "andF";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, text));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, BG1) {
  const char *text = "&";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::BACKGROUND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BG2) {
  const char *text = "&";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::BACKGROUND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BG3) {
  const char *text = "&!";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DISOWN_BG, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BG4) {
  const char *text = "&!";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DISOWN_BG, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BG5) {
  const char *text = "&|";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DISOWN_BG, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, BG6) {
  const char *text = "&|";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DISOWN_BG, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, OR1) {
  const char *text = "or";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, "or", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, OR2) {
  const char *text = "or";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::OR, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, OR3) {
  const char *text = "orA";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, text));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, PIPE1) {
  const char *text = "|";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::PIPE, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, PIPE2) {
  const char *text = "|";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::PIPE, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, XOR1) {
  const char *text = "xor";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, "xor", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, XOR2) {
  const char *text = "xor";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::XOR, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, XOR3) { // invalid
  const char *text = "^";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, "^"));
}

TEST_F(LexerTest_Lv1, XOR4) {
  const char *text = "xorT";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, text));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, COND_AND1) {
  const char *text = "&&";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, "&"));
}

TEST_F(LexerTest_Lv1, COND_AND2) {
  const char *text = "&&";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COND_AND, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, COND_AND3) {
  const char *text = "&&";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COND_AND, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, COND_OR1) {
  const char *text = "||";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, "|"));
}

TEST_F(LexerTest_Lv1, COND_OR2) {
  const char *text = "||";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COND_OR, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, COND_OR3) {
  const char *text = "||";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COND_OR, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MATCH1) {
  const char *text = "=~";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, "="));
}

TEST_F(LexerTest_Lv1, MATCH2) {
  const char *text = "=~";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::MATCH, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, UNMATCH1) {
  const char *text = "!~";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::NOT, "!", TokenKind::COMMAND, "~", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, UNMATCH2) {
  const char *text = "!~";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::UNMATCH, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, INC1) {
  const char *text = "++";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::PLUS, "+", TokenKind::PLUS, "+", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, INC2) {
  const char *text = "++";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INC, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, DEC1) {
  const char *text = "--";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::MINUS, "-", TokenKind::MINUS, "-", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, DEC2) {
  const char *text = "--";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DEC, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, ASSIGN1) {
  const char *text = "=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, text));
}

TEST_F(LexerTest_Lv1, ASSIGN2) {
  const char *text = "=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ASSIGN, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, ADD_ASSIGN1) {
  const char *text = "+=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::PLUS, "+", TokenKind::INVALID, "="));
}

TEST_F(LexerTest_Lv1, ADD_ASSIGN2) {
  const char *text = "+=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ADD_ASSIGN, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, SUB_ASSIGN1) {
  const char *text = "-=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::MINUS, "-", TokenKind::INVALID, "="));
}

TEST_F(LexerTest_Lv1, SUB_ASSIGN2) {
  const char *text = "-=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::SUB_ASSIGN, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MUL_ASSIGN1) {
  const char *text = "*=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ENV_ASSIGN, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, MUL_ASSIGN2) {
  const char *text = "*=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::MUL_ASSIGN, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, DIV_ASSIGN1) {
  const char *text = "/=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ENV_ASSIGN, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, DIV_ASSIGN2) {
  const char *text = "/=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::DIV_ASSIGN, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MOD_ASSIGN1) {
  const char *text = "%=";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ENV_ASSIGN, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, MOD_ASSIGN2) {
  const char *text = "%=";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::MOD_ASSIGN, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, AS1) {
  const char *text = "as";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, AS2) {
  const char *text = "as";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::AS, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, AS3) {
  const char *text = "as";
  this->initLexer(text, yycNAME);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IDENTIFIER, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, AS4) {
  const char *text = "asO";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, text));
}

TEST_F(LexerTest_Lv1, FUNC1) {
  const char *text = "Func";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, FUNC2) {
  const char *text = "Func";
  this->initLexer(text, yycTYPE);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::FUNC, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, FUNC3) {
  const char *text = "Func";
  this->initLexer(text, yycNAME);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IDENTIFIER, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, IN1) {
  const char *text = "in";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, IN2) {
  const char *text = "in";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IN, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, IN3) {
  const char *text = "in";
  this->initLexer(text, yycNAME);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IDENTIFIER, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, IN4) {
  const char *text = "inQ";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, text));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, IS1) {
  const char *text = "is";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, IS2) {
  const char *text = "is";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IS, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, IS3) {
  const char *text = "is";
  this->initLexer(text, yycNAME);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IDENTIFIER, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, IS4) {
  const char *text = "isW";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::INVALID, text));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, TYPEOF1) {
  const char *text = "typeof";
  this->initLexer(text, yycTYPE);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::TYPEOF, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, TYPEOF2) {
  const char *text = "typeof";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, NAME1) {
  const char *text = "assert";
  this->initLexer(text, yycNAME);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::IDENTIFIER, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, NAME2) {
  const char *text = "assert";
  this->initLexer(text, yycTYPE);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::TYPE_NAME, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, ACCESS1) {
  const char *text = ".";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, ACCESS2) {
  const char *text = ".";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ACCESSOR, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

/**
 * new line, space and comment
 */
TEST_F(LexerTest_Lv1, LINE_END1) {
  const char *text = ";";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LINE_END, text, TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, LINE_END2) {
  const char *text = ";";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LINE_END, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LINE_END3) {
  const char *text = ";";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::LINE_END, text, TokenKind::EOS, ""));
  ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

class DefaultCommentStore : public CommentStore {
private:
  std::vector<Token> tokens;

public:
  void operator()(Token token) override { this->tokens.push_back(token); }

  const auto &getTokens() const { return this->tokens; }
};

TEST_F(LexerTest_Lv1, COMMENT1) {
  const char *text = "#fhreuvrei o";
  this->initLexer(text);
  DefaultCommentStore commentStore;
  this->lexer->setCommentStore(makeObserver(commentStore));
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
  ASSERT_EQ(1, commentStore.getTokens().size());
  ASSERT_EQ(text, this->lexer->toTokenText(commentStore.getTokens().back()));
}

TEST_F(LexerTest_Lv1, COMMENT2) {
  const char *text = "#ああ  あ";
  this->initLexer(text, yycEXPR);
  DefaultCommentStore commentStore;
  this->lexer->setCommentStore(makeObserver(commentStore));
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
  ASSERT_EQ(1, commentStore.getTokens().size());
  ASSERT_EQ(text, this->lexer->toTokenText(commentStore.getTokens().back()));
}

TEST_F(LexerTest_Lv1, COMMENT3) {
  const char *text = "#hello  \t  ";
  this->initLexer(text, yycNAME);
  DefaultCommentStore commentStore;
  this->lexer->setCommentStore(makeObserver(commentStore));
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
  ASSERT_EQ(1, commentStore.getTokens().size());
  ASSERT_EQ(text, this->lexer->toTokenText(commentStore.getTokens().back()));
}

TEST_F(LexerTest_Lv1, COMMENT4) {
  const char *text = "#hferu";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycTYPE);
  DefaultCommentStore commentStore;
  this->lexer->setCommentStore(makeObserver(commentStore));
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
  ASSERT_EQ(1, commentStore.getTokens().size());
  ASSERT_EQ(text, this->lexer->toTokenText(commentStore.getTokens().back()));
}

TEST_F(LexerTest_Lv1, COMMENT5) {
  const char *text = "#2345y;;::";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycCMD);
  DefaultCommentStore commentStore;
  this->lexer->setCommentStore(makeObserver(commentStore));
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
  ASSERT_EQ(1, commentStore.getTokens().size());
  ASSERT_EQ(text, this->lexer->toTokenText(commentStore.getTokens().back()));
}

TEST_F(LexerTest_Lv1, COMMENT6) {
  const char *text = "echo #1234\n#abcd";
  this->initLexer(text);
  DefaultCommentStore commentStore;
  this->lexer->setCommentStore(makeObserver(commentStore));
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, "echo", TokenKind::EOS, ""));
  ASSERT_EQ(2, commentStore.getTokens().size());
  ASSERT_EQ("#1234", this->lexer->toTokenText(commentStore.getTokens()[0]));
  ASSERT_EQ("#abcd", this->lexer->toTokenText(commentStore.getTokens()[1]));
}

TEST_F(LexerTest_Lv1, EMPTY) {
  const char *text = "";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE1) {
  const char *text = "    \t   \n  ";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE2) {
  const char *text = "   \n var \\\r\\\n";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::VAR, "var", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE3) {
  const char *text = "\n  \n assert \\\r\\\n";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::ASSERT, "assert", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE4) {
  const char *text = "\\\r\\\necho";
  this->initLexer(text);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::COMMAND, "echo", TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE5) {
  const char *text = "    \t   \n  \\\n\\\r ";
  this->initLexer(text, yycEXPR);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE6) {
  const char *text = "     \t    \n  \\\n\\\r    \t";
  this->initLexer(text);
  this->lexer->pushLexerMode(yycTYPE);
  ASSERT_NO_FATAL_FAILURE(EXPECT(TokenKind::EOS, ""));
}

TEST(LexerTest_Lv2, NEW_LINE) {
  std::string line = "  \n  \n   assert  \n ";
  Lexer lexer("(string)", ByteBuffer(line.c_str(), line.c_str() + line.size()), nullptr);
  Token t;
  TokenKind k = lexer.nextToken(t);

  ASSERT_STREQ(toString(TokenKind::ASSERT), toString(k));
  ASSERT_TRUE(lexer.isPrevNewLine());

  k = lexer.nextToken(t);
  ASSERT_STREQ(toString(TokenKind::EOS), toString(k));
  ASSERT_TRUE(lexer.isPrevNewLine());
  k = lexer.nextToken(t);
  ASSERT_STREQ(toString(TokenKind::EOS), toString(k));
  ASSERT_TRUE(lexer.isPrevNewLine());
}

TEST(LexerTest_Lv2, NEW_LINE2) {
  std::string line = "  \n  var a";
  Lexer lexer("(string)", ByteBuffer(line.c_str(), line.c_str() + line.size()), nullptr);
  Token t;
  TokenKind k = lexer.nextToken(t);

  ASSERT_STREQ(toString(TokenKind::VAR), toString(k));
  ASSERT_TRUE(lexer.isPrevNewLine());

  k = lexer.nextToken(t);
  ASSERT_STREQ(toString(TokenKind::IDENTIFIER), toString(k));
  ASSERT_FALSE(lexer.isPrevNewLine());

  k = lexer.nextToken(t);
  ASSERT_STREQ(toString(TokenKind::EOS), toString(k));
  ASSERT_TRUE(lexer.isPrevNewLine());
  ASSERT_FALSE(lexer.isPrevSpace());

  k = lexer.nextToken(t);
  ASSERT_STREQ(toString(TokenKind::EOS), toString(k));
  ASSERT_TRUE(lexer.isPrevNewLine());
  ASSERT_FALSE(lexer.isPrevSpace());
}

TEST(LexerTest_Lv3, IllegalChar) {
  unsigned char str[] = {0x82, 0}; // broken UTF-8 code

  ByteBuffer buf;
  buf.append((char *)str, std::size(str));
  Lexer lexer("(string)", std::move(buf), nullptr);
  Token t;
  TokenKind k = lexer.nextToken(t);

  ASSERT_STREQ(toString(TokenKind::INVALID), toString(k));
}

TEST(LineNumTest, case1) {
  LineNumTable lineNumTable;
  ASSERT_EQ(1u, lineNumTable.getOffset());
  ASSERT_EQ(1u, lineNumTable.lookup(12));      // empty
  ASSERT_EQ(1u, lineNumTable.getMaxLineNum()); // empty
}

TEST(LineNumTest, case2) {
  LineNumTable table;
  table.addNewlinePos(5);
  ASSERT_EQ(1u, table.lookup(3));
  ASSERT_EQ(1u, table.lookup(4));
  ASSERT_EQ(1u, table.lookup(5));
  ASSERT_EQ(2u, table.lookup(6));

  table.setOffset(2);
  ASSERT_EQ(2u, table.getOffset());
  ASSERT_EQ(2u, table.lookup(3));
  ASSERT_EQ(2u, table.lookup(4));
  ASSERT_EQ(2u, table.lookup(5));
  ASSERT_EQ(3u, table.lookup(6));
}

TEST(LineNumTest, case3) {
  LineNumTable table;
  table.addNewlinePos(5);
  table.addNewlinePos(4); // overwrite
  ASSERT_EQ(1u, table.lookup(3));
  ASSERT_EQ(1u, table.lookup(4));
  ASSERT_EQ(1u, table.lookup(5));
  ASSERT_EQ(2u, table.lookup(6));
}

TEST(LineNumTest, case4) {
  LineNumTable table;
  table.setOffset(4);
  ASSERT_EQ(4u, table.lookup(5));
  table.addNewlinePos(10);
  ASSERT_EQ(4u, table.lookup(5));
  ASSERT_EQ(5u, table.lookup(13));
}

#define ARRAY(...)                                                                                 \
  (int[]) { __VA_ARGS__ }

struct EscapeSeqTest : public ::testing::Test {
  static void assertEscape(StringRef ref, std::vector<int> &&codes, bool needPrefix = false) {
    std::vector<int> out;
    const char *begin = ref.begin();
    const char *end = ref.end();
    while (begin != end) {
      auto ret = parseEscapeSeq(begin, end, needPrefix);
      if (ret) {
        out.push_back(ret.codePoint);
        begin += ret.consumedSize;
      } else {
        out.push_back(*(begin++));
      }
    }
    ASSERT_EQ(codes, out);
  }

  template <unsigned int N>
  void assertEscape(StringRef ref, const int (&codes)[N], bool needPrefix = false) {
    std::vector<int> expect(std::begin(codes), std::end(codes));
    this->assertEscape(ref, std::move(expect), needPrefix);
  }

  void assertEscape(StringRef ref, int code, bool needPrefix = false) {
    this->assertEscape(ref, ARRAY(code), needPrefix);
  }
};

TEST_F(EscapeSeqTest, base) {
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("1\\a1", ARRAY('1', '\a', '1')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("1\\b1", ARRAY('1', '\b', '1')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("1\\e1", ARRAY('1', 0x1b, '1')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("1\\E1", ARRAY('1', 0x1b, '1')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\f", '\f'));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\n", '\n'));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\r", '\r'));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\t\\v", ARRAY('\t', '\v')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("1\\d1", ARRAY('1', '\\', 'd', '1')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\'", ARRAY('\\', '\'')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\\"", ARRAY('\\', '"')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\\\", ARRAY('\\')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\xFq", ARRAY(0x0F, 'q')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\xa11", ARRAY(0xa1, '1')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\xW", ARRAY('\\', 'x', 'W')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\x", ARRAY('\\', 'x')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\", ARRAY('\\')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("s\\0", ARRAY('s', '\0')));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\0123", ARRAY('\123'), true));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\0123", ARRAY('\012', '3'), false));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\uA9", 0xa9));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\u2328", 0x2328));
  ASSERT_NO_FATAL_FAILURE(this->assertEscape("\\U2328", 0x2328));
}

TEST_F(EscapeSeqTest, error) {
  StringRef ref = "\\z";
  auto ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::UNKNOWN, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(2, ret.consumedSize);

  ref = "34";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::END, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(0, ret.consumedSize);

  ref = "";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::END, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(0, ret.consumedSize);

  ref = "\\";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::END, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(0, ret.consumedSize);

  ref = "\\x1w";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::OK, ret.kind);
  ASSERT_EQ('\x01', ret.codePoint);
  ASSERT_EQ(3, ret.consumedSize);

  ref = "\\x";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::NEED_CHARS, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(2, ret.consumedSize);

  ref = "\\xQ";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::NEED_CHARS, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(2, ret.consumedSize);

  ref = "\\uQ";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::NEED_CHARS, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(2, ret.consumedSize);

  ref = "\\u";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::NEED_CHARS, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(2, ret.consumedSize);

  ref = "\\UQ";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::NEED_CHARS, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(2, ret.consumedSize);

  ref = "\\U";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::NEED_CHARS, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(2, ret.consumedSize);

  ref = "\\123";
  ret = parseEscapeSeq(ref.begin(), ref.end(), true);
  ASSERT_EQ(EscapeSeqResult::UNKNOWN, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(2, ret.consumedSize);

  ref = "\\UFFFFFF";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::RANGE, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(8, ret.consumedSize);

  ref = "\\UFFFFFFF";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::RANGE, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(9, ret.consumedSize);

  ref = "\\UFFFFFFFF";
  ret = parseEscapeSeq(ref.begin(), ref.end(), false);
  ASSERT_EQ(EscapeSeqResult::RANGE, ret.kind);
  ASSERT_EQ(-1, ret.codePoint);
  ASSERT_EQ(10, ret.consumedSize);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
