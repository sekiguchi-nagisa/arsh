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
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <string>

#include <assert.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <misc/debug.h>

#include <parser/Token.h>
#include "Token.h"

#define EACH_LEXER_MODE(OP) \
    OP(yycSTMT) \
    OP(yycEXPR) \
    OP(yycNAME) \
    OP(yycDSTRING) \
    OP(yycCMD) \
    OP(yycTYPE)

namespace ydsh {
namespace parser {

typedef enum {
#define GEN_ENUM(ENUM) ENUM,
    EACH_LEXER_MODE(GEN_ENUM)
#undef GEN_ENUM
} LexerMode;

template<typename LEXER_DEF, typename TOKEN_KIND>
struct Lexer {
    /**
     * may be null, if input source is string. not closed it.
     */
    FILE *fp;

    unsigned int bufSize;

    /**
     * must terminate null character.
     */
    unsigned char *buf;

    /**
     * current reading pointer of buf.
     */
    unsigned char *cursor;

    /**
     * limit of buf.
     */
    unsigned char *limit;

    /**
     * for backtracking.
     */
    unsigned char *marker;

    /**
     * for trailing context.
     */
    unsigned char *ctxMarker;

    /**
     * initial value is 1.
     */
    unsigned int lineNum;

    /**
     * if fp is null or fp reach EOF, it it true.
     */
    bool endOfFile;

    /**
     * if true, reach end of string. nextToken() always return EOS.
     */
    bool endOfString;

    /**
     * default mode is yycSTMT
     */
    std::vector<LexerMode> modeStack;

    bool prevNewLine;

    const static unsigned int DEFAULT_SIZE = 256;

    static const char *lexerModeNames[];

    static LEXER_DEF lexerDef;

    Lexer(unsigned int initSize, bool fixed = false) :
            fp(0),
            bufSize((fixed || initSize > DEFAULT_SIZE) ? initSize : DEFAULT_SIZE),
            buf(new unsigned char[this->bufSize]),
            cursor(this->buf), limit(this->buf), marker(0), ctxMarker(0),
            lineNum(1), endOfFile(fixed), endOfString(false),
            modeStack(1, yycSTMT), prevNewLine(false) {
        this->buf[0] = '\0';    // terminate null character.
    }

    Lexer(unsigned int initSize, FILE *fp) :
            Lexer(initSize) {
        this->fp = fp;
    }

    /**
     * equivalent to Lexer(DEFAULT_SIZE, fp).
     */
    Lexer(FILE *fp) : Lexer(DEFAULT_SIZE, fp) {
    }

    /**
     * copy src to this->buf.
     * src must terminate null character.
     */
    Lexer(const char *src) :
            Lexer(strlen(src) + 1, true) {
        this->copySrcBuf(src);
    }

    /**
     * create copy of lexer.
     * internal state is initialized.
     * fp is always null.
     */
    Lexer(const Lexer &lexer) :
            Lexer(lexer.getUsedSize(), true) {
        this->copySrcBuf(lexer.buf);
    }

    ~Lexer() {
        delete[] this->buf;
        this->buf = 0;
    }

    /**
     * used for constructor. not use it.
     */
    void copySrcBuf(const void *srcBuf) {
        memcpy(this->buf, srcBuf, this->bufSize);
        this->limit += this->bufSize - 1;
    }

    /**
     * get current reading position.
     */
    unsigned int getPos() const {
        return this->cursor - this->buf;
    }

    void setPos(unsigned int pos) {
        if(this->buf + pos > this->limit) {
            fatal("too large position: %u\n", pos);
        }
        this->cursor = this->buf + pos;
    }

    /**
     * used size of buf. must be this->getUsedSize() <= this->getBufSize().
     */
    unsigned int getUsedSize() const {
        return this->limit - this->buf + 1;
    }

    bool isPrevNewLine() {
        return this->prevNewLine;
    }

    void setLineNum(unsigned int lineNum) {
        this->lineNum = lineNum;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    const char *getLexerModeName(LexerMode mode) const {
        return lexerModeNames[mode];
    }

    /**
     * lexer entry point.
     * write next token to token.
     * return the kind of next token.
     */
    TOKEN_KIND nextToken(Token &token);

    /**
     * if this->usedSize + needSize > this->maxSize, expand buf.
     */
    void expandBuf(unsigned int needSize);

    /**
     * fill buffer. called from this->nextToken().
     */
    bool fill(int n);

    // some token api

    /**
     * get line token which token belongs to.
     */
    Token getLineToken(const Token &token, bool skipEOS = false) const;

    Token getLineTokenImpl(const Token &token) const;

    // token to value converting api.
    /**
     * get text of token.
     */
    std::string toTokenText(const Token &token) const;

    /**
     * convert token to string (single quote string or double quote string)
     */
    std::string toString(const Token &token, bool isSingleQuote = true) const;

    /**
     * convert token to command argument
     * if expandTilde is true, the replace first tilde to HOME
     */
    std::string toCmdArg(const Token &token, bool expandTilde = false) const;

    /**
     * convert token to name(remove '$' char)
     * ex. $hoge, ${hoge}, hoge
     */
    std::string toName(const Token &token) const;

    /**
     * if converted number is out of range, status is 1.
     */
    char toInt8(const Token &token, int &status) const;

    unsigned char toUint8(const Token &token, int &status) const;
    short toInt16(const Token &token, int &status) const;
    unsigned short toUint16(const Token &token, int &status) const;

    /**
     * equivalent to toInt32().
     */
    int toInt(const Token &token, int &status) const;

    int toInt32(const Token &token, int &status) const;
    unsigned int toUint32(const Token &token, int &status) const;
    long toInt64(const Token &token, int &status) const;
    unsigned long toUint64(const Token &token, int &status) const;

    /**
     * if converted number is out of range, status is 1.
     */
    double toDouble(const Token &token, int &status) const;
};

//========== implementation ==============

template<typename LEXER_DEF, typename TOKEN_KIND>
const char *Lexer<LEXER_DEF, TOKEN_KIND>::lexerModeNames[] = {
#define GEN_NAME(ENUM) #ENUM,
        EACH_LEXER_MODE(GEN_NAME)
#undef GEN_NAME
#undef EACH_LEXER_MODE
};

template<typename LEXER_DEF, typename TOKEN_KIND>
LEXER_DEF Lexer<LEXER_DEF, TOKEN_KIND>::lexerDef;

template<typename LEXER_DEF, typename TOKEN_KIND>
TOKEN_KIND Lexer<LEXER_DEF, TOKEN_KIND>::nextToken(Token &token) {
    return lexerDef(this, token);
}

template<typename LEXER_DEF, typename TOKEN_KIND>
void Lexer<LEXER_DEF, TOKEN_KIND>::expandBuf(unsigned int needSize) {
    unsigned int usedSize = this->getUsedSize();
    unsigned int size = usedSize + needSize;
    if(size > this->bufSize) {
        unsigned int newSize = this->bufSize;
        do {
            newSize *= 2;
        } while(newSize < size);
        unsigned int pos = this->getPos();
        unsigned int markerPos = this->marker - this->buf;
        unsigned int ctxMarkerPos = this->ctxMarker - this->buf;
        unsigned char *newBuf = new unsigned char[newSize];
        memcpy(newBuf, this->buf, usedSize);
        delete[] this->buf;
        this->buf = newBuf;
        this->bufSize = newSize;
        this->cursor = this->buf + pos;
        this->limit = this->buf + usedSize - 1;
        this->marker = this->buf + markerPos;
        this->ctxMarker = this->buf + ctxMarkerPos;
    }
}

template<typename LEXER_DEF, typename TOKEN_KIND>
bool Lexer<LEXER_DEF, TOKEN_KIND>::fill(int n) {
    if(this->endOfString && this->limit - this->cursor <= 0) {
        return false;
    }

    if(!this->endOfFile) {
        int needSize = n - (this->limit - this->cursor);
        assert(needSize > -1);
        this->expandBuf(needSize);
        int readSize = fread(this->limit, sizeof(unsigned char), needSize, this->fp);
        this->limit += readSize;
        *this->limit = '\0';
        if(readSize < needSize) {
            this->endOfFile = true;
        }
    }
    return true;
}


#define CHECK_TOK(token) \
    assert(token.startPos < this->getUsedSize() &&\
            token.startPos + token.size <= this->getUsedSize())

template<typename LEXER_DEF, typename TOKEN_KIND>
Token Lexer<LEXER_DEF, TOKEN_KIND>::getLineToken(const Token &token, bool skipEOS) const {
    if(skipEOS && token.size == 0) {
        unsigned int startIndex = token.startPos;
        for(; startIndex > 0; startIndex--) {
            char ch = this->buf[startIndex];
            if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\000') {
                continue;
            }
            if(ch == '\\' && startIndex - 1 > 0 && this->buf[startIndex - 1] == '\n') {
                continue;
            }
            break;
        }
        Token skippedToken;
        skippedToken.startPos = startIndex;
        skippedToken.size = 0;
        return this->getLineTokenImpl(skippedToken);
    }
    return this->getLineTokenImpl(token);
}

template<typename LEXER_DEF, typename TOKEN_KIND>
Token Lexer<LEXER_DEF, TOKEN_KIND>::getLineTokenImpl(const Token &token) const {
    CHECK_TOK(token);

    // find start index of line.
    unsigned int startIndex;
    for(startIndex = token.startPos; startIndex > 0; startIndex--) {
        if(this->buf[startIndex] == '\n') {
            startIndex += (startIndex == token.startPos) ? 0 : 1;
            break;
        }
    }

    // find stop index of line
    unsigned int stopIndex;
    unsigned int usedSize = this->getUsedSize();
    for(stopIndex = token.startPos + token.size; stopIndex < usedSize; stopIndex++) {
        if(this->buf[stopIndex] == '\n') {
            break;
        }
    }
    Token lineToken;
    lineToken.startPos = startIndex;
    lineToken.size = stopIndex - startIndex;
    return lineToken;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
std::string Lexer<LEXER_DEF, TOKEN_KIND>::toTokenText(const Token &token) const {
    CHECK_TOK(token);
    return std::string((char *) (this->buf + token.startPos), token.size);
}

template<typename LEXER_DEF, typename TOKEN_KIND>
std::string Lexer<LEXER_DEF, TOKEN_KIND>::toString(const Token &token, bool isSingleQuote) const {
    CHECK_TOK(token);

    std::string str;
    str.reserve(token.size);

    unsigned int offset = isSingleQuote ? 1 : 0;
    unsigned int size = token.size - offset;
    for(unsigned int i = offset; i < size; i++) {
        char ch = this->buf[token.startPos + i];
        if(ch == '\\') {    // handle escape sequence
            char nextCh = this->buf[token.startPos + ++i];
            switch(nextCh) {
            case 'b' :
                ch = '\b';
                break;
            case 'f' :
                ch = '\f';
                break;
            case 'n' :
                ch = '\n';
                break;
            case 'r' :
                ch = '\r';
                break;
            case 't' :
                ch = '\t';
                break;
            case '\'':
                ch = '\'';
                break;
            case '"' :
                ch = '"';
                break;
            case '\\':
                ch = '\\';
                break;
            case '`' :
                ch = '`';
                break;
            case '$' :
                ch = '$';
                break;
            default:
                fatal("unexpected escape sequence: %c\n", nextCh);
                break;
            }
        }
        str += ch;
    }
    return str;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
std::string Lexer<LEXER_DEF, TOKEN_KIND>::toCmdArg(const Token &token, bool expandTilde) const {
    CHECK_TOK(token);

    std::string str;
    str.reserve(token.size);

    bool startWithTildeSlash = false;
    if(expandTilde) {
        if(token.size == 1 && this->buf[token.startPos] == '~') {
            return std::string(getenv("HOME"));
        }
        if(token.size > 1 && this->buf[token.startPos] == '~' && this->buf[token.startPos + 1] == '/') {
            str += getenv("HOME");
            str += '/';
            startWithTildeSlash = true;
        }
    }
    for(unsigned int i = startWithTildeSlash ? 2 : 0; i < token.size; i++) {
        char ch = this->buf[token.startPos + i];
        if(ch == '\\') {
            char nextCh = this->buf[token.startPos + ++i];
            switch(nextCh) {
            case '\n':
            case '\r':
                continue;
            default:
                ch = nextCh;
                break;
            }
        }
        str += ch;
    }
    return str;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
std::string Lexer<LEXER_DEF, TOKEN_KIND>::toName(const Token &token) const {
    CHECK_TOK(token);

    std::string name;
    name.reserve(token.size);
    for(unsigned int i = 0; i < token.size; i++) {
        char ch = this->buf[token.startPos + i];
        switch(ch) {
        case '$':
        case '{':
        case '}':
            continue;
        default:
            name += ch;
            break;
        }
    }
    return name;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
char Lexer<LEXER_DEF, TOKEN_KIND>::toInt8(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT8_MAX || value < INT8_MIN) {
        status = 1;
        return 0;
    }
    return (char) value;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
unsigned char Lexer<LEXER_DEF, TOKEN_KIND>::toUint8(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT8_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned char) value;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
short Lexer<LEXER_DEF, TOKEN_KIND>::toInt16(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT16_MAX || value < INT16_MIN) {
        status = 1;
        return 0;
    }
    return (short) value;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
unsigned short Lexer<LEXER_DEF, TOKEN_KIND>::toUint16(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT16_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned short) value;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
int Lexer<LEXER_DEF, TOKEN_KIND>::toInt(const Token &token, int &status) const {
    return this->toInt32(token, status);
}

template<typename LEXER_DEF, typename TOKEN_KIND>
int Lexer<LEXER_DEF, TOKEN_KIND>::toInt32(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > INT32_MAX || value < INT32_MIN) {
        status = 1;
        return 0;
    }
    return (int) value;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
unsigned int Lexer<LEXER_DEF, TOKEN_KIND>::toUint32(const Token &token, int &status) const {
    long value = this->toInt64(token, status);
    if(value > UINT32_MAX || value < 0) {
        status = 1;
        return 0;
    }
    return (unsigned int) value;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
long Lexer<LEXER_DEF, TOKEN_KIND>::toInt64(const Token &token, int &status) const {
    CHECK_TOK(token);

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.startPos + i];
    }
    str[token.size] = '\0';

    // convert to int
    char *end;
    const long value = strtol(str, &end, 10);

    // check error
    if(end == str) {
        fatal("cannot covert to int: %s\n", str);
    }
//    if(*end != '\0') {
//        fatal("found illegal character in num: %s\n", str);
//    }
    if((value == LONG_MIN || value == LONG_MAX) && errno == ERANGE) {
        status = 1;
        return 0;
    }
    status = 0;
    return value;
}

template<typename LEXER_DEF, typename TOKEN_KIND>
unsigned long Lexer<LEXER_DEF, TOKEN_KIND>::toUint64(const Token &token, int &status) const {
    CHECK_TOK(token);

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.startPos + i];
    }
    str[token.size] = '\0';

    // convert to int
    char *end;
    const long long value = strtoll(str, &end, 10);

    // check error
    if(end == str) {
        fatal("cannot covert to int: %s\n", str);
    }
    if((value == LLONG_MIN || value == LLONG_MAX) && errno == ERANGE) {
        status = 1;
        return 0;
    }
    if(value > UINT64_MAX || value < 0) {
        status = 1;
        return 0;
    }
    status = 0;
    return (unsigned long) value;
}



template<typename LEXER_DEF, typename TOKEN_KIND>
double Lexer<LEXER_DEF, TOKEN_KIND>::toDouble(const Token &token, int &status) const {
    CHECK_TOK(token);

    char str[token.size + 1];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.startPos + i];
    }
    str[token.size] = '\0';

    // convert to double
    char *end;
    double value = strtod(str, &end);

    // check error
    if(value == 0 && end == str) {
        fatal("cannot convert to double: %s\n", str);
    }
    if(*end != '\0') {
        fatal("found illegal character in num: %s\n", str);
    }
    if(value == 0 && errno == ERANGE) {
        status = 1;
        return 0;
    }
    if((value == HUGE_VAL || value == -HUGE_VAL) && errno == ERANGE) {
        status = 1;
        return 0;
    }
    status = 0;
    return value;
}

//==== default lexer definition ======
struct LexerDef {
    TokenKind operator()(Lexer<LexerDef, TokenKind> *lexer, Token &token) const;
};

} // namespace parser
} // namespace ydsh

#endif /* PARSER_LEXER_H_ */
