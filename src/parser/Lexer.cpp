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

#include <assert.h>
#include <string.h>
#include <string>

#include <util/debug.h>
#include <parser/Lexer.h>

/**
 * the implementation of Lexer::nextToken() exists
 * at src/nextToken.re2c.cpp
 */


// ###################
// ##     Lexer     ##
// ###################

Lexer::Lexer(unsigned int initSize, bool fixed) :
        fp(0),
        bufSize((fixed || initSize > DEFAULT_SIZE) ? initSize : DEFAULT_SIZE),
        buf(new unsigned char[this->bufSize]),
        cursor(this->buf), limit(this->buf), marker(0),
        lineNum(1), endOfFile(false), modeStack(1, yycSTMT), prevNewLine(false) {
    this->buf[0] = '\0';    // terminate null character.
}

Lexer::Lexer(unsigned int initSize, FILE *fp) :
        Lexer(initSize) {
    this->fp = fp;
}

Lexer::Lexer(FILE *fp) : Lexer(DEFAULT_SIZE, fp) {
}

Lexer::Lexer(const char *src) :
        Lexer(strlen(src) + 1, true) {
    this->copySrcBuf(src);
}

Lexer::Lexer(const Lexer &lexer) :
        Lexer(lexer.getUsedSize(), true) {
    this->copySrcBuf(lexer.buf);
}

Lexer::~Lexer() {
    delete[] this->buf;
    this->buf = 0;
}

void Lexer::copySrcBuf(const void *srcBuf) {
    memcpy(this->buf, srcBuf, this->bufSize);
    this->limit += this->bufSize - 1;
}

void Lexer::expandBuf(unsigned int needSize) {
    unsigned int usedSize = this->getUsedSize();
    unsigned int size = usedSize + needSize;
    if(size > this->bufSize) {
        unsigned int newSize = this->bufSize;
        do {
            newSize *= 2;
        } while(newSize < size);
        unsigned int pos = this->getPos();
        unsigned int markerPos = this->marker - this->buf;
        unsigned char *newBuf = new unsigned char[newSize];
        memcpy(newBuf, this->buf, usedSize);
        delete[] this->buf;
        this->buf = newBuf;
        this->bufSize = newSize;
        this->cursor = this->buf + pos;
        this->limit = this->buf + usedSize - 1;
        this->marker = this->buf + markerPos;
    }
}

bool Lexer::fill(int n) {
    if(this->endOfFile && (this->limit - this->cursor) <= 0) {
        return false;
    }

    if(this->fp == 0) {
        if(this->limit - this->cursor <= 0) {
            this->endOfFile = true;
        }
        return true;
    }
    int needSize = n - (this->limit - this->cursor);
    assert(needSize > -1);
    this->expandBuf(needSize);
    int readSize = fread(this->limit, needSize, 1, this->fp);
    this->limit += readSize;
    *this->limit = '\0';
    if(readSize < needSize) {
        this->endOfFile = true;
    }
    return true;
}

unsigned int Lexer::getPos() const {
    return this->cursor - this->buf;
}

unsigned int Lexer::getBufSize() const {
    return this->bufSize;
}

unsigned int Lexer::getUsedSize() const {
    return this->limit - this->buf + 1;
}

bool Lexer::isPrevNewLine() {
    return this->prevNewLine;
}

void Lexer::setLineNum(unsigned int lineNum) {
    this->lineNum = lineNum;
}

unsigned int Lexer::getLineNum() const {
    return this->lineNum;
}

#define CHECK_TOK(token) \
    assert(token.startPos < this->getUsedSize() &&\
            token.startPos + token.size <= this->getUsedSize())

Token Lexer::getLineToken(Token &token) {
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
            stopIndex -= (stopIndex == token.startPos + token.size) ? 0 : 1;
            break;
        }
    }
    Token lineToken;
    lineToken.startPos = startIndex;
    lineToken.size = stopIndex - startIndex;
    return lineToken;
}

std::string Lexer::toTokenText(Token &token) {
    CHECK_TOK(token);
    return std::string((char*)(this->buf + token.startPos), token.size);
}

std::string Lexer::toString(Token &token, bool isSingleQuote) {
    CHECK_TOK(token);

    std::string str;
    str.reserve(token.size);

    unsigned int offset = isSingleQuote ? 1 : 0;
    unsigned int size = token.size - offset;
    for(unsigned int i = 0; i < size; i++) {
        char ch = this->buf[token.startPos + i];
        if(ch == '\\') {    // handle escape sequence
            char nextCh = this->buf[token.startPos + ++i];
            switch(nextCh) {
            case 'b' : ch = '\b'; break;
            case 'f' : ch = '\f'; break;
            case 'n' : ch = '\n'; break;
            case 'r' : ch = '\r'; break;
            case 't' : ch = '\t'; break;
            case '\'': ch = '\''; break;
            case '"' : ch = '"' ; break;
            case '\\': ch = '\\'; break;
            case '`' : ch = '`' ; break;
            case '$' : ch = '$' ; break;
            default:
                fatal("unexpected escape sequence: %c\n", nextCh);
                break;
            }
        }
        str += ch;
    }
    return str;
}

std::string Lexer::toCmdArg(Token &token) {
    CHECK_TOK(token);

    std::string str;
    str.reserve(token.size);

    for(unsigned int i = 0; i < token.size; i++) {
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

std::string Lexer::toName(Token &token) {
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

int Lexer::toInt(Token &token) {
    CHECK_TOK(token);

    char str[token.size];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.startPos + i];
    }
    return atoi(str);
}

double Lexer::toDouble(Token &token) {
    CHECK_TOK(token);

    char str[token.size];
    for(unsigned int i = 0; i < token.size; i++) {
        str[i] = this->buf[token.startPos + i];
    }
    return atof(str);
}
