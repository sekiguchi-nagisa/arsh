#include <gtest/gtest.h>

#include <parser/Lexer.h>
#include <core/DSType.h>
#include <misc/unused.h>

#ifndef LEXER_TEST_DIR
#define LEXER_TEST_DIR "./"
#endif

using namespace ydsh::core;
using namespace ydsh::parser;

/**
 * ############################
 * #  Lv0: test file loading  #
 * ############################
 */
TEST(LexerTest_Lv0, case1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        FILE *fp = fopen(LEXER_TEST_DIR  "/" "lexer_test.cpp", "r");
        ASSERT_FALSE(fp == 0);
    });
}

/**
 * #####################################################
 * #  Lv1: statement token and Lexer::toTokenText().  ##
 * #####################################################
 */

#define DUP(text) text "    \t  \t\t  " text

class LexerTest_Lv1 : public ::testing::Test {
public:
    Lexer *lexer;
    std::vector<std::pair<TokenKind, Token>> tokens;

public:
    LexerTest_Lv1() = default;

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
        TokenKind k;
        do {
            SCOPED_TRACE("");
            this->lexer->nextToken(t);
            k = t.kind;
            this->tokens.push_back(std::make_pair(k, t));
        } while(k != EOS && k != INVALID);
    }

    virtual void assertKind(TokenKind expected, TokenKind actual) {
        SCOPED_TRACE("");
        ASSERT_STREQ(TO_NAME(expected), TO_NAME(actual));
    }

    virtual void assertToken(unsigned int index, TokenKind expectedKind, const char *expectedText) {
        SCOPED_TRACE("");
        ASSERT_TRUE(index < this->tokens.size());
        auto pair = this->tokens[index];
        this->assertKind(expectedKind, pair.first);

        std::string text = this->lexer->toTokenText(pair.second);
        ASSERT_STREQ(expectedText, text.c_str());
    }

    virtual void assertTokens(std::vector<std::pair<TokenKind, const char *>> expectedList) {
        SCOPED_TRACE("");

        const unsigned int size = expectedList.size();
        ASSERT_EQ(size, this->tokens.size());

        for(unsigned int i = 0; i < size; i++) {
            this->assertToken(i, expectedList[i].first, expectedList[i].second);
        }
    }
};

typedef std::vector<std::pair<TokenKind, const char *>> ExpectedList;

void addPair(ExpectedList &list) {
    UNUSED(list);
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
};

#define EXPECT(...) this->assertTokens(expect(__VA_ARGS__))

TEST_F(LexerTest_Lv1, assert_tok) {
#define TEXT "assert"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(ASSERT, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, break_tok) {
#define TEXT "break"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(BREAK, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, catch_tok) {  // only available EXPR mode
#define TEXT "catch"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->lexer->setLexerMode(yycEXPR);
        this->tokenize();
        EXPECT(CATCH, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, class_tok) {
#define TEXT "class"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(CLASS, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, continue_tok) {
#define TEXT "continue"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(CONTINUE, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, do_tok) {
#define TEXT "do"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(DO, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, else_tok) {   // only available EXPR mode
#define TEXT "else"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->lexer->setLexerMode(yycEXPR);
        this->tokenize();
        EXPECT(ELSE, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, exportenv_tok) {
#define TEXT "export-env"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(EXPORT_ENV, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, finally_tok) {    // only available EXPR mode
#define TEXT "finally"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->lexer->setLexerMode(yycEXPR);
        this->tokenize();
        EXPECT(FINALLY, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, for_tok) {
#define TEXT "for"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(FOR, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, function_tok) {
#define TEXT "function"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(FUNCTION, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, if_tok) {
#define TEXT "if"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(IF, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, importenv_tok) {
#define TEXT "import-env"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(IMPORT_ENV, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, let_tok) {
#define TEXT "let"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(LET, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, new_tok) {
#define TEXT "new"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(NEW, TEXT, NEW, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, not_tok) {
#define TEXT "not"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(NOT, TEXT, NOT, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, return_tok) {
#define TEXT "return"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(RETURN, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, try_tok) {
#define TEXT "try"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(TRY, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, throw_tok) {
#define TEXT "throw"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(THROW, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, var_tok) {
#define TEXT "var"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(VAR, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, while_tok) {
#define TEXT "while"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(WHILE, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, plus_tok) {
#define TEXT "+"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(PLUS, TEXT, PLUS, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, minus_tok) {
#define TEXT "-"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(MINUS, TEXT, MINUS, TEXT, EOS, "");
    });
#undef TEXT
}

/**
 * literal test
 */
// integer literal
TEST_F(LexerTest_Lv1, int_literal1) {
#define TEXT "0"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(INT_LITERAL, TEXT, INT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, int_literal2) {
#define TEXT "123408"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(INT_LITERAL, TEXT, INT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, int_literal3) {
#define TEXT "9"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(INT_LITERAL, TEXT, INT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, int_literal4) {
#define TEXT "759801"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(INT_LITERAL, TEXT, INT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

// invalid int literal
TEST_F(LexerTest_Lv1, invaild_int_literal) {
#define TEXT "014"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(INT_LITERAL, "0", INT_LITERAL, "14", INT_LITERAL, "0", INT_LITERAL, "14", EOS, "");
    });
#undef TEXT
}

// float literal
TEST_F(LexerTest_Lv1, float_literal1) {
#define TEXT "0.010964"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(FLOAT_LITERAL, TEXT, FLOAT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, float_literal2) {
#define TEXT "103.0109640"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(FLOAT_LITERAL, TEXT, FLOAT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, float_literal3) {
#define TEXT "0.010964e0"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(FLOAT_LITERAL, TEXT, FLOAT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, float_literal4) {
#define TEXT "12.010964E-102"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(FLOAT_LITERAL, TEXT, FLOAT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, float_literal5) {
#define TEXT "0.00"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(FLOAT_LITERAL, TEXT, FLOAT_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

// invalid float literal
TEST_F(LexerTest_Lv1, invalid_float_literal1) {
#define TEXT "0.010964e+01"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(FLOAT_LITERAL, "0.010964e+0", INT_LITERAL, "1",
               FLOAT_LITERAL, "0.010964e+0", INT_LITERAL, "1", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, invalid_float_literal2) {
#define TEXT "0012.04e-78"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(INT_LITERAL, "0", INT_LITERAL, "0",
               FLOAT_LITERAL, "12.04e-78", INT_LITERAL, "0",
               INT_LITERAL, "0", FLOAT_LITERAL, "12.04e-78",EOS, "");
    });
#undef TEXT
}

// string literal
TEST_F(LexerTest_Lv1, string_literal1) {
#define TEXT "''"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(STRING_LITERAL, TEXT, STRING_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_literal2) {
#define TEXT "'fhrωu4あ\t3\"5^*&!@#~AFG '"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(STRING_LITERAL, TEXT, STRING_LITERAL, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_literal3) {
#define TEXT "'\\t\\n\\r\\\\'"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(STRING_LITERAL, "'\\t\\n\\r\\\\'", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_literal4) {
#define TEXT "$'\\''"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(STRING_LITERAL, "$'\\''", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_literal5) {
#define TEXT "$'\\n'"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(STRING_LITERAL, "$'\\n'", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_literal6) {
#define TEXT "$'\\\\'"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(STRING_LITERAL, "$'\\\\'", EOS, "");
    });
#undef TEXT
}


// invalid string literal
TEST_F(LexerTest_Lv1, invalid_string_literal) {
#define TEXT "'\\''"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(STRING_LITERAL, "'\\'", INVALID, "'");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_expr1) {
#define TEXT "\"hello word\""
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello word", CLOSE_DQUOTE, "\"", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_expr2) {
#define TEXT "\"hello ${a} word\""
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello ",
               APPLIED_NAME, "${a}", STR_ELEMENT, " word",
               CLOSE_DQUOTE, "\"", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_expr3) {
#define TEXT "\"hello\\\"world\""
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello\\\"world",
               CLOSE_DQUOTE, "\"", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_expr4) {
#define TEXT "\"hello\\$world\""
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello\\$world",
               CLOSE_DQUOTE, "\"", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, string_expr5) {
#define TEXT "\"\\\\\""
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "\\\\",
               CLOSE_DQUOTE, "\"", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, invalid_string_expr) {
#define TEXT "\"hello$\""
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(OPEN_DQUOTE, "\"", STR_ELEMENT, "hello", INVALID, "$");
    });
#undef TEXT
}

// applied name
TEST_F(LexerTest_Lv1, appliedName1) {
#define TEXT "$w10i_fArhue"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(APPLIED_NAME, TEXT, APPLIED_NAME, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, appliedName2) {
#define TEXT "$__0"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(APPLIED_NAME, TEXT, APPLIED_NAME, TEXT, EOS, "");
    });
#undef TEXT
}

// special name
TEST_F(LexerTest_Lv1, specialName) {
#define TEXT "$@"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(SPECIAL_NAME, TEXT, SPECIAL_NAME, TEXT, EOS, "");
    });
#undef TEXT
}

/**
 * brace test
 */
TEST_F(LexerTest_Lv1, LP) {
#define TEXT "("
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(LP, TEXT, LP, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, RP) {
#define TEXT ")"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(INVALID, ")");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, LB) {
#define TEXT "["
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(LB, TEXT, LB, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, RB) {
#define TEXT "]"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(RB, TEXT, RB, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, LBC) {
#define TEXT "{"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(LBC, TEXT, LBC, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, RBC) {
#define TEXT "}"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "}");
    });
#undef TEXT
}

/*
 * command token
 */
TEST_F(LexerTest_Lv1, CMD1) {
#define TEXT "\\assert"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, "\\assert", LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, CMD2) {
#define TEXT "\\ \\t\\r\\n\\;\\'\\\"\\`\\|\\&\\<\\>\\(\\)\\{\\}\\$\\#\\!\\[\\]\\8"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, CMD3) {
#define TEXT "あ漢ω"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, CMD4) {
#define TEXT "l\\"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, "l\\\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, CMD5) {
#define TEXT "\\"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(EOS, "");
    });
#undef TEXT
}


/**
 * test expr token in stmt mode.
 */
TEST_F(LexerTest_Lv1, COLON) {
#define TEXT ":"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, COMMA) {
#define TEXT ","
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, MUL) {
#define TEXT "*"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, DIV) {
#define TEXT "/"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, MOD) {
#define TEXT "%"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, LT) {
#define TEXT "<"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "<");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, GT) {
#define TEXT ">"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, ">");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, LE) {
#define TEXT "<="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "<");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, GE) {
#define TEXT ">="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, ">");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, EQ) {
#define TEXT "=="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, NE) {
#define TEXT "!="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "!");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, AND) {
#define TEXT "&"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "&");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, OR) {
#define TEXT "|"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "|");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, XOR) {
#define TEXT "^"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, COND_AND) {
#define TEXT "&&"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "&");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, COND_OR) {
#define TEXT "||"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "|");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, RE_MATCH) {
#define TEXT "=~"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, RE_UNMATCH) {
#define TEXT "!~"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(INVALID, "!");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, INC) {
#define TEXT "++"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(PLUS, "+", PLUS, "+", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, DEC) {
#define TEXT "--"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(MINUS, "-", MINUS, "-", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, ASSIGN) {
#define TEXT "="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, ADD_ASSIGN) {
#define TEXT "+="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(PLUS, "+", COMMAND, "=", LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, SUB_ASSIGN) {
#define TEXT "-="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(MINUS, "-", COMMAND, "=", LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, MUL_ASSIGN) {
#define TEXT "*="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, DIV_ASSIGN) {
#define TEXT "/="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, MOD_ASSIGN) {
#define TEXT "%="
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, AS) {
#define TEXT "as"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, FUNC) {
#define TEXT "Func"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, IN) {
#define TEXT "in"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, IS) {
#define TEXT "is"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, ACCESS) {
#define TEXT "."
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, TEXT, LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

/**
 * new line, space and comment
 */
TEST_F(LexerTest_Lv1, LINE_END) {
#define TEXT ";"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(LINE_END, TEXT, LINE_END, TEXT, EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, COMMENT) {
#define TEXT "#fhreuvrei o"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, SPACE1) {
#define TEXT ""
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(DUP(TEXT));
        this->tokenize();
        EXPECT(EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, SPACE2) {
#define TEXT "   \n var \\\r\\\n"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(VAR, "var", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, SPACE3) {
#define TEXT "\n  \n assert \\\r\\\n"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(ASSERT, "assert", EOS, "");
    });
#undef TEXT
}

TEST_F(LexerTest_Lv1, SPACE4) {
#define TEXT "\\\r\\\necho"
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->initLexer(TEXT);
        this->tokenize();
        EXPECT(COMMAND, "echo", LINE_END, "\n", EOS, "");
    });
#undef TEXT
}

TEST(LexerTest_Lv2, NEW_LINE) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        Lexer lexer("  \n  \n   assert  \n ");
        Token t;
        TokenKind k;
        lexer.nextToken(t);
        k = t.kind;
        ASSERT_STREQ(TO_NAME(ASSERT), TO_NAME(k));
        ASSERT_TRUE(lexer.isPrevNewLine());

        lexer.nextToken(t);
        k = t.kind;
        ASSERT_STREQ(TO_NAME(EOS), TO_NAME(k));
        ASSERT_TRUE(lexer.isPrevNewLine());

        lexer.nextToken(t);
        k = t.kind;
        ASSERT_STREQ(TO_NAME(EOS), TO_NAME(k));
        ASSERT_FALSE(lexer.isPrevNewLine());
    });
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
