/*
 * Copyright (C) 2015 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PARSER_LEXER_H_
#define PARSER_LEXER_H_

#include <stdio.h>

#include <vector>

struct Token {
    unsigned int startPos;
    unsigned int size;
};

typedef enum {
    INVALID,
    EOS,

    // token kind definition.
    // reserved key word.
    ASSERT,
    BREAK,
    CATCH,
    CLASS,
    CONTINUE,
    DO,
    ELSE,
    EXTENDS,
    EXPORT_ENV,
    FINALLY,
    FOR,
    FUNCTION,
    IF,
    IMPORT_ENV,
    LET,
    NEW,
    NOT,
    RETURN,
    TRY,
    THROW,
    VAR,
    WHILE,

    // unary op
    PLUS,
    MINUS,

    // literal
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    OPEN_DQUOTE,
    BQUOTE_LITERAL,
    START_SUB_CMD,

    // applied name
    APPLIED_NAME,
    SPECIAL_NAME,
    VAR_NAME,

    // bracket
    LP, // (
    RP, // )
    LB, // [
    RB, // ]
    LBC, // {
    RBC, // }
    LA, // <
    RA, // >

    // command
    COMMAND,

    // separator
    COLON,
    COMMA,

    // binary op
    MUL,
    DIV,
    MOD,
    LE,
    GE,
    EQ,
    NE,
    AND,
    OR,
    XOR,
    COND_AND,
    COND_OR,
    RE_MATCH,
    RE_UNMATCH,

    // suffix op
    INC,
    DEC,

    // assign op
    ASSIGN,
    ADD_ASSIGN,
    SUB_ASSIGN,
    MUL_ASSIGN,
    DIV_ASSIGN,
    MOD_ASSIGN,

    // context dependent key word
    AS,
    FUNC,
    IN,
    IS,

    // identifier.
    IDENTIFIER,

    // accessor
    ACCESSOR,

    // line end
    LINE_END,
    NEW_LINE,


} TokenKind;

// binary op alias
#define ADD PLUS
#define SUB MINUS
#define LT LA
#define GT RA


typedef enum {
    yycSTMT,
    yycEXPR,
    yycDSTRING,
    yycCMD,
} LexerMode;

class Lexer {
private:
    /**
     * may be null, if input source is string. not closed it.
     */
    FILE *fp;

    unsigned int bufSize;

    /**
     * must terminate null character.
     */
    char *buf;

    /**
     * current reading pointer of buf.
     */
    char *cursor;

    /**
     * limit of buf.
     */
    char *limit;

    /**
     * for backtracking.
     */
    char *marker;

    unsigned int lineNum;

    /**
     * if fp == null, is true.
     * if fp reach end of file, is true.
     */
    bool endOfFile;

    std::vector<LexerMode> modeStack;

    const static unsigned int DEFAULT_SIZE = 256;

public:
    Lexer(unsigned int initSize, FILE *fp);

    /**
     * equivalent to Lexer(DEFAULT_SIZE, fp).
     */
    Lexer(FILE *fp);

    /**
     * in destructor call, must delete buf.
     * so, move the ownership of buf to this->buf.
     * buf must terminate null character.
     */
    Lexer(unsigned int size, char *buf);

    ~Lexer();

private:
    /**
     * if this->usedSize + needSize > this->maxSize, expand buf.
     */
    void expandBuf(unsigned int needSize);

public:
    /**
     * fill buffer. called from this->nextToken().
     */
    bool fill(int n);

    /**
     * get current reading position.
     */
    unsigned int getPos();

    unsigned int getBufSize();

    /**
     * used size of buf. must be this->getUsedSize() <= this->getBufSize().
     */
    unsigned int getUsedSize();

    /**
     * lexer entry point.
     * write next token to token.
     * return the kind of next token.
     */
    TokenKind nextToken(Token &token);

    unsigned int getLineNum();

    // token to value converting api.
    std::string toString(Token &token);
    int toInt(Token &token);
};

#endif /* PARSER_LEXER_H_ */
