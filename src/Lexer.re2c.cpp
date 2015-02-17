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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <string>

#include <parser/Lexer.h>

Lexer::Lexer(unsigned int initSize, FILE *fp) :
        fp(fp),
        bufSize(initSize < DEFAULT_SIZE ? DEFAULT_SIZE : initSize),
        buf(new char[this->bufSize]),
        cursor(this->buf), limit(this->buf), marker(0),
        lineNum(0), endOfFile(false), modeStack(1),
        stmtMode(true), enterStmt(false) {
    assert(fp != 0);
    this->buf[0] = '\0';    // terminate null character.
    this->modeStack.push_back(DEFAULT);
}

Lexer::Lexer(FILE *fp) : Lexer(DEFAULT_SIZE, fp) {
}

Lexer::Lexer(unsigned int size, char *buf) :
        fp(0), bufSize(size), buf(buf),
        cursor(buf), limit(buf + size - 1), marker(0),
        lineNum(0), endOfFile(true), modeStack(1),
        stmtMode(true), enterStmt(false) {
    this->modeStack.push_back(DEFAULT);
}

Lexer::~Lexer() {
    delete[] this->buf;
    this->buf = 0;
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
        char *newBuf = new char[newSize];
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
    if(this->endOfFile) {
        return true;    // already filled.
    }
    int needSize = n - (this->limit - this->cursor);
    int readSize = fread(this->limit, needSize, 1, this->fp);
    this->limit += readSize;
    *this->limit = '\0';
    if(readSize < needSize) {
        this->endOfFile = true;
    }
    return true;
}

unsigned int Lexer::getPos() {
    return this->cursor - this->buf;
}

unsigned int Lexer::getBufSize() {
    return this->bufSize;
}

unsigned int Lexer::getUsedSize() {
    return this->limit - this->buf + 1;
}

#define POP \
    do {\
        if(this->modeStack.size() > 1) {\
            this->modeStack.pop_back();\
        }\
    } while(0)

#define PUSH(m) this->modeStack.push_back(m)

#define RET(k) do { kind = k; goto END; } while(0)

#define SKIP goto INIT

TokenKind Lexer::nextToken(Token &token) {
    /*!re2c
      re2c:define:YYCTYPE = "char";
      re2c:define:YYCURSOR = this->cursor;
      re2c:define:YYLIMIT = this->limit;
      re2c:define:YYMARKER = this->marker;
      re2c:define:YYFILL:naked = 1;
      re2c:define:YYFILL@len = #;
      re2c:define:YYFILL = "if (!this->fill(#)) { RET(EOS); }";
      re2c:yyfill:enable = 1;
      re2c:indent:top = 1;
      re2c:indent:string = "    ";

      VAR_NAME = [a-zA-Z] [_0-9a-zA-Z]* | '_' [_0-9a-zA-Z]+;
      SKIP  = '\\' [\r\n] | [ \t];
      OTHER = .;
    */

INIT:
    unsigned int startPos = this->getPos();
    TokenKind kind = INVALID;
    if(this->modeStack.size() < 1) {
        RET(INVALID);
    }
    switch(this->modeStack.back()) {    //TODO:
    case DEFAULT:
        break;
    case NAME:
        /*!re2c
          VAR_NAME               { RET(VAR_NAME); }
          SKIP+                  { SKIP; }
          OTHER                  { RET(INVALID);  }
        */
        break;
    case DSTRING:
        break;
    case CMD:
        break;
    }

END:
    this->stmtMode = this->enterStmt;
    this->enterStmt = false;
    token.startPos = startPos;
    token.size = this->getPos() - startPos;
    return kind;
}

unsigned int Lexer::getLineNum() {
    return this->lineNum;
}

std::string Lexer::toString(Token &token) {
    assert(token.startPos < this->getUsedSize() &&
            token.startPos + token.size <= this->getUsedSize());
    return std::string(this->buf[token.startPos], token.size);
}

int Lexer::toInt(Token &token) {
    return 0;   //FIXME:
}

