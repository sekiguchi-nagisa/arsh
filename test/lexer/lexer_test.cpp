#include "gtest/gtest.h"

#include <lexer.h>
#include <type.h>

#ifndef LEXER_TEST_DIR
#define LEXER_TEST_DIR "./"
#endif

using namespace ydsh;

/**
 * ############################
 * #  Lv0: test file loading  #
 * ############################
 */
TEST(LexerTest_Lv0, case1) {
    FILE *fp = fopen(LEXER_TEST_DIR  "/" "lexer_test.cpp", "r");
    ASSERT_FALSE(fp == nullptr);
    fclose(fp);
}

/**
 * #####################################################
 * #  Lv1: statement token and Lexer::toTokenText().  ##
 * #####################################################
 */

class LexerTest_Lv1 : public ::testing::Test {
public:
    Lexer *lexer;
    std::vector<std::pair<TokenKind, Token>> tokens;

public:
    LexerTest_Lv1() = default;

    ~LexerTest_Lv1() override {
        delete this->lexer;
        this->lexer = nullptr;
    }

    // for test
    virtual void initLexer(const char *text) {
        this->lexer = new Lexer("(string)", ByteBuffer(text, text + strlen(text)), std::string());
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
        } while(k != EOS && k != INVALID);
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

        for(unsigned int i = 0; i < size; i++) {
            this->assertToken(i, expectedList[i].first, expectedList[i].second);
        }
    }

    virtual void assertLexerMode(LexerMode mode) {
        ASSERT_STREQ(toModeName(mode), toModeName(this->lexer->getLexerMode()));
    }
};

typedef std::vector<std::pair<TokenKind, const char *>> ExpectedList;

void addPair(ExpectedList &) {
}

template <typename ...T>
void addPair(ExpectedList &list, TokenKind kind, const char *text, T&& ...rest) {
    list.push_back(std::make_pair(kind, text));
    addPair(list, std::forward<T>(rest)...);
}

template <typename ...T>
ExpectedList expect(TokenKind kind, const char *text, T&& ...rest) {
    ExpectedList list;
    addPair(list, kind, text, std::forward<T>(rest)...);
    return list;
}

#define EXPECT(...) this->assertTokens(expect(__VA_ARGS__))

TEST_F(LexerTest_Lv1, assert_tok) {
    const char *text = "assert";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ASSERT, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, break_tok) {
    const char *text = "break";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(BREAK, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, catch_tok) {
    const char *text = "catch";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CATCH, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, class_tok) {
    const char *text = "class";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CLASS, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, continue_tok) {
    const char *text = "continue";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CONTINUE, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, do_tok) {
    const char *text = "do";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(DO, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, elif_tok) {
    const char *text = "elif";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ELIF, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, else_tok1) {
    const char *text = "else";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ELSE, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, else_tok2) {
    const char *text = "else";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ELSE, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, exportenv_tok) {
    const char *text = "export-env";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EXPORT_ENV, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, finally_tok) {
    const char *text = "finally";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FINALLY, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, for_tok) {
    const char *text = "for";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FOR, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, function_tok) {
    const char *text = "function";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FUNCTION, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, if_tok) {
    const char *text = "if";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IF, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, importenv_tok) {
    const char *text = "import-env";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IMPORT_ENV, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, interface_tok) {
    const char *text = "interface";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INTERFACE, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, let_tok) {
    const char *text = "let";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LET, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, new_tok) {
    const char *text = "new";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(NEW, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, not_tok) {
    const char *text = "!";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(NOT, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, return_tok) {
    const char *text = "return";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RETURN, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, source_tok) {
    const char *text = "source";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SOURCE, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, try_tok) {
    const char *text = "try";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(TRY, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, throw_tok) {
    const char *text = "throw";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(THROW, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, alias_tok) {
    const char *text = "alias";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ALIAS, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, var_tok) {
    const char *text = "var";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(VAR, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

TEST_F(LexerTest_Lv1, while_tok1) {
    const char *text = "while";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(WHILE, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, while_tok2) {
    const char *text = "while";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(WHILE, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, plus_tok1) {
    const char *text = "+";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(PLUS, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, plus_tok2) {
    const char *text = "+";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ADD, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, minus_tok1) {
    const char *text = "-";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MINUS, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, minus_tok2) {
    const char *text = "-";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SUB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

/**
 * literal test
 */
// integer literal
TEST_F(LexerTest_Lv1, int_literal1) {
    const char *text = "0";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal2) {
    const char *text = "123408";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal3) {
    const char *text = "9";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal4) {
    const char *text = "759801";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal5) {
    const char *text = "0xabcdef0123456789ABCDEF";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal6) {
    const char *text = "014";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, int_literal7) {
    const char *text = "0O14";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

// invalid int literal
TEST_F(LexerTest_Lv1, invalid_int_literal1) {
    const char *text = "+23";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(PLUS, "+", INT_LITERAL, "23", EOS, ""));
}

TEST_F(LexerTest_Lv1, invalid_int_literal2) {
    const char *text = "-23";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MINUS, "-", INT_LITERAL, "23", EOS, ""));
}

TEST_F(LexerTest_Lv1, invalid_int_literal3) {
    const char *text = "008";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, "00", INVALID, "8"));
}

// float literal
TEST_F(LexerTest_Lv1, float_literal1) {
    const char *text = "0.010964";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FLOAT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, float_literal2) {
    const char *text = "103.0109640";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FLOAT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, float_literal3) {
    const char *text = "0.010964e0";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FLOAT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, float_literal4) {
    const char *text = "12.010964E-102";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FLOAT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, float_literal5) {
    const char *text = "0.00";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FLOAT_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

// invalid float literal
TEST_F(LexerTest_Lv1, invalid_float_literal1) {
    const char *text = "0.010964e+01";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FLOAT_LITERAL, "0.010964e+0", INVALID, "1"));
}

TEST_F(LexerTest_Lv1, invalid_float_literal2) {
    const char *text = "0012.04e-78";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INT_LITERAL, "0012", ACCESSOR, ".", INVALID, "0"));
}

// string literal
TEST_F(LexerTest_Lv1, string_literal1) {
    const char *text = "''";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_literal2) {
    const char *text = "'fhrωu4あ\t3\"5^*&!@#~AFG '";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_literal3) {
    const char *text = R"('\t\n\r\\')";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, "'\\t\\n\\r\\\\'", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_litearl4) {
    const char *text = "'\n\thoge\\\"'";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, estring_literal1) {
    const char *text = "$'\\''";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, "$'\\''", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, estring_literal2) {
    const char *text = "$'\\n'";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, "$'\\n'", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, estring_literal3) {
    const char *text = "$'\\\\'";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, "$'\\\\'", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, estring_literal4) {
    const char *text = "$'\\\\'";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, "$'\\\\'", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}


// invalid string literal
TEST_F(LexerTest_Lv1, invalid_string_literal) {
    const char *text = "'\\''";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(STRING_LITERAL, "'\\'", INVALID, "'"));
}

TEST_F(LexerTest_Lv1, string_expr1) {
    const char *text = R"("hello word")";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(
            EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello word",
                   CLOSE_DQUOTE, "\"", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr2) {
    const char *text = R"("hello ${a} word")";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(
            EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello ",
                   APPLIED_NAME, "${a}", STR_ELEMENT, " word",
                   CLOSE_DQUOTE, "\"", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr3) {
    const char *text = R"("hello\"world")";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(
            EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello\\\"world",
                   CLOSE_DQUOTE, "\"", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr4) {
    const char *text = R"("hello\$world")";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(
            EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello\\$world",
                   CLOSE_DQUOTE, "\"", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr5) {
    const char *text = R"("\\")";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(
            EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "\\\\",
                   CLOSE_DQUOTE, "\"", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, string_expr6) {
    const char *text = "\"\n\t\\$\\\\$(ls)hoge\"";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(
            EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "\n\t\\$\\\\",
                   START_SUB_CMD, "$(", COMMAND, "ls", RP, ")", STR_ELEMENT, "hoge",
                   CLOSE_DQUOTE, "\"", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, invalid_string_expr) {
    const char *text = R"("hello$")";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello", INVALID, "$"));
}

// regex literal
TEST_F(LexerTest_Lv1, regex1) {
    const char *text = "$/hoge/";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(REGEX_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, regex2) {
    const char *text = "$/ho\\/ge/";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(REGEX_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, invalid_regex1) {
    const char *text = "$/ho/ge/";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(REGEX_LITERAL, "$/ho/", INVALID, "g"));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, invalid_regex2) {
    const char *text = "$/ho\nge/";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INVALID, "$"));
}

TEST_F(LexerTest_Lv1, signal1) {
    const char *text = "%'de1'";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SIGNAL_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, signal2) {
    const char *text = "%'INT'";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SIGNAL_LITERAL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, invalid_signal) {
    const char *text = "%'000'";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, "%", STRING_LITERAL, "'000'", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}


TEST_F(LexerTest_Lv1, subCmd1) {
    const char *text = "$(";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(START_SUB_CMD, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, subCmd2) {
    const char *text = "$(";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycDSTRING);
    ASSERT_NO_FATAL_FAILURE(EXPECT(START_SUB_CMD, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, subCmd3) {
    const char *text = "$(";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(START_SUB_CMD, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycCMD));
}

TEST_F(LexerTest_Lv1, inSub1) {
    const char *text = ">(";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(START_IN_SUB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, inSub2) {
    const char *text = ">(";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(START_IN_SUB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycCMD));
}

TEST_F(LexerTest_Lv1, outSub1) {
    const char *text = "<(";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(START_OUT_SUB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, outSub2) {
    const char *text = "<(";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(START_OUT_SUB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycCMD));
}


// applied name
TEST_F(LexerTest_Lv1, appliedName1) {
    const char *text = "$w10i_fArhue";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(APPLIED_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, appliedName2) {
    const char *text ="$__0";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(APPLIED_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, appliedName3) {
    const char *text = "$hoge";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(APPLIED_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, appliedName4) {
    const char *text = "$ab02";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycDSTRING);
    ASSERT_NO_FATAL_FAILURE(EXPECT(APPLIED_NAME, text, STR_ELEMENT, "\n", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, appliedName5) {
    const char *text = "${ab02}";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycDSTRING);
    ASSERT_NO_FATAL_FAILURE(EXPECT(APPLIED_NAME, text, STR_ELEMENT, "\n", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, appliedName6) {
    const char *text = "$ab02";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(APPLIED_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, appliedName7) {
    const char *text = "${ab02}";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(APPLIED_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, appliedName8) {
    const char *text = "$hello[";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(APPLIED_NAME_WITH_BRACKET, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

// special name
TEST_F(LexerTest_Lv1, specialName1) {
    const char *text = "$@";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName2) {
    const char *text = "$0";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName3) {
    const char *text = "$?";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName4) {
    const char *text = "$@";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycDSTRING);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, STR_ELEMENT, "\n", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, specialName5) {
    const char *text = "${0}";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycDSTRING);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, STR_ELEMENT, "\n", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycDSTRING));
}

TEST_F(LexerTest_Lv1, specialName6) {
    const char *text = "$?";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, specialName7) {
    const char *text = "${@}";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, specialName8) {
    const char *text = "$#";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName9) {
    const char *text = "$6";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, specialName10) {
    const char *text = "${9}";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, specialName11) {
    const char *text = "$2[";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME_WITH_BRACKET, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, specialName12) {
    const char *text = "$?[";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SPECIAL_NAME_WITH_BRACKET, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}


/**
 * brace test
 */
TEST_F(LexerTest_Lv1, LP1) {
    const char *text = "(";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LP, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, LP2) {
    const char *text = "(";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LP, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, RP1) {
    const char *text = ")";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RP, ")", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RP2) {
    const char *text = ")";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycSTMT);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RP, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RP3) {
    const char *text = ")";
    this->initLexer(text, yycEXPR);
    this->lexer->pushLexerMode(yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RP, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, LB1) {
    const char *text = "[";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LB2) {
    const char *text = "[";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}


TEST_F(LexerTest_Lv1, RB1) {
    const char *text = "]";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RB2) {
    const char *text = "]";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RB3) {
    const char *text = "]";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycSTMT);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RB, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LBC1) {
    const char *text = "{";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LBC, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));

}

TEST_F(LexerTest_Lv1, LBC2) {
    const char *text = "{";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LBC, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
    this->lexer->popLexerMode();
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, RBC1) {
    const char *text = "}";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RBC, "}", EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RBC2) {
    const char *text = "}";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycSTMT);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RBC, text, EOS, ""););
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, RBC3) {
    const char *text = "}";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(RBC, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

/*
 * command token
 */
TEST_F(LexerTest_Lv1, CMD1) {
    const char *text = "\\assert";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, "\\assert", EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD2) {
    const char *text = R"(\ \t\r\n\;\'\"\`\|\&\<\>\(\)\{\}\$\#\!\[\]\8)";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD3) {
    const char *text = "あ漢ω";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD4) {
    const char *text = "l\\";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, "l\\\n", EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD5) {
    const char *text = "\\";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD6) {
    const char *text = "d#d";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD7) {
    const char *text = "d#";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD8) {
    const char *text = "d!!";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD9) {
    const char *text = "!hoge";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(NOT, "!", COMMAND, "hoge", EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD10) {
    const char *text = "\\$true";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD11) {
    const char *text = "\\[\\]true";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD12) {
    const char *text = "/usr/bin/[";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD13) {
    const char *text = "/usr/bin/]";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG1) {   // allow  '[' and ']'
    const char *text = "[[][";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG2) {
    const char *text = "abcd";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG3) {
    const char *text = "a2134:*";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, "a2134:", GLOB_ZERO_OR_MORE, "*", EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG4) {
    const char *text = "\\;あω𪗱";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG5) {
    const char *text = "??\\\n";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(GLOB_ANY, "?", GLOB_ANY, "?", EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG6) {
    const char *text = "{}}{";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG7) {
    const char *text = "hfeiru#fr";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG8) {
    const char *text = "ああ#";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG9) {
    const char *text = "!hoge";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG10) {
    const char *text = "qwwre!";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG11) {
    const char *text = "\\;q\\$wwre!";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG12) {
    const char *text = "q\\ w\\\twre!";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG13) {
    const char *text = "AA\\\n";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, "AA\\\n", EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG14) {
    const char *text = "\\?\\?";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, "\\?\\?", EOS, ""));
}

TEST_F(LexerTest_Lv1, CMD_ARG15) {
    const char *text = "\\?\\*\\??";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(CMD_ARG_PART, "\\?\\*\\?", GLOB_ANY, "?", EOS, ""));
}

/**
 * test expr token in stmt mode.
 */
TEST_F(LexerTest_Lv1, COLON1) {
    const char *text = ":";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, COLON2) {
    const char *text = ":";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COLON, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, COMMA1) {
    const char *text = ",";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, COMMA2) {
    const char *text = ",";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMA, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MUL1) {
    const char *text = "*";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, MUL2) {
    const char *text = "*";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MUL, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, DIV1) {
    const char *text = "/";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, DIV2) {
    const char *text = "/";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(DIV, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MOD1) {
    const char *text = "%";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, MOD2) {
    const char *text = "%";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MOD, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LT1) {
    const char *text = "<";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INVALID, "<"));
}

TEST_F(LexerTest_Lv1, LT2) {
    const char *text = "<";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LT, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, GT1) {
    const char *text = ">";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INVALID, ">"));
}

TEST_F(LexerTest_Lv1, GT2) {
    const char *text = ">";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(GT, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LE1) {
    const char *text = "<=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INVALID, "<"));
}

TEST_F(LexerTest_Lv1, LE2) {
    const char *text = "<=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LE, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, GE1) {
    const char *text = ">=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INVALID, ">"));
}

TEST_F(LexerTest_Lv1, GE2) {
    const char *text = ">=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(GE, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, EQ1) {
    const char *text = "==";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, EQ2) {
    const char *text = "==";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EQ, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, NE1) {
    const char *text = "!=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(NOT, "!", COMMAND, "=", EOS, ""));
}

TEST_F(LexerTest_Lv1, NE2) {
    const char *text = "!=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(NE, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, AND1) {
    const char *text = "and";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, "and", EOS, ""));
}

TEST_F(LexerTest_Lv1, AND2) {
    const char *text = "and";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(AND, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, BG1) {
    const char *text = "&";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(BACKGROUND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, BG2) {
    const char *text = "&";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(BACKGROUND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, BG3) {
    const char *text = "&!";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(DISOWN_BG, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, BG4) {
    const char *text = "&!";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(DISOWN_BG, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, BG5) {
    const char *text = "&|";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(DISOWN_BG, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, BG6) {
    const char *text = "&|";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(DISOWN_BG, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, OR1) {
    const char *text = "or";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, "or", EOS, ""));
}

TEST_F(LexerTest_Lv1, OR2) {
    const char *text = "or";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(OR, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, PIPE1) {
    const char *text = "|";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(PIPE, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, PIPE2) {
    const char *text = "|";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(PIPE, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, XOR1) {
    const char *text = "xor";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, "xor", EOS, ""));
}

TEST_F(LexerTest_Lv1, XOR2) {
    const char *text = "xor";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(XOR, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, XOR3) {   // invalid
    const char *text = "^";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INVALID, "^"));
}

TEST_F(LexerTest_Lv1, COND_AND1) {
    const char *text = "&&";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INVALID, "&"));
}

TEST_F(LexerTest_Lv1, COND_AND2) {
    const char *text = "&&";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COND_AND, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, COND_AND3) {
    const char *text = "&&";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COND_AND, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, COND_OR1) {
    const char *text = "||";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INVALID, "|"));
}

TEST_F(LexerTest_Lv1, COND_OR2) {
    const char *text = "||";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COND_OR, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, COND_OR3) {
    const char *text = "||";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COND_OR, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MATCH1) {
    const char *text = "=~";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, MATCH2) {
    const char *text = "=~";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MATCH, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, UNMATCH1) {
    const char *text = "!~";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(NOT, "!", COMMAND, "~", EOS, ""));
}

TEST_F(LexerTest_Lv1, UNMATCH2) {
    const char *text = "!~";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(UNMATCH, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, INC1) {
    const char *text = "++";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(PLUS, "+", PLUS, "+", EOS, ""));
}

TEST_F(LexerTest_Lv1, INC2) {
    const char *text = "++";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(INC, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, DEC1) {
    const char *text = "--";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MINUS, "-", MINUS, "-", EOS, ""));
}

TEST_F(LexerTest_Lv1, DEC2) {
    const char *text = "--";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(DEC, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, ASSIGN1) {
    const char *text = "=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, ASSIGN2) {
    const char *text = "=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ASSIGN, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, ADD_ASSIGN1) {
    const char *text = "+=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(PLUS, "+", COMMAND, "=", EOS, ""));
}

TEST_F(LexerTest_Lv1, ADD_ASSIGN2) {
    const char *text = "+=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ADD_ASSIGN, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, SUB_ASSIGN1) {
    const char *text = "-=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MINUS, "-", COMMAND, "=", EOS, ""));
}

TEST_F(LexerTest_Lv1, SUB_ASSIGN2) {
    const char *text = "-=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(SUB_ASSIGN, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MUL_ASSIGN1) {
    const char *text = "*=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, MUL_ASSIGN2) {
    const char *text = "*=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MUL_ASSIGN, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, DIV_ASSIGN1) {
    const char *text = "/=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, DIV_ASSIGN2) {
    const char *text = "/=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(DIV_ASSIGN, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, MOD_ASSIGN1) {
    const char *text = "%=";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, MOD_ASSIGN2) {
    const char *text = "%=";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(MOD_ASSIGN, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, AS1) {
    const char *text = "as";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, AS2) {
    const char *text = "as";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(AS, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, AS3) {
    const char *text = "as";
    this->initLexer(text, yycNAME);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IDENTIFIER, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, FUNC1) {
    const char *text = "Func";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, FUNC2) {
    const char *text = "Func";
    this->initLexer(text, yycTYPE);
    ASSERT_NO_FATAL_FAILURE(EXPECT(FUNC, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, FUNC3) {
    const char *text = "Func";
    this->initLexer(text, yycNAME);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IDENTIFIER, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, IN1) {
    const char *text = "in";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, IN2) {
    const char *text = "in";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IN, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, IN3) {
    const char *text = "in";
    this->initLexer(text, yycNAME);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IDENTIFIER, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, IS1) {
    const char *text = "is";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, IS2) {
    const char *text = "is";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IS, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, IS3) {
    const char *text = "is";
    this->initLexer(text, yycNAME);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IDENTIFIER, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, TYPEOF1) {
    const char *text = "typeof";
    this->initLexer(text, yycTYPE);
    ASSERT_NO_FATAL_FAILURE(EXPECT(TYPEOF, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, TYPEOF2) {
    const char *text = "typeof";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, NAME1) {
    const char *text = "assert";
    this->initLexer(text, yycNAME);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IDENTIFIER, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycEXPR));
}

TEST_F(LexerTest_Lv1, NAME2) {
    const char *text = "assert";
    this->initLexer(text, yycTYPE);
    ASSERT_NO_FATAL_FAILURE(EXPECT(IDENTIFIER, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, ACCESS1) {
    const char *text = ".";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, ACCESS2) {
    const char *text = ".";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ACCESSOR, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycNAME));
}

/**
 * new line, space and comment
 */
TEST_F(LexerTest_Lv1, LINE_END1) {
    const char *text = ";";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LINE_END, text, EOS, ""));
}

TEST_F(LexerTest_Lv1, LINE_END2) {
    const char *text = ";";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LINE_END, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, LINE_END3) {
    const char *text = ";";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(LINE_END, text, EOS, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertLexerMode(yycSTMT));
}

TEST_F(LexerTest_Lv1, COMMENT1) {
    const char *text = "#fhreuvrei o";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, COMMENT2) {
    const char *text = "#ああ  あ";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, COMMENT3) {
    const char *text = "#hello  \t  ";
    this->initLexer(text, yycNAME);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, COMMENT4) {
    const char *text = "#hferu";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycTYPE);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, COMMENT5) {
    const char *text = "#2345y;;::";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycCMD);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, EMPTY) {
const char *text = "";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE1) {
    const char *text = "    \t   \n  ";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE2) {
    const char *text = "   \n var \\\r\\\n";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(VAR, "var", EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE3) {
    const char *text = "\n  \n assert \\\r\\\n";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(ASSERT, "assert", EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE4) {
    const char *text = "\\\r\\\necho";
    this->initLexer(text);
    ASSERT_NO_FATAL_FAILURE(EXPECT(COMMAND, "echo", EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE5) {
    const char *text = "    \t   \n  \\\n\\\r ";
    this->initLexer(text, yycEXPR);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST_F(LexerTest_Lv1, SPACE6) {
    const char *text = "     \t    \n  \\\n\\\r    \t";
    this->initLexer(text);
    this->lexer->pushLexerMode(yycTYPE);
    ASSERT_NO_FATAL_FAILURE(EXPECT(EOS, ""));
}

TEST(LexerTest_Lv2, NEW_LINE) {
    std::string line = "  \n  \n   assert  \n ";
    Lexer lexer("(string)", ByteBuffer(line.c_str(), line.c_str() + line.size()), std::string());
    Token t;
    TokenKind k = lexer.nextToken(t);

    ASSERT_STREQ(TO_NAME(ASSERT), TO_NAME(k));
    ASSERT_TRUE(lexer.isPrevNewLine());

    k = lexer.nextToken(t);
    ASSERT_STREQ(TO_NAME(EOS), TO_NAME(k));
    ASSERT_TRUE(lexer.isPrevNewLine());
    k = lexer.nextToken(t);
    ASSERT_STREQ(TO_NAME(EOS), TO_NAME(k));
    ASSERT_TRUE(lexer.isPrevNewLine());
}

TEST(LexerTest_Lv2, NEW_LINE2) {
    std::string line = "  \n  var a";
    Lexer lexer("(string)", ByteBuffer(line.c_str(), line.c_str() + line.size()), std::string());
    Token t;
    TokenKind k = lexer.nextToken(t);

    ASSERT_STREQ(TO_NAME(VAR), TO_NAME(k));
    ASSERT_TRUE(lexer.isPrevNewLine());

    k = lexer.nextToken(t);
    ASSERT_STREQ(TO_NAME(IDENTIFIER), TO_NAME(k));
    ASSERT_FALSE(lexer.isPrevNewLine());

    k = lexer.nextToken(t);
    ASSERT_STREQ(TO_NAME(EOS), TO_NAME(k));
    ASSERT_TRUE(lexer.isPrevNewLine());
    ASSERT_FALSE(lexer.isPrevSpace());

    k = lexer.nextToken(t);
    ASSERT_STREQ(TO_NAME(EOS), TO_NAME(k));
    ASSERT_TRUE(lexer.isPrevNewLine());
    ASSERT_FALSE(lexer.isPrevSpace());
}

TEST(LexerTest_Lv3, IllegalChar) {
    unsigned char str[] = {0x82, 0};    // broken UTF-8 code

    ByteBuffer buf;
    buf.append((char*)str, arraySize(str));
    Lexer lexer("(string)", std::move(buf), std::string());
    Token t;
    TokenKind k = lexer.nextToken(t);

    ASSERT_STREQ(TO_NAME(INVALID), TO_NAME(k));
}

TEST(LineNumTest, case1) {
    LineNumTable lineNumTable;
    ASSERT_EQ(1u, lineNumTable.getOffset());
    ASSERT_EQ(1u, lineNumTable.lookup(12)); // empty
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
    table.addNewlinePos(4);  // overwrite
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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
