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

struct Token {
    unsigned int startPos;
    unsigned int size;
};

typedef enum {
    INVALID,
} TokenKind;

class Lexer {
private:
    /**
     * may be null, if input source is string. not closed it.
     */
    FILE *fp;

    /**
     * used size of buffer. must be usedSize <= maxSize
     */
    unsigned int usedSize;

    unsigned int maxSize;

    /**
     * must terminate null character.
     */
    char *buf;

    unsigned int lineNum;

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
     * append to this->buf.
     * if this->usedSize + size > this->size, expand buf.
     * b must not terminate null character.
     */
    void appendToBuf(unsigned int size, char *b);

public:
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
