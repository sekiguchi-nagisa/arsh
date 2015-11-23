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
#include <core/logger.h>

#ifdef USE_TRACE_TOKEN
#include <iostream>
#include <cstdlib>
#endif

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
        if(!this->modeStack.empty()) {\
            this->modeStack.back() = yyc ## m;\
        } else {\
            ERROR();\
        }\
    } while(0)

/*
 * count new line and increment lineNum.
 */
#define COUNT_NEW_LINE() \
    do {\
        const unsigned int stopPos = this->getPos();\
        for(unsigned int i = startPos; i < stopPos; ++i) {\
            if(this->buf[i] == '\n') \
            { this->srcInfoPtr->addNewlinePos(i); } \
        }\
    } while(0)

#define FIND_NEW_LINE() \
    do {\
        foundNewLine = true;\
        SKIP();\
    } while(0)

#define FIND_SPACE() \
    do {\
        foundSpace = true;\
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
      DIGITS = [0-9]+;
      FLOAT_SUFFIX =  [eE] [+-]? NUM;
      FLOAT = NUM "." DIGITS FLOAT_SUFFIX?;

      SQUOTE_CHAR = '\\' ['] | [^'\000];
      DQUOTE_CHAR = "\\" [^\000] | [^$\\"\000];
      VAR_NAME = [_a-zA-Z] [_0-9a-zA-Z]* ;
      SPECIAL_NAMES = [@#?0-9];

      STRING_LITERAL = ['] [^'\000]* ['];
      ESTRING_LITERAL = "$" ['] SQUOTE_CHAR* ['];
      PATH_CHARS = "/" | ("/" [_a-zA-Z0-9]+ )+;
      APPLIED_NAME = "$" VAR_NAME;
      SPECIAL_NAME = "$" SPECIAL_NAMES;

      INNER_NAME = APPLIED_NAME | '${' VAR_NAME '}';
      INNER_SPECIAL_NAME = SPECIAL_NAME | '${' SPECIAL_NAMES '}';

      CMD_START_CHAR     = "\\" [^\r\n\000] | [^ \t\r\n\\;'"`|&<>(){}$#![\]+\-0-9\000];
      CMD_CHAR           = "\\" [^\000]     | [^ \t\r\n\\;'"`|&<>(){}$#![\]\000];
      CMD_ARG_START_CHAR = "\\" [^\r\n\000] | [^ \t\r\n\\;'"`|&<>(){}$#![\]\000];

      LINE_END = ";";
      NEW_LINE = [\r\n][ \t\r\n]*;
      COMMENT = "#" [^\r\n\000]*;
      OTHER = . ;
    */

    bool foundNewLine = false;
    bool foundSpace = false;
    LexerMode prevMode = this->modeStack.back();

    INIT:
    unsigned int startPos = this->getPos();
    TokenKind kind = INVALID;
    /*!re2c
      <STMT> "assert"          { RET(ASSERT); }
      <STMT> "break"           { RET(BREAK); }
      <STMT> "catch"           { RET(CATCH); }
      <STMT> "class"           { MODE(NAME); RET(CLASS); }
      <STMT> "continue"        { RET(CONTINUE); }
      <STMT> "do"              { RET(DO); }
      <STMT> "elif"            { RET(ELIF); }
      <STMT> "else"            { RET(ELSE); }
      <STMT> "export-env"      { MODE(NAME); RET(EXPORT_ENV); }
      <STMT> "finally"         { RET(FINALLY); }
      <STMT> "for"             { RET(FOR); }
      <STMT> "function"        { MODE(NAME); RET(FUNCTION); }
      <STMT> "if"              { RET(IF); }
      <STMT> "import-env"      { MODE(NAME); RET(IMPORT_ENV); }
      <STMT> "interface"       { RET(INTERFACE); }
      <STMT> "let"             { MODE(NAME); RET(LET); }
      <STMT> "new"             { MODE(EXPR); RET(NEW); }
      <STMT> "not"             { RET(NOT); }
      <STMT> "return"          { RET(RETURN); }
      <STMT> "try"             { RET(TRY); }
      <STMT> "throw"           { RET(THROW); }
      <STMT> "type-alias"      { MODE(NAME); RET(TYPE_ALIAS);}
      <STMT> "var"             { MODE(NAME); RET(VAR); }
      <STMT> "while"           { RET(WHILE); }

      <STMT,EXPR> "+"          { MODE(STMT); RET(PLUS); }
      <STMT,EXPR> "-"          { MODE(STMT); RET(MINUS); }

      <STMT> NUM               { MODE(EXPR); RET(INT_LITERAL); }
      <STMT> NUM "b"           { MODE(EXPR); RET(BYTE_LITERAL); }
      <STMT> NUM "i16"         { MODE(EXPR); RET(INT16_LITERAL); }
      <STMT> NUM "i32"         { MODE(EXPR); RET(INT32_LITERAL); }
      <STMT> NUM "i64"         { MODE(EXPR); RET(INT64_LITERAL); }
      <STMT> NUM "u16"         { MODE(EXPR); RET(UINT16_LITERAL); }
      <STMT> NUM "u32"         { MODE(EXPR); RET(UINT32_LITERAL); }
      <STMT> NUM "u64"         { MODE(EXPR); RET(UINT64_LITERAL); }
      <STMT> FLOAT             { MODE(EXPR); RET(FLOAT_LITERAL); }
      <STMT> STRING_LITERAL    { COUNT_NEW_LINE(); MODE(EXPR); RET(STRING_LITERAL); }
      <STMT> ESTRING_LITERAL   { COUNT_NEW_LINE(); MODE(EXPR); RET(STRING_LITERAL); }
      <STMT> "p" ['] PATH_CHARS [']
                               { MODE(EXPR); RET(PATH_LITERAL); }
      <STMT> ["]               { MODE(EXPR); PUSH_MODE(DSTRING); RET(OPEN_DQUOTE); }
      <STMT> "$("              { MODE(EXPR); PUSH_MODE(STMT); RET(START_SUB_CMD); }

      <STMT> APPLIED_NAME      { MODE(EXPR); RET(APPLIED_NAME); }
      <STMT> SPECIAL_NAME      { MODE(EXPR); RET(SPECIAL_NAME); }

      <STMT,EXPR> "("          { MODE(EXPR); PUSH_MODE(STMT); RET(LP); }
      <STMT,EXPR> ")"          { POP_MODE(); RET(RP); }
      <STMT,EXPR> "["          { MODE(EXPR); PUSH_MODE(STMT); RET(LB); }
      <STMT,EXPR> "]"          { POP_MODE(); RET(RB); }
      <STMT,EXPR> "{"          { MODE(STMT); PUSH_MODE(STMT); RET(LBC); }
      <STMT,EXPR> "}"          { POP_MODE(); RET(RBC); }

      <STMT> CMD_START_CHAR CMD_CHAR*
                               { PUSH_MODE(CMD); COUNT_NEW_LINE(); RET(COMMAND); }

      <EXPR> ":"               { RET(COLON); }
      <EXPR> ","               { MODE(STMT); RET(COMMA); }

      <EXPR> "*"               { MODE(STMT); RET(MUL); }
      <EXPR> "/"               { MODE(STMT); RET(DIV); }
      <EXPR> "%"               { MODE(STMT); RET(MOD); }
      <EXPR> "<"               { MODE(STMT); RET(LA); }
      <EXPR> ">"               { MODE(STMT); RET(RA); }
      <EXPR> "<="              { MODE(STMT); RET(LE); }
      <EXPR> ">="              { MODE(STMT); RET(GE); }
      <EXPR> "=="              { MODE(STMT); RET(EQ); }
      <EXPR> "!="              { MODE(STMT); RET(NE); }
      <EXPR> "&"               { MODE(STMT); RET(AND); }
      <EXPR> "|"               { MODE(STMT); RET(OR); }
      <EXPR> "^"               { MODE(STMT); RET(XOR); }
      <EXPR> "&&"              { MODE(STMT); RET(COND_AND); }
      <EXPR> "||"              { MODE(STMT); RET(COND_OR); }
      <EXPR> "=~"              { MODE(STMT); RET(MATCH); }
      <EXPR> "!~"              { MODE(STMT); RET(UNMATCH); }

      <EXPR> "++"              { RET(INC); }
      <EXPR> "--"              { RET(DEC); }

      <EXPR> "="               { MODE(STMT); RET(ASSIGN); }
      <EXPR> "+="              { MODE(STMT); RET(ADD_ASSIGN); }
      <EXPR> "-="              { MODE(STMT); RET(SUB_ASSIGN); }
      <EXPR> "*="              { MODE(STMT); RET(MUL_ASSIGN); }
      <EXPR> "/="              { MODE(STMT); RET(DIV_ASSIGN); }
      <EXPR> "%="              { MODE(STMT); RET(MOD_ASSIGN); }

      <EXPR> "as"              { RET(AS); }
      <EXPR> "is"              { RET(IS); }
      <EXPR> "in"              { MODE(STMT); RET(IN); }

      <NAME> VAR_NAME          { MODE(EXPR); RET(IDENTIFIER); }
      <EXPR> "."               { MODE(NAME); RET(ACCESSOR); }

      <STMT,EXPR> LINE_END     { MODE(STMT); RET(LINE_END); }
      <STMT,EXPR,NAME,TYPE> NEW_LINE
                               { COUNT_NEW_LINE(); FIND_NEW_LINE(); }

      <STMT,EXPR,NAME,CMD,TYPE> COMMENT
                               { SKIP(); }
      <STMT,EXPR,NAME,TYPE> [ \t]+
                               { SKIP(); }
      <STMT,EXPR,NAME,TYPE> "\\" [\r\n]
                               { COUNT_NEW_LINE(); SKIP(); }

      <DSTRING> ["]            { POP_MODE(); RET(CLOSE_DQUOTE);}
      <DSTRING> DQUOTE_CHAR+   { COUNT_NEW_LINE(); RET(STR_ELEMENT);}
      <DSTRING,CMD> INNER_NAME { RET(APPLIED_NAME); }
      <DSTRING,CMD> INNER_SPECIAL_NAME
                               { RET(SPECIAL_NAME); }
      <DSTRING,CMD> "${"       { PUSH_MODE(STMT); RET(START_INTERP); }
      <DSTRING,CMD> "$("       { PUSH_MODE(STMT); RET(START_SUB_CMD); }

      <CMD> CMD_ARG_START_CHAR CMD_CHAR*
                               { COUNT_NEW_LINE();  RET(CMD_ARG_PART); }
      <CMD> STRING_LITERAL     { COUNT_NEW_LINE(); RET(STRING_LITERAL); }
      <CMD> ESTRING_LITERAL    { COUNT_NEW_LINE(); RET(STRING_LITERAL); }
      <CMD> ["]                { PUSH_MODE(DSTRING); RET(OPEN_DQUOTE); }
      <CMD> ")"                { POP_MODE(); POP_MODE(); RET(RP); }
      <CMD> "("                { PUSH_MODE(CMD); RET(LP); }
      <CMD> "["                { PUSH_MODE(STMT); RET(LB); }
      <CMD> [ \t]+             { FIND_SPACE(); }
      <CMD> "\\" [\r\n]        { COUNT_NEW_LINE(); FIND_SPACE(); }

      <CMD> "<"                { RET(REDIR_IN_2_FILE); }
      <CMD> (">" | "1>")       { RET(REDIR_OUT_2_FILE); }
      <CMD> ("1>>" | ">>")     { RET(REDIR_OUT_2_FILE_APPEND); }
      <CMD> "2>"               { RET(REDIR_ERR_2_FILE); }
      <CMD> "2>>"              { RET(REDIR_ERR_2_FILE_APPEND); }
      <CMD> (">&" | "&>")      { RET(REDIR_MERGE_ERR_2_OUT_2_FILE); }
      <CMD> "&>>"              { RET(REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND); }
      <CMD> "2>&1"             { RET(REDIR_MERGE_ERR_2_OUT); }
      <CMD> "1>&2"             { RET(REDIR_MERGE_OUT_2_ERR); }

      <CMD> "|"                { POP_MODE(); MODE(STMT); RET(PIPE); }
      <CMD> "&"                { RET(BACKGROUND); }
      <CMD> "||"               { POP_MODE(); MODE(STMT); RET(OR_LIST); }
      <CMD> "&&"               { POP_MODE(); MODE(STMT); RET(AND_LIST); }
      <CMD> LINE_END           { POP_MODE(); MODE(STMT); RET(LINE_END); }
      <CMD> NEW_LINE           { POP_MODE(); MODE(STMT); COUNT_NEW_LINE(); RET(LINE_END); }

      <TYPE> VAR_NAME ("." VAR_NAME)+
                               { RET(TYPE_PATH); }
      <TYPE> "Func"            { RET(FUNC); }
      <TYPE> "typeof"          { RET(TYPEOF); }
      <TYPE> VAR_NAME          { RET(IDENTIFIER); }
      <TYPE> "<"               { RET(TYPE_OPEN); }
      <TYPE> ">"               { RET(TYPE_CLOSE); }
      <TYPE> ","               { RET(TYPE_SEP); }
      <TYPE> "["               { RET(PTYPE_OPEN); }
      <TYPE> "]"               { RET(PTYPE_CLOSE); }
      <TYPE> "\000"            { REACH_EOS();}
      <TYPE> OTHER             { RET(TYPE_OTHER); }


      <STMT,EXPR,NAME,DSTRING,CMD> "\000" { REACH_EOS();}
      <STMT,EXPR,NAME,DSTRING,CMD> OTHER  { RET(INVALID); }
    */

    END:
    token.pos = startPos;
    token.size = this->getPos() - startPos;
    goto RET;

    EOS:
    kind = EOS;
    token.pos = this->limit - this->buf;
    token.size = 0;
    goto RET;

    RET:
    this->prevNewLine = foundNewLine;
    this->prevSpace = foundSpace;
    this->prevMode = prevMode;

    LOG(TRACE_TOKEN,
        kind << ", " << token << ", text = " << this->toTokenText(token) << std::endl
                     << "   lexer mode: " << toModeName(this->getLexerMode())
    );
    return kind;
}

} // namespace parser
} // namespace ydsh