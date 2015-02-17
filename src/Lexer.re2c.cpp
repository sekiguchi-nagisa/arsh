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
        lineNum(0), endOfFile(false), modeStack(1) {
    assert(fp != 0);
    this->buf[0] = '\0';    // terminate null character.
    this->modeStack.push_back(yycSTMT);
}

Lexer::Lexer(FILE *fp) : Lexer(DEFAULT_SIZE, fp) {
}

Lexer::Lexer(unsigned int size, char *buf) :
        fp(0), bufSize(size), buf(buf),
        cursor(buf), limit(buf + size - 1), marker(0),
        lineNum(0), endOfFile(true), modeStack(1) {
    this->modeStack.push_back(yycSTMT);
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

// helper macro definition.
#define RET(k) do { kind = k; goto END; } while(0)

#define SKIP() goto INIT

#define ERROR() do { RET(INVALID); } while(0)

#define POP_MODE() \
    do {\
        if(this->modeStack.size() > 1) {\
            this->modeStack.pop_back();\
        } else {\
            ERROR();\
        }\
    } while(0)

#define PUSH_MODE(m) this->modeStack.push_back(yyc ## m)

#define MODE(m) \
    do {\
        if(this->modeStack.size() > 0) {\
            this->modeStack[this->modeStack.size() - 1] = yyc ## m;\
        } else {\
            ERROR();\
        }\
    } while(0)

#define INC_LINE_NUM() this->lineNum++

#define YYGETCONDITION() this->modeStack.back()

TokenKind Lexer::nextToken(Token &token) {
    /*!re2c
      re2c:define:YYGETCONDITION = YYGETCONDITION;
      re2c:define:YYCTYPE = "unsigned char";
      re2c:define:YYCURSOR = this->cursor;
      re2c:define:YYLIMIT = this->limit;
      re2c:define:YYMARKER = this->marker;
      re2c:define:YYFILL:naked = 1;
      re2c:define:YYFILL@len = #;
      re2c:define:YYFILL = "if(!this->fill(#)) { RET(EOS); }";
      re2c:yyfill:enable = 1;
      re2c:indent:top = 1;
      re2c:indent:string = "    ";

      NUM = '0' | [1-9] [0-9]*;
      DIGITS = [0-9]+;
      FLOAT_SUFFIX =  [eE] [+-]? NUM;
      FLOAT = NUM '.' DIGITS FLOAT_SUFFIX?;

      SQUOTE_CHAR = [^\r\n'\\] | '\\' [btnfr'\\];
      VAR_NAME = [a-zA-Z] [_0-9a-zA-Z]* | '_' [_0-9a-zA-Z]+;
      SPECIAL_NAMES = [@];

      CMD_START_CHAR = '\\' . | [^ \t\r\n;'"`|&<>(){}$#![\]0-9];
      CMD_CHAR       = '\\' . | [^ \t\r\n;'"`|&<>(){}$#![\]];

      OTHER = .;
    */

INIT:
    unsigned int startPos = this->getPos();
    TokenKind kind = INVALID;
    /*!re2c
      <STMT,EXPR> 'assert'     { MODE(EXPR); RET(ASSERT); }
      <STMT,EXPR> 'catch'      { MODE(EXPR); RET(CATCH); }
      <STMT,EXPR> 'class'      { MODE(EXPR); RET(CLASS); }
      <STMT,EXPR> 'continue'   { MODE(EXPR); RET(CONTINUE); }
      <STMT,EXPR> 'do'         { MODE(EXPR); RET(DO); }
      <STMT,EXPR> 'else'       { MODE(EXPR); RET(ELSE); }
      <STMT,EXPR> 'extends'    { MODE(EXPR); RET(EXTENDS); }
      <STMT,EXPR> 'export-env' { MODE(EXPR); RET(EXPORT_ENV); }
      <STMT,EXPR> 'finally'    { MODE(EXPR); RET(FINALLY); }
      <STMT,EXPR> 'for'        { MODE(EXPR); RET(FOR); }
      <STMT,EXPR> 'function'   { MODE(EXPR); RET(FUNCTION); }
      <STMT,EXPR> 'if'         { MODE(EXPR); RET(IF); }
      <STMT,EXPR> 'import-env' { MODE(EXPR); RET(IMPORT_ENV); }
      <STMT,EXPR> 'let'        { MODE(EXPR); RET(LET); }
      <STMT,EXPR> 'new'        { MODE(EXPR); RET(NEW); }
      <STMT,EXPR> 'not'        { MODE(EXPR); RET(NOT); }
      <STMT,EXPR> 'return'     { MODE(EXPR); RET(RETURN); }
      <STMT,EXPR> 'try'        { MODE(EXPR); RET(TRY); }
      <STMT,EXPR> 'throw'      { MODE(EXPR); RET(THROW); }
      <STMT,EXPR> 'var'        { MODE(EXPR); RET(VAR); }
      <STMT,EXPR> 'while'      { MODE(EXPR); RET(WHILE); }

      <STMT,EXPR> '+'          { MODE(EXPR); RET(PLUS); }
      <STMT,EXPR> '-'          { MODE(EXPR); RET(MINUS); }

      <STMT,EXPR> NUM          { MODE(EXPR); RET(INT_LITERAL); }
      <STMT,EXPR> FLOAT        { MODE(EXPR); RET(FLOAT_LITERAL); }
      <STMT,EXPR> ['] SQUOTE_CHAR* [']
                               { MODE(EXPR); RET(STRING_LITERAL); }
      <STMT,EXPR> ["]          { PUSH_MODE(DSTRING); RET(OPEN_DQUOTE); }
      <STMT,EXPR> [`] ('\\' '`' | [^\n\r])+ [`]
                               { MODE(EXPR); RET(BQUOTE_LITERAL); }
      <STMT,EXPR> '$('         { PUSH_MODE(STMT); RET(START_SUB_CMD); }

      <STMT,EXPR> '$' VAR_NAME { MODE(EXPR); RET(APPLIED_NAME); }
      <STMT,EXPR> '$' SPECIAL_NAMES
                               { MODE(EXPR); RET(SPECIAL_NAME); }

      <STMT,EXPR> '('          { PUSH_MODE(STMT); RET(LP); }
      <STMT,EXPR> ')'          { POP_MODE(); RET(RP); }
      <STMT,EXPR> '['          { MODE(EXPR); RET(LB); }
      <STMT,EXPR> ']'          { MODE(EXPR); RET(RB); }
      <STMT,EXPR> '{'          { PUSH_MODE(STMT); RET(LBC); }
      <STMT,EXPR> '}'          { POP_MODE(); RET(RBC); }
      <STMT,EXPR> '<'          { MODE(EXPR); RET(LA); }
      <STMT,EXPR> '>'          { MODE(EXPR); RET(RA); }

      <STMT> CMD_START_CHAR CMD_CHAR*
                               { PUSH_MODE(CMD); RET(COMMAND); }

      <EXPR> ':'               { RET(COLON); }
      <EXPR> ','               { RET(COMMA); }

      <EXPR> '*'               { RET(MUL); }
      <EXPR> '/'               { RET(DIV); }
      <EXPR> '%'               { RET(MOD); }
      <EXPR> '<='              { RET(LE); }
      <EXPR> '>='              { RET(GE); }
      <EXPR> '=='              { RET(EQ); }
      <EXPR> '!='              { RET(NE); }
      <EXPR> '&'               { RET(AND); }
      <EXPR> '|'               { RET(OR); }
      <EXPR> '^'               { RET(XOR); }
      <EXPR> '&&'              { RET(COND_AND); }
      <EXPR> '||'              { RET(COND_OR); }
      <EXPR> '=~'              { RET(RE_MATCH); }
      <EXPR> '!~'              { RET(RE_UNMATCH); }

      <EXPR> '++'              { RET(INC); }
      <EXPR> '--'              { RET(DEC); }

      <EXPR> '='               { MODE(STMT); RET(ASSIGN); }
      <EXPR> '+='              { MODE(STMT); RET(ADD_ASSIGN); }
      <EXPR> '-='              { MODE(STMT); RET(SUB_ASSIGN); }
      <EXPR> '*='              { MODE(STMT); RET(MUL_ASSIGN); }
      <EXPR> '/='              { MODE(STMT); RET(DIV_ASSIGN); }
      <EXPR> '%='              { MODE(STMT); RET(MOD_ASSIGN); }

      <EXPR> 'as'              { RET(AS); }
      <EXPR> 'Func'            { RET(FUNC); }
      <EXPR> 'in'              { RET(IN); }
      <EXPR> 'is'              { RET(IS); }

      <EXPR> VAR_NAME          { RET(IDENTIFIER); }
      <EXPR> '.'               { RET(ACCESSOR); }

      <STMT,EXPR> ';'          { RET(LINE_END); }
      <STMT,EXPR> [\r\n]       { INC_LINE_NUM(); RET(NEW_LINE); }

      <STMT,EXPR> '#' [^\r\n]* { SKIP(); }
      <STMT,EXPR> [ \t]+       { SKIP(); }
      <STMT,EXPR> '\\' [\r\n]  { INC_LINE_NUM(); SKIP(); }

      <STMT,EXPR,DSTRING,CMD> OTHER { RET(INVALID); }
    */

END:
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

