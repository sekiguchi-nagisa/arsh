#include <gtest/gtest.h>
#include <stdio.h>
#include <util/debug.h>

#include <parser/Lexer.h>
#include <core/DSType.h>

#include <string.h>
#include <vector>

#ifndef LEXER_TEST_DIR
#define LEXER_TEST_DIR "./"
#endif

/**
 * ############################
 * #  Lv0: test file loading  #
 * ############################
 */
TEST(LexerTest_Lv0, case1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        FILE *fp = fopen(LEXER_TEST_DIR "lexer_test.cpp", "r");
        ASSERT_FALSE(fp == 0);
    });
}

/**
 * ##################################################
 * #  Lv1: statement token and Lexer::toString().  ##
 * ##################################################
 */

#define DUP(text) text "    \t  \t\t  " text

class LexerTest_Lv1 : public ::testing::Test {
public:
    Lexer *lexer;
    std::vector<std::pair<TokenKind, Token>> tokens;

public:
    LexerTest_Lv1() :
        lexer(), tokens() {
    }

    virtual ~LexerTest_Lv1() {
        delete this->lexer;
        this->lexer = 0;
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    // for test
    virtual void initLexer(const char *text) {
        this->lexer = new Lexer(text);
    }

    virtual const std::vector<std::pair<TokenKind, Token>> &getTokens() {
        return this->tokens;
    }

    virtual void tokenize() {
        SCOPED_TRACE("");

        Token t;
        TokenKind k = EOS;
        do {
            SCOPED_TRACE("");
            k = this->lexer->nextToken(t);
            this->tokens.push_back(std::make_pair(k, t));
        } while(k != EOS && k != INVALID);
    }

    virtual void assertToken(unsigned int index, TokenKind expectedKind, const char *expectedText) {
        SCOPED_TRACE("");
        ASSERT_TRUE(index < this->tokens.size());
        auto pair = this->tokens[index];
        ASSERT_EQ(expectedKind, pair.first);

        ASSERT_EQ(strlen(expectedText), pair.second.size);
        std::string text = this->lexer->toString(pair.second);
        ASSERT_STREQ(expectedText, text.c_str());
    }
};

TEST_F(LexerTest_Lv1, assert_tok) {
#define TEXT "assert"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, ASSERT, TEXT);
        this->assertToken(1, ASSERT, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, catch_tok) {
#define TEXT "catch"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, CATCH, TEXT);
        this->assertToken(1, CATCH, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, class_tok) {
#define TEXT "class"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, CLASS, TEXT);
        this->assertToken(1, CLASS, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, continue_tok) {
#define TEXT "continue"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, CONTINUE, TEXT);
        this->assertToken(1, CONTINUE, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, do_tok) {
#define TEXT "do"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, DO, TEXT);
        this->assertToken(1, DO, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, else_tok) {
#define TEXT "else"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, ELSE, TEXT);
        this->assertToken(1, ELSE, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, extends_tok) {
#define TEXT "extends"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, EXTENDS, TEXT);
        this->assertToken(1, EXTENDS, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, exportenv_tok) {
#define TEXT "export-env"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, EXPORT_ENV, TEXT);
        this->assertToken(1, EXPORT_ENV, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, finally_tok) {
#define TEXT "finally"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, FINALLY, TEXT);
        this->assertToken(1, FINALLY, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, for_tok) {
#define TEXT "for"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, FOR, TEXT);
        this->assertToken(1, FOR, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, function_tok) {
#define TEXT "function"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, FUNCTION, TEXT);
        this->assertToken(1, FUNCTION, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, if_tok) {
#define TEXT "if"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, IF, TEXT);
        this->assertToken(1, IF, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, importenv_tok) {
#define TEXT "import-env"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, IMPORT_ENV, TEXT);
        this->assertToken(1, IMPORT_ENV, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, let_tok) {
#define TEXT "let"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, LET, TEXT);
        this->assertToken(1, LET, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, new_tok) {
#define TEXT "new"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, NEW, TEXT);
        this->assertToken(1, NEW, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, not_tok) {
#define TEXT "not"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, NOT, TEXT);
        this->assertToken(1, NOT, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, return_tok) {
#define TEXT "return"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, RETURN, TEXT);
        this->assertToken(1, RETURN, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, try_tok) {
#define TEXT "try"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, TRY, TEXT);
        this->assertToken(1, TRY, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, throw_tok) {
#define TEXT "throw"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, THROW, TEXT);
        this->assertToken(1, THROW, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, var_tok) {
#define TEXT "var"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, VAR, TEXT);
        this->assertToken(1, VAR, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, while_tok) {
#define TEXT "while"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, WHILE, TEXT);
        this->assertToken(1, WHILE, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, plus_tok) {
#define TEXT "+"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, PLUS, TEXT);
        this->assertToken(1, PLUS, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, minus_tok) {
#define TEXT "-"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        ASSERT_EQ(this->getTokens().size(), 3);
        this->assertToken(0, MINUS, TEXT);
        this->assertToken(1, MINUS, TEXT);
        ASSERT_EQ(EOS, this->getTokens()[2].first);
    });
#undef TEXT
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
