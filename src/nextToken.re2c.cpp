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

#include <parser/Lexer.h>

// helper macro definition.
#define RET(k) do { kind = k; goto END; } while(0)

#define REACH_EOS() do { this->endOfString = true; goto EOS; } while(0)

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

/*
 * count new line and increment lineNum.
 */
#define COUNT_NEW_LINE() \
    do {\
        unsigned int stopPos = this->getPos();\
        for(unsigned int i = startPos; i < stopPos; ++i) {\
            if(this->buf[i] == '\n') { ++this->lineNum; } \
        }\
    } while(0)

#define FIND_NEW_LINE() \
    do {\
        foundNewLine = true;\
        SKIP();\
    } while(0)


#define YYGETCONDITION() this->modeStack.back()

namespace ydsh {
namespace parser {

TokenKind Lexer::nextToken(Token &token) {
    /*!re2c
      re2c:define:YYGETCONDITION = YYGETCONDITION;
      re2c:define:YYCTYPE = "unsigned char";
      re2c:define:YYCURSOR = this->cursor;
      re2c:define:YYLIMIT = this->limit;
      re2c:define:YYMARKER = this->marker;
      re2c:define:YYCTXMARKER = this->ctxMarker;
      re2c:define:YYFILL:naked = 1;
      re2c:define:YYFILL@len = #;
      re2c:define:YYFILL = "if(!this->fill(#)) { REACH_EOS(); }";
      re2c:yyfill:enable = 1;
      re2c:indent:top = 1;
      re2c:indent:string = "    ";

      NUM = "0" | [1-9] [0-9]*;
      INT = [+-]? NUM;
      DIGITS = [0-9]+;
      FLOAT_SUFFIX =  [eE] [+-]? NUM;
      FLOAT = INT "." DIGITS FLOAT_SUFFIX?;

      SQUOTE_CHAR = [^\r\n'\\] | '\\' [btnfr'\\];
      VAR_NAME = [_a-zA-Z] [_0-9a-zA-Z]* ;
      SPECIAL_NAMES = [@0?];

      STRING_LITERAL = ['] SQUOTE_CHAR* ['];
      PATH_CHARS = "/" | ("/" [_a-zA-Z0-9]+ )+;
      BQUOTE_LITERAL = [`] ('\\' '`' | [^\n\r])+ [`];
      APPLIED_NAME = "$" VAR_NAME;
      SPECIAL_NAME = "$" SPECIAL_NAMES;

      INNER_NAME = APPLIED_NAME | '${' VAR_NAME '}';
      INNER_SPECIAL_NAME = SPECIAL_NAME | '${' SPECIAL_NAMES '}';

      CMD_START_CHAR = "\\" [^\r\n] | [^ \t\r\n;'"`|&<>(){}$#![\]0-9\000];
      CMD_CHAR       = "\\" . | "\\" [\r\n] | [^ \t\r\n;'"`|&<>(){}$#![\]\000];

      LINE_END = ";";
      NEW_LINE = [\r\n][ \t\r\n]*;
      COMMENT = "#" [^\r\n\000]*;
      OTHER = . | [\r\n];
    */

    bool foundNewLine = false;

    INIT:
    unsigned int startPos = this->getPos();
    TokenKind kind = INVALID;
    /*!re2c
      <STMT,EXPR> "assert"     { RET(ASSERT); }
      <STMT,EXPR> "break"      { RET(BREAK); }
      <STMT,EXPR> "catch"      { RET(CATCH); }
      <STMT,EXPR> "class"      { MODE(NAME); RET(CLASS); }
      <STMT,EXPR> "continue"   { RET(CONTINUE); }
      <STMT,EXPR> "do"         { RET(DO); }
      <STMT,EXPR> "elif"       { RET(ELIF); }
      <STMT,EXPR> "else"       { RET(ELSE); }
      <STMT,EXPR> "extends"    { MODE(EXPR); RET(EXTENDS); }
      <STMT,EXPR> "export-env" { MODE(NAME); RET(EXPORT_ENV); }
      <STMT,EXPR> "finally"    { RET(FINALLY); }
      <STMT,EXPR> "for"        { RET(FOR); }
      <STMT,EXPR> "function"   { MODE(NAME); RET(FUNCTION); }
      <STMT,EXPR> "if"         { RET(IF); }
      <STMT,EXPR> "import-env" { MODE(NAME); RET(IMPORT_ENV); }
      <STMT,EXPR> "interface"  { RET(INTERFACE); }
      <STMT,EXPR> "let"        { MODE(NAME); RET(LET); }
      <STMT,EXPR> "new"        { MODE(EXPR); RET(NEW); }
      <STMT,EXPR> "not"        { MODE(EXPR); RET(NOT); }
      <STMT,EXPR> "return"     { MODE(EXPR); RET(RETURN); }
      <STMT,EXPR> "try"        { RET(TRY); }
      <STMT,EXPR> "throw"      { MODE(EXPR); RET(THROW); }
      <STMT,EXPR> "type-alias" { MODE(EXPR); RET(TYPE_ALIAS);}
      <STMT,EXPR> "var"        { MODE(NAME); RET(VAR); }
      <STMT,EXPR> "while"      { RET(WHILE); }

      <STMT,EXPR> "+"          { MODE(EXPR); RET(PLUS); }
      <STMT,EXPR> "-"          { MODE(EXPR); RET(MINUS); }

      <STMT,EXPR> INT          { MODE(EXPR); RET(INT_LITERAL); }
      <STMT,EXPR> INT "b"      { MODE(EXPR); RET(BYTE_LITERAL); }
      <STMT,EXPR> INT "i16"    { MODE(EXPR); RET(INT16_LITERAL); }
      <STMT,EXPR> INT "i32"    { MODE(EXPR); RET(INT32_LITERAL); }
      <STMT,EXPR> INT "i64"    { MODE(EXPR); RET(INT64_LITERAL); }
      <STMT,EXPR> INT "u16"    { MODE(EXPR); RET(UINT16_LITERAL); }
      <STMT,EXPR> INT "u32"    { MODE(EXPR); RET(UINT32_LITERAL); }
      <STMT,EXPR> INT "u64"    { MODE(EXPR); RET(UINT64_LITERAL); }
      <STMT,EXPR> FLOAT        { MODE(EXPR); RET(FLOAT_LITERAL); }
      <STMT,EXPR> STRING_LITERAL
                               { MODE(EXPR); RET(STRING_LITERAL); }
      <STMT,EXPR> "p" ['] PATH_CHARS [']
                               { MODE(EXPR); RET(PATH_LITERAL); }
      <STMT,EXPR> ["]          { MODE(EXPR); PUSH_MODE(DSTRING); RET(OPEN_DQUOTE); }
      <STMT,EXPR> BQUOTE_LITERAL
                               { MODE(EXPR); RET(BQUOTE_LITERAL); }
      <STMT,EXPR> "$("         { MODE(EXPR); PUSH_MODE(STMT); RET(START_SUB_CMD); }

      <STMT,EXPR> APPLIED_NAME { MODE(EXPR); RET(APPLIED_NAME); }
      <STMT,EXPR> SPECIAL_NAME { MODE(EXPR); RET(SPECIAL_NAME); }

      <STMT,EXPR> "("          { MODE(EXPR); PUSH_MODE(STMT); RET(LP); }
      <STMT,EXPR> ")"          { POP_MODE(); RET(RP); }
      <STMT,EXPR> "["          { MODE(EXPR); RET(LB); }
      <STMT,EXPR> "]"          { MODE(EXPR); RET(RB); }
      <STMT,EXPR> "{"          { MODE(EXPR); PUSH_MODE(STMT); RET(LBC); }
      <STMT,EXPR> "}"          { POP_MODE(); RET(RBC); }

      <STMT> CMD_START_CHAR CMD_CHAR*
                               { PUSH_MODE(CMD); COUNT_NEW_LINE(); RET(COMMAND); }

      <EXPR> ":"               { RET(COLON); }
      <EXPR> ","               { RET(COMMA); }

      <EXPR> "*"               { RET(MUL); }
      <EXPR> "/"               { RET(DIV); }
      <EXPR> "%"               { RET(MOD); }
      <EXPR> "<"               { RET(LA); }
      <EXPR> ">"               { RET(RA); }
      <EXPR> "<="              { RET(LE); }
      <EXPR> ">="              { RET(GE); }
      <EXPR> "=="              { RET(EQ); }
      <EXPR> "!="              { RET(NE); }
      <EXPR> "&"               { RET(AND); }
      <EXPR> "|"               { RET(OR); }
      <EXPR> "^"               { RET(XOR); }
      <EXPR> "&&"              { RET(COND_AND); }
      <EXPR> "||"              { RET(COND_OR); }
      <EXPR> "=~"              { RET(RE_MATCH); }
      <EXPR> "!~"              { RET(RE_UNMATCH); }

      <EXPR> "++"              { RET(INC); }
      <EXPR> "--"              { RET(DEC); }

      <EXPR> "="               { MODE(STMT); RET(ASSIGN); }
      <EXPR> "+="              { MODE(STMT); RET(ADD_ASSIGN); }
      <EXPR> "-="              { MODE(STMT); RET(SUB_ASSIGN); }
      <EXPR> "*="              { MODE(STMT); RET(MUL_ASSIGN); }
      <EXPR> "/="              { MODE(STMT); RET(DIV_ASSIGN); }
      <EXPR> "%="              { MODE(STMT); RET(MOD_ASSIGN); }

      <EXPR> "as"              { RET(AS); }
      <EXPR> "Func"            { RET(FUNC); }
      <EXPR> "in"              { RET(IN); }
      <EXPR> "is"              { RET(IS); }

      <EXPR,NAME> VAR_NAME     { MODE(EXPR); RET(IDENTIFIER); }
      <NAME> NEW_LINE          { COUNT_NEW_LINE(); SKIP(); }
      <EXPR> "."               { MODE(NAME); RET(ACCESSOR); }

      <STMT,EXPR> LINE_END     { MODE(STMT); RET(LINE_END); }
      <STMT,EXPR> NEW_LINE     { MODE(STMT); COUNT_NEW_LINE(); FIND_NEW_LINE(); }

      <STMT,EXPR,NAME,CMD,TYPE> COMMENT
                               { SKIP(); }
      <STMT,EXPR,NAME,TYPE> [ \t]+
                               { SKIP(); }
      <STMT,EXPR,NAME,TYPE> "\\" [\r\n]
                               { INC_LINE_NUM(); SKIP(); }

      <DSTRING> ["]            { POP_MODE(); RET(CLOSE_DQUOTE);}
      <DSTRING> ([^\r\n`$"\\] | '\\' [$btnfr"`\\])+
                               { RET(STR_ELEMENT);}
      <DSTRING,CMD> BQUOTE_LITERAL
                               { RET(BQUOTE_LITERAL); }
      <DSTRING,CMD> INNER_NAME { RET(APPLIED_NAME); }
      <DSTRING,CMD> INNER_SPECIAL_NAME
                               { RET(SPECIAL_NAME); }
      <DSTRING,CMD> "${"       { PUSH_MODE(EXPR); RET(START_INTERP); }
      <DSTRING,CMD> "$("       { PUSH_MODE(STMT); RET(START_SUB_CMD); }

      <CMD> CMD_CHAR+          { COUNT_NEW_LINE();  RET(CMD_ARG_PART); }
      <CMD> STRING_LITERAL     { RET(STRING_LITERAL); }
      <CMD> ["]                { PUSH_MODE(DSTRING); RET(OPEN_DQUOTE); }
      <CMD> ")"                { POP_MODE(); POP_MODE(); RET(RP); }
      <CMD> [ \t]+ / "&>"      { RET(CMD_SEP); }
      <CMD> [ \t]+ / ([|&] | LINE_END | NEW_LINE | ')' | '#')
                               { SKIP(); }
      <CMD> [ \t]+             { RET(CMD_SEP); }

      <CMD> "<"                { RET(REDIR_IN_2_FILE); }
      <CMD> (">" | "1>")       { RET(REDIR_OUT_2_FILE); }
      <CMD> ("1>>" | ">>")     { RET(REDIR_OUT_2_FILE_APPEND); }
      <CMD> "2>"               { RET(REDIR_ERR_2_FILE); }
      <CMD> "2>>"              { RET(REDIR_ERR_2_FILE_APPEND); }
      <CMD> (">&" | "&>")      { RET(REDIR_MERGE_ERR_2_OUT_2_FILE); }
      <CMD> "&>>"              { RET(REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND); }
      <CMD> "2>&1"             { RET(REDIR_MERGE_ERR_2_OUT); }

      <CMD> "|"                { POP_MODE(); MODE(STMT); RET(PIPE); }
      <CMD> "&"                { RET(BACKGROUND); }
      <CMD> "||"               { POP_MODE(); MODE(STMT); RET(OR_LIST); }
      <CMD> "&&"               { POP_MODE(); MODE(STMT); RET(AND_LIST); }
      <CMD> LINE_END           { POP_MODE(); MODE(STMT); RET(LINE_END); }
      <CMD> NEW_LINE           { POP_MODE(); MODE(STMT); COUNT_NEW_LINE(); RET(LINE_END); }

      <TYPE> VAR_NAME ("." VAR_NAME)+
                               { RET(TYPE_PATH); }
      <TYPE> "Func"            { RET(FUNC); }
      <TYPE> VAR_NAME          { RET(IDENTIFIER); }
      <TYPE> NEW_LINE          { COUNT_NEW_LINE(); FIND_NEW_LINE(); }
      <TYPE> "<"               { RET(TYPE_OPEN); }
      <TYPE> ">"               { RET(TYPE_CLOSE); }
      <TYPE> ","               { RET(TYPE_SEP); }
      <TYPE> "["               { RET(PTYPE_OPEN); }
      <TYPE> "]"               { RET(PTYPE_CLOSE); }
      <TYPE> OTHER             { RET(TYPE_OTHER); }


      <STMT,EXPR,NAME,DSTRING,CMD,TYPE> "\000" { REACH_EOS();}
      <STMT,EXPR,NAME,DSTRING,CMD> OTHER  { RET(INVALID); }
    */

    END:
    token.startPos = startPos;
    token.size = this->getPos() - startPos;
    this->prevNewLine = foundNewLine;
#ifdef X_TRACE_TOKEN
#include <stdio.h>
    fprintf(stderr, "nextToken(): < kind=%s, start=%d, size=%d, text=%s >\n",
            TO_NAME(kind), token.startPos, token.size, this->toTokenText(token).c_str());
    fprintf(stderr, "   lexer mode: %s\n", this->getLexerModeName(YYGETCONDITION()));
#endif
    return kind;

    EOS:
    token.startPos = this->limit - this->buf;
    token.size = 0;
    this->prevNewLine = foundNewLine;
#ifdef X_TRACE_TOKEN
#include <stdio.h>
    fprintf(stderr, "nextToken(): < kind=%s, start=%d, size=%d, text=%s >\n",
            TO_NAME(EOS), token.startPos, token.size, this->toTokenText(token).c_str());
    fprintf(stderr, "   lexer mode: %s\n", this->getLexerModeName(YYGETCONDITION()));
#endif
    return EOS;
}

} // namespace parser
} // namespace ydsh