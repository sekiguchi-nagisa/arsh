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

#include <util/debug.h>
#include <parser/Lexer.h>

Lexer::Lexer(unsigned int initSize, bool fixed) :
        fp(0),
        bufSize((fixed || initSize > DEFAULT_SIZE) ? initSize : DEFAULT_SIZE),
        buf(new unsigned char[this->bufSize]),
        cursor(this->buf), limit(this->buf), marker(0),
        lineNum(1), endOfFile(false), modeStack(1, yycSTMT) {
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

#define INC_LINE_NUM() ++this->lineNum

#define FIND_NEW_LINE() \
    do {\
        unsigned int stopPos = this->getPos();\
        for(unsigned int i = startPos; i < stopPos; ++i) {\
            if(this->buf[i] == '\n') { ++this->lineNum; } \
        }\
    } while(0)


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

      STRING_LITERAL = ['] SQUOTE_CHAR* ['];
      BQUOTE_LITERAL = [`] ('\\' '`' | [^\n\r])+ [`];
      APPLIED_NAME = '$' VAR_NAME;
      SPECIAL_NAME = '$' SPECIAL_NAMES;

      INNER_NAME = APPLIED_NAME | '${' VAR_NAME '}';
      INNER_SPECIAL_NAME = SPECIAL_NAME | '${' SPECIAL_NAMES '}';

      CMD_START_CHAR = '\\' . | [^ \t\r\n;'"`|&<>(){}$#![\]0-9\000];
      CMD_CHAR       = '\\' . | [^ \t\r\n;'"`|&<>(){}$#![\]\000];

      LINE_END = ';';
      NEW_LINE = [\r\n];
      COMMENT = '#' [^\r\n\000]*;
      OTHER = .;
    */

INIT:
    unsigned int startPos = this->getPos();
    TokenKind kind = INVALID;
    /*!re2c
      <STMT> 'assert'          { RET(ASSERT); }
      <STMT> 'break'           { RET(BREAK); }
      <STMT> 'catch'           { RET(CATCH); }
      <STMT> 'class'           { MODE(EXPR); RET(CLASS); }
      <STMT> 'continue'        { RET(CONTINUE); }
      <STMT> 'do'              { RET(DO); }
      <STMT> 'else'            { RET(ELSE); }
      <STMT,EXPR> 'extends'    { MODE(EXPR); RET(EXTENDS); }
      <STMT> 'export-env'      { MODE(EXPR); RET(EXPORT_ENV); }
      <STMT> 'finally'         { RET(FINALLY); }
      <STMT> 'for'             { RET(FOR); }
      <STMT> 'function'        { MODE(EXPR); RET(FUNCTION); }
      <STMT> 'if'              { RET(IF); }
      <STMT> 'import-env'      { MODE(EXPR); RET(IMPORT_ENV); }
      <STMT> 'let'             { MODE(EXPR); RET(LET); }
      <STMT,EXPR> 'new'        { MODE(EXPR); RET(NEW); }
      <STMT,EXPR> 'not'        { MODE(EXPR); RET(NOT); }
      <STMT> 'return'          { MODE(EXPR); RET(RETURN); }
      <STMT> 'try'             { RET(TRY); }
      <STMT> 'throw'           { MODE(EXPR); RET(THROW); }
      <STMT> 'var'             { MODE(EXPR); RET(VAR); }
      <STMT> 'while'           { RET(WHILE); }

      <STMT,EXPR> '+'          { MODE(EXPR); RET(PLUS); }
      <STMT,EXPR> '-'          { MODE(EXPR); RET(MINUS); }

      <STMT,EXPR> NUM          { MODE(EXPR); RET(INT_LITERAL); }
      <STMT,EXPR> FLOAT        { MODE(EXPR); RET(FLOAT_LITERAL); }
      <STMT,EXPR> STRING_LITERAL
                               { MODE(EXPR); RET(STRING_LITERAL); }
      <STMT,EXPR,CMD> ["]      { PUSH_MODE(DSTRING); RET(OPEN_DQUOTE); }
      <STMT,EXPR> BQUOTE_LITERAL
                               { MODE(EXPR); RET(BQUOTE_LITERAL); }
      <STMT,EXPR,DSTRING,CMD> '$('
                               { PUSH_MODE(STMT); RET(START_SUB_CMD); }

      <STMT,EXPR> APPLIED_NAME { MODE(EXPR); RET(APPLIED_NAME); }
      <STMT,EXPR> SPECIAL_NAME { MODE(EXPR); RET(SPECIAL_NAME); }

      <STMT,EXPR> '('          { PUSH_MODE(STMT); RET(LP); }
      <STMT,EXPR> ')'          { POP_MODE(); RET(RP); }
      <STMT,EXPR> '['          { MODE(EXPR); RET(LB); }
      <STMT,EXPR> ']'          { MODE(EXPR); RET(RB); }
      <STMT,EXPR> '{'          { PUSH_MODE(STMT); RET(LBC); }
      <STMT,EXPR> '}'          { POP_MODE(); RET(RBC); }

      <STMT> CMD_START_CHAR CMD_CHAR*
                               { PUSH_MODE(CMD); FIND_NEW_LINE(); RET(COMMAND); }

      <EXPR> ':' [ \t\r\n]*    { FIND_NEW_LINE(); RET(COLON); }
      <EXPR> ',' [ \t\r\n]*    { FIND_NEW_LINE(); RET(COMMA); }

      <EXPR> '*'               { RET(MUL); }
      <EXPR> '/'               { RET(DIV); }
      <EXPR> '%'               { RET(MOD); }
      <EXPR> '<'               { RET(LA); }
      <EXPR> '>'               { RET(RA); }
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

      <STMT,EXPR> LINE_END     { MODE(STMT); RET(LINE_END); }
      <STMT,EXPR> NEW_LINE     { MODE(STMT); INC_LINE_NUM(); RET(NEW_LINE); }

      <STMT,EXPR,CMD> COMMENT  { SKIP(); }
      <STMT,EXPR> [ \t]+       { SKIP(); }
      <EXPR> '\\' [\r\n]       { INC_LINE_NUM(); SKIP(); }

      <DSTRING> ["]            { POP_MODE(); RET(CLOSE_DQUOTE);}
      <DSTRING> ([^\r\n`$"\\] | '\\' [$btnfr"`\\])+
                               { RET(STR_ELEMENT);}
      <DSTRING,CMD> BQUOTE_LITERAL
                               { RET(BQUOTE_LITERAL); }
      <DSTRING,CMD> INNER_NAME { RET(APPLIED_NAME); }
      <DSTRING,CMD> INNER_SPECIAL_NAME
                               { RET(SPECIAL_NAME); }
      <DSTRING,CMD> '${'       { PUSH_MODE(EXPR); RET(START_INTERP); }

      <CMD> CMD_CHAR+          { FIND_NEW_LINE();  RET(CMD_ARG_PART); }
      <CMD> STRING_LITERAL     { RET(STRING_LITERAL); }
      <CMD> ')'                { POP_MODE(); POP_MODE(); RET(RP); }
      <CMD> [ \t]+             { RET(CMD_SEP); }
      <CMD> ('<' | '>' | '1>' | '1>>' | '>>' | '2>' | '2>>' | '>&' | '&>' | '&>>')
                               { RET(REDIR_OP); }
      <CMD> '2>&1'             { RET(REDIR_OP_NO_ARG); }
      <CMD> '|'                { POP_MODE(); MODE(STMT); RET(PIPE); }
      <CMD> '&'                { RET(BACKGROUND); }
      <CMD> '||'               { POP_MODE(); MODE(STMT); RET(OR_LIST); }
      <CMD> '&&'               { POP_MODE(); MODE(STMT); RET(AND_LIST); }
      <CMD> LINE_END           { POP_MODE(); MODE(STMT); RET(LINE_END); }
      <CMD> NEW_LINE           { POP_MODE(); MODE(STMT); RET(NEW_LINE); }


      <STMT,EXPR,DSTRING,CMD> '\000' { RET(EOS); }
      <STMT,EXPR,DSTRING,CMD> OTHER  { RET(INVALID); }
    */

END:
    token.startPos = startPos;
    token.size = this->getPos() - startPos;
    return kind;
}

void Lexer::setLineNum(unsigned int lineNum) {
    this->lineNum = lineNum;
}

unsigned int Lexer::getLineNum() const {
    return this->lineNum;
}

#define CHECK_TOK(token) \
    assert(token.startPos < this->getUsedSize &&\
            token.startPos + token.size <= this->getUsedSize())

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
    return 0;   //FIXME:
}

