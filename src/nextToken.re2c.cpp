/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include "lexer.h"
#include "logger.h"

// helper macro definition.
#define RET_(k)                                                                                    \
  do {                                                                                             \
    kind = k;                                                                                      \
    goto END;                                                                                      \
  } while (false)
#define RET(k) RET_(TokenKind::k)

#define RET_OR_COMP(k)                                                                             \
  do {                                                                                             \
    auto _kind = TokenKind::k;                                                                     \
    if (this->inCompletionPoint()) {                                                               \
      this->setCompTokenKind(TokenKind::k);                                                        \
      _kind = TokenKind::COMPLETION;                                                               \
    }                                                                                              \
    RET_(_kind);                                                                                   \
  } while (false)

#define REACH_EOS()                                                                                \
  do {                                                                                             \
    if (this->isEnd()) {                                                                           \
      goto EOS;                                                                                    \
    } else {                                                                                       \
      ERROR();                                                                                     \
    }                                                                                              \
  } while (false)

#define SKIP() goto INIT

#define ERROR()                                                                                    \
  do {                                                                                             \
    RET(INVALID);                                                                                  \
  } while (false)

#define POP_MODE() this->popLexerMode()

#define PUSH_MODE(m) this->pushLexerMode(yyc##m)

#define PUSH_MODE_SKIP_NL(m) this->pushLexerMode(LexerMode(yyc##m, true))

#define MODE(m) this->setLexerCond(yyc##m)

/*
 * update line number table
 */
#define UPDATE_LN() this->updateNewline(startPos)

#define FIND_NEW_LINE()                                                                            \
  do {                                                                                             \
    foundNewLine = true;                                                                           \
    SKIP();                                                                                        \
  } while (false)

#define FIND_SPACE()                                                                               \
  do {                                                                                             \
    foundSpace = true;                                                                             \
    SKIP();                                                                                        \
  } while (false)

#define SKIPPABLE_NL() this->getLexerMode().skipNL()

#define YYGETCONDITION() this->getLexerMode().cond()

#define STORE_COMMENT() this->addComment(startPos)

namespace ydsh {

TokenKind Lexer::nextToken(Token &token) {
  /*!re2c
    re2c:define:YYCONDTYPE = "LexerCond : unsigned char";
    re2c:define:YYGETCONDITION = YYGETCONDITION;
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = this->cursor;
    re2c:define:YYLIMIT = this->limit;
    re2c:define:YYMARKER = this->marker;
    re2c:define:YYCTXMARKER = this->ctxMarker;
    re2c:define:YYFILL:naked = 1;
    re2c:define:YYFILL@len = #;
    re2c:define:YYFILL = "if(!this->fill(#)) { REACH_EOS(); }";
    re2c:yyfill:enable = 0;
    re2c:indent:top = 1;
    re2c:indent:string = "    ";

    NUM = "0" | [1-9] [0-9]*;
    OCTAL = "0" [oO]? [0-7]+;
    HEX = "0" [xX] [0-9a-fA-F]+;
    INTEGER = NUM | OCTAL | HEX;
    DIGITS = [0-9]+;
    FLOAT_SUFFIX =  [eE] [+-]? DIGITS;
    FLOAT = NUM "." DIGITS FLOAT_SUFFIX?;

    SQUOTE_CHAR = '\\' ['] | [^'\000];
    DQUOTE_CHAR = "\\" [^\000] | [^$\\"\000];
    VAR_NAME = [_a-zA-Z] [_0-9a-zA-Z]* ;
    SPECIAL_NAMES = [@#?$0-9];

    STRING_LITERAL = ['] [^'\000]* ['];
    ESTRING_LITERAL = "$" ['] SQUOTE_CHAR* ['];
    APPLIED_NAME = "$" VAR_NAME;
    SPECIAL_NAME = "$" SPECIAL_NAMES;

    INNER_NAME = APPLIED_NAME | "${" VAR_NAME "}";
    INNER_SPECIAL_NAME = SPECIAL_NAME | "${" SPECIAL_NAMES "}";
    INNER_FIELD = "${" VAR_NAME ("." VAR_NAME)+ "}";

    CMD_START_CHAR     = "\\" [^\r\n\000] | [^ \t\r\n\\;='"`|&<>(){}$#[\]!+\-0-9\000];
    CMD_CHAR           = "\\" [^\000]     | [^ \t\r\n\\;='"`|&<>(){}$\000];
    CMD = CMD_START_CHAR CMD_CHAR*;

    CMD_ARG_START_CHAR = "\\" [^\r\n\000] | [^ \t\r\n\\;'"`|&<>()$?*{},#\000];
    CMD_ARG_CHAR       = "\\" [^\000]     | [^ \t\r\n\\;'"`|&<>()$?*{},\000];
    CMD_ARG = CMD_ARG_START_CHAR CMD_ARG_CHAR*;

    ENV_ASSIGN = CMD "=";

    REGEX_CHAR = "\\/" | [^\r\n\000/];
    REGEX = "$/" REGEX_CHAR* "/" [_a-z]*;

    LINE_END = ";";
    NEW_LINE = [\r\n][ \t\r\n]*;
    COMMENT = "#" [^\r\n\000]*;
  */

  bool foundNewLine = false;
  bool foundSpace = false;

INIT:
  const unsigned int startPos = this->getPos();
  LexerMode mode = this->getLexerMode();
  TokenKind kind = TokenKind::INVALID;
  /*!re2c
    <STMT> "alias"           { MODE(NAME); RET_OR_COMP(ALIAS); }
    <STMT> "assert"          { RET_OR_COMP(ASSERT); }
    <STMT> "break"           { RET_OR_COMP(BREAK); }
    <STMT> "case"            { RET_OR_COMP(CASE); }
    <EXPR> "catch"           { MODE(PARAM); RET(CATCH); }
    <STMT> "continue"        { RET_OR_COMP(CONTINUE); }
    <STMT> "coproc"          { RET_OR_COMP(COPROC); }
    <STMT> "defer"           { RET_OR_COMP(DEFER); }
    <STMT> "do"              { RET_OR_COMP(DO); }
    <EXPR> "elif"            { MODE(STMT); RET(ELIF); }
    <STMT,EXPR> "else"       { MODE(EXPR); RET(ELSE); }
    <STMT> "export-env"      { MODE(NAME); RET_OR_COMP(EXPORT_ENV); }
    <STMT> "exportenv"       { MODE(NAME); RET_OR_COMP(EXPORT_ENV); }
    <EXPR> "finally"         { RET(FINALLY); }
    <STMT,EXPR> "for"        { RET_OR_COMP(FOR); }
    <STMT> "function"        { MODE(NAME); RET_OR_COMP(FUNCTION); }
    <STMT> "if"              { RET_OR_COMP(IF); }
    <STMT> "import-env"      { MODE(NAME); RET_OR_COMP(IMPORT_ENV); }
    <STMT> "importenv"       { MODE(NAME); RET_OR_COMP(IMPORT_ENV); }
    <STMT> "interface"       { RET(INTERFACE); }
    <STMT> "let"             { MODE(NAME); RET_OR_COMP(LET); }
    <STMT> "new"             { MODE(EXPR); RET_OR_COMP(NEW); }
    <STMT> "return"          { RET_OR_COMP(RETURN); }
    <STMT> "source"          { MODE(CMD); RET_OR_COMP(SOURCE); }
    <STMT> "source!"         { MODE(CMD); RET_OR_COMP(SOURCE_OPT); }
    <STMT> "try"             { RET_OR_COMP(TRY); }
    <STMT> "throw"           { RET_OR_COMP(THROW); }
    <STMT> "typedef"         { MODE(NAME); RET_OR_COMP(TYPEDEF); }
    <STMT> "var"             { MODE(NAME); RET_OR_COMP(VAR); }
    <STMT,EXPR> "while"      { MODE(STMT); RET_OR_COMP(WHILE); }

    <STMT> "+"               { RET(PLUS); }
    <STMT> "-"               { RET(MINUS); }
    <STMT> "!"               { RET(NOT); }

    <STMT> INTEGER           { MODE(EXPR); RET(INT_LITERAL); }
    <STMT> FLOAT             { MODE(EXPR); RET(FLOAT_LITERAL); }
    <STMT> STRING_LITERAL    { UPDATE_LN(); MODE(EXPR); RET(STRING_LITERAL); }
    <STMT> ESTRING_LITERAL   { UPDATE_LN(); MODE(EXPR); RET(STRING_LITERAL); }
    <STMT> REGEX             { MODE(EXPR); RET(REGEX_LITERAL); }
    <STMT> "%" ['] VAR_NAME [']
                             { MODE(EXPR); RET(SIGNAL_LITERAL); }
    <STMT> ["]               { MODE(EXPR); PUSH_MODE(DSTRING); RET(OPEN_DQUOTE); }
    <STMT> "$("              { MODE(EXPR); PUSH_MODE_SKIP_NL(STMT); RET(START_SUB_CMD); }
    <STMT> ">("              { MODE(EXPR); PUSH_MODE_SKIP_NL(STMT); RET(START_IN_SUB); }
    <STMT> "<("              { MODE(EXPR); PUSH_MODE_SKIP_NL(STMT); RET(START_OUT_SUB); }
    <STMT> "@("              { MODE(EXPR); PUSH_MODE_SKIP_NL(CMD); RET(AT_PAREN); }

    <STMT> "$"               { if(this->inCompletionPoint()) { RET_OR_COMP(APPLIED_NAME); }
                               else { ERROR();} }
    <STMT> APPLIED_NAME      { MODE(EXPR); RET_OR_COMP(APPLIED_NAME); }
    <STMT> SPECIAL_NAME      { MODE(EXPR); RET(SPECIAL_NAME); }

    <STMT,EXPR,CMD> "("      { MODE(EXPR); PUSH_MODE_SKIP_NL(STMT); RET(LP); }
    <STMT,EXPR,CMD,PARAM> ")"
                             { POP_MODE(); RET(RP); }
    <STMT,EXPR> "["          { MODE(EXPR); PUSH_MODE_SKIP_NL(STMT); RET(LB); }
    <STMT,EXPR> "]"          { POP_MODE(); RET(RB); }
    <STMT,EXPR> "{"          { MODE(EXPR); PUSH_MODE(STMT); RET(LBC); }
    <STMT,EXPR> "}"          { POP_MODE(); RET(RBC); }

    <STMT> CMD               { MODE(CMD); UPDATE_LN(); RET_OR_COMP(COMMAND); }
    <STMT> ENV_ASSIGN        { MODE(CMD); RET(ENV_ASSIGN); }

    <EXPR> ":"               { RET(COLON); }
    <EXPR> ","               { MODE(STMT); RET(COMMA); }

    <EXPR> "+"               { MODE(STMT); RET(ADD); }
    <EXPR> "-"               { MODE(STMT); RET(SUB); }
    <EXPR> "*"               { MODE(STMT); RET(MUL); }
    <EXPR> "/"               { MODE(STMT); RET(DIV); }
    <EXPR> "%"               { MODE(STMT); RET(MOD); }
    <EXPR> "<"               { MODE(STMT); RET(LT); }
    <EXPR> ">"               { MODE(STMT); RET(GT); }
    <EXPR> "<="              { MODE(STMT); RET(LE); }
    <EXPR> ">="              { MODE(STMT); RET(GE); }
    <EXPR> "=="              { MODE(STMT); RET(EQ); }
    <EXPR> "!="              { MODE(STMT); RET(NE); }
    <EXPR> "and"             { MODE(STMT); RET(AND); }
    <EXPR> "or"              { MODE(STMT); RET(OR); }
    <EXPR> "xor"             { MODE(STMT); RET(XOR); }
    <EXPR,CMD> "&&"          { MODE(STMT); RET(COND_AND); }
    <EXPR,CMD> "||"          { MODE(STMT); RET(COND_OR); }
    <EXPR> "=~"              { MODE(STMT); RET(MATCH); }
    <EXPR> "!~"              { MODE(STMT); RET(UNMATCH); }
    <EXPR> ":-"              { MODE(STMT); RET(STR_CHECK); }
    <EXPR> "?"               { MODE(STMT); RET(TERNARY); }
    <EXPR> "??"              { MODE(STMT); RET(NULL_COALE); }
    <EXPR,CMD> "|"           { MODE(STMT); RET(PIPE); }

    <EXPR> "++"              { RET(INC); }
    <EXPR> "--"              { RET(DEC); }
    <EXPR> "!"               { RET(UNWRAP); }

    <EXPR> "="               { MODE(STMT); RET(ASSIGN); }
    <EXPR> "+="              { MODE(STMT); RET(ADD_ASSIGN); }
    <EXPR> "-="              { MODE(STMT); RET(SUB_ASSIGN); }
    <EXPR> "*="              { MODE(STMT); RET(MUL_ASSIGN); }
    <EXPR> "/="              { MODE(STMT); RET(DIV_ASSIGN); }
    <EXPR> "%="              { MODE(STMT); RET(MOD_ASSIGN); }
    <EXPR> ":="              { MODE(STMT); RET(STR_ASSIGN); }
    <EXPR> "??="             { MODE(STMT); RET(NULL_ASSIGN); }
    <EXPR> ("=>" | "->")     { MODE(STMT); RET(CASE_ARM); }

    <EXPR> "as"              { RET(AS); }
    <EXPR> "is"              { RET(IS); }
    <EXPR> "in"              { MODE(STMT); RET(IN); }
    <EXPR> "with"            { MODE(CMD); RET(WITH); }
    <EXPR,CMD> "&"           { MODE(STMT); RET(BACKGROUND); }
    <EXPR,CMD> ("&!" | "&|") { MODE(STMT); RET(DISOWN_BG); }

    <NAME> VAR_NAME          { MODE(EXPR); RET_OR_COMP(IDENTIFIER); }
    <EXPR> "."               { MODE(NAME); RET(ACCESSOR); }
    <EXPR> VAR_NAME          { RET_OR_COMP(INVALID); }

    <DSTRING> ["]            { POP_MODE(); RET(CLOSE_DQUOTE); }
    <DSTRING> DQUOTE_CHAR+   { UPDATE_LN(); RET(STR_ELEMENT); }
    <DSTRING> "$"            { if(this->inCompletionPoint()) { RET_OR_COMP(APPLIED_NAME); }
                               else { RET(STR_ELEMENT); } }
    <DSTRING,CMD> INNER_NAME { RET_OR_COMP(APPLIED_NAME); }
    <DSTRING,CMD> INNER_SPECIAL_NAME
                             { RET(SPECIAL_NAME); }
    <DSTRING,CMD> INNER_FIELD
                             { RET(APPLIED_NAME_WITH_FIELD); }
    <DSTRING,CMD> "${"       { PUSH_MODE_SKIP_NL(STMT); RET(START_INTERP); }
    <DSTRING,CMD> "$("       { PUSH_MODE_SKIP_NL(STMT); RET(START_SUB_CMD); }

    <CMD> CMD_ARG            { UPDATE_LN(); RET_OR_COMP(CMD_ARG_PART); }
    <CMD> "?"                { RET(GLOB_ANY); }
    <CMD> "*"                { RET(GLOB_ZERO_OR_MORE); }
    <CMD> "{"                { RET(BRACE_OPEN); }
    <CMD> "}"                { RET(BRACE_CLOSE); }
    <CMD> ","                { RET(BRACE_SEP); }
    <CMD> STRING_LITERAL     { UPDATE_LN(); RET(STRING_LITERAL); }
    <CMD> ESTRING_LITERAL    { UPDATE_LN(); RET(STRING_LITERAL); }
    <CMD> ["]                { PUSH_MODE(DSTRING); RET(OPEN_DQUOTE); }
    <CMD> APPLIED_NAME "["   { PUSH_MODE_SKIP_NL(STMT); RET(APPLIED_NAME_WITH_BRACKET); }
    <CMD> SPECIAL_NAME "["   { PUSH_MODE_SKIP_NL(STMT); RET(SPECIAL_NAME_WITH_BRACKET); }
    <CMD> APPLIED_NAME "("   { PUSH_MODE_SKIP_NL(STMT); RET(APPLIED_NAME_WITH_PAREN); }
    <CMD> "$"                { if(this->inCompletionPoint()) { RET_OR_COMP(APPLIED_NAME); }
                               else { ERROR();} }

    <CMD> "<"                { RET(REDIR_IN_2_FILE); }
    <CMD> (">" | "1>")       { RET(REDIR_OUT_2_FILE); }
    <CMD> ("1>>" | ">>")     { RET(REDIR_OUT_2_FILE_APPEND); }
    <CMD> "2>"               { RET(REDIR_ERR_2_FILE); }
    <CMD> "2>>"              { RET(REDIR_ERR_2_FILE_APPEND); }
    <CMD> (">&" | "&>")      { RET(REDIR_MERGE_ERR_2_OUT_2_FILE); }
    <CMD> "&>>"              { RET(REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND); }
    <CMD> "2>&1"             { RET(REDIR_MERGE_ERR_2_OUT); }
    <CMD> "1>&2"             { RET(REDIR_MERGE_OUT_2_ERR); }
    <CMD> "<<<"              { RET(REDIR_HERE_STR); }
    <CMD> ">("               { PUSH_MODE_SKIP_NL(STMT); RET(START_IN_SUB); }
    <CMD> "<("               { PUSH_MODE_SKIP_NL(STMT); RET(START_OUT_SUB); }

    <CMD> NEW_LINE           { if(!SKIPPABLE_NL()) { MODE(STMT); } UPDATE_LN(); FIND_NEW_LINE(); }

    <TYPE> "Func"            { RET_OR_COMP(FUNC); }
    <TYPE> "typeof"          { RET_OR_COMP(TYPEOF); }
    <TYPE> VAR_NAME          { RET_OR_COMP(TYPE_NAME); }
    <TYPE> "<"               { RET(TYPE_OPEN); }
    <TYPE> ">"               { RET(TYPE_CLOSE); }
    <TYPE> ","               { RET(TYPE_SEP); }
    <TYPE> "."               { RET(TYPE_DOT); }
    <TYPE> "["               { RET(ATYPE_OPEN); }
    <TYPE> "]"               { RET(ATYPE_CLOSE); }
    <TYPE> "("               { RET(PTYPE_OPEN); }
    <TYPE> ")"               { RET(PTYPE_CLOSE); }
    <TYPE> ":"               { RET(TYPE_MSEP); }
    <TYPE> "!" / [^=~]       { RET(TYPE_OPT); }
    <TYPE> ("=>" | "->")     { RET(TYPE_ARROW); }

    <PARAM> VAR_NAME         { MODE(EXPR); RET(PARAM_NAME); }
    <PARAM> APPLIED_NAME     { MODE(EXPR); RET(PARAM_NAME); }
    <PARAM> "("              { MODE(EXPR); PUSH_MODE_SKIP_NL(PARAM); RET(LP); }

    <STMT,EXPR,CMD> LINE_END { MODE(STMT); RET(LINE_END); }
    <STMT,EXPR,NAME,TYPE,PARAM> NEW_LINE
                             { UPDATE_LN(); FIND_NEW_LINE(); }

    <STMT,EXPR,NAME,CMD,TYPE,PARAM> COMMENT
                             { if(this->inCompletionPoint()) { setComplete(false); }
                               STORE_COMMENT(); SKIP(); }
    <STMT,EXPR,NAME,CMD,TYPE,PARAM> [ \t]+
                             { FIND_SPACE(); }
    <STMT,EXPR,NAME,CMD,TYPE,PARAM> "\\" [\r\n]
                             { UPDATE_LN(); SKIP(); }


    <STMT,EXPR,NAME,DSTRING,CMD,TYPE,PARAM> "\000" { REACH_EOS();}
    <STMT,EXPR,NAME,DSTRING,CMD,TYPE,PARAM> *      { RET(INVALID); }
  */

END:
  token.pos = startPos;
  token.size = this->getPos() - startPos;
  this->prevMode = mode;
  goto RET;

EOS:
  kind = TokenKind::EOS;
  token.pos = this->getUsedSize();
  token.size = 0;
  this->cursor--;
  foundNewLine = true; // previous char is always newline
  if (this->isComplete()) {
    this->setCompTokenKind(kind);
    kind = TokenKind::COMPLETION;
    foundNewLine = false;
  }
  goto RET;

RET:
  this->prevNewLine = foundNewLine;
  this->prevSpace = foundSpace;

  LOG(TRACE_TOKEN, "%s, %s, text = %s\n    lexer mode: %s", toString(kind), toString(token).c_str(),
      this->toTokenText(token).c_str(), this->getLexerMode().toString().c_str());
  return kind;
}

} // namespace ydsh