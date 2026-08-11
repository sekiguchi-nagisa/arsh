/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#include "js_lexer.h"

// helper macro definition.
#define RET(k)                                                                                     \
  do {                                                                                             \
    kind = JSTokenKind::k;                                                                         \
    goto END;                                                                                      \
  } while (false)

#define REACH_EOS()                                                                                \
  do {                                                                                             \
    if (this->isEnd()) {                                                                           \
      goto EOS;                                                                                    \
    } else {                                                                                       \
      ERROR();                                                                                     \
    }                                                                                              \
  } while (false)

#define UPDATE_LN() this->updateNewline(startPos)

#define SKIP() goto INIT

#define FIND_NEW_LINE()                                                                            \
  do {                                                                                             \
    foundNewLine = true;                                                                           \
    SKIP();                                                                                        \
  } while (false)

#define ERROR()                                                                                    \
  do {                                                                                             \
    RET(INVALID);                                                                                  \
  } while (false)

#define POP_MODE() this->popMode()

#define PUSH_MODE(m) this->pushMode(yyc##m)

namespace arsh::re262 {

// #####################
// ##     JSLexer     ##
// #####################

JSTokenKind JSLexer::nextToken(Token &token) {
  /*!re2c
    re2c:define:YYCONDTYPE = "JSLexerMode : unsigned char";
    re2c:define:YYGETCONDITION = this->getMode;
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = this->cursor;
    re2c:define:YYLIMIT = this->limit;
    re2c:define:YYMARKER = this->marker;
    re2c:define:YYCTXMARKER = this->ctxMarker;
    re2c:define:YYFILL:naked = 1;
    re2c:define:YYFILL@len = #;
    re2c:define:YYFILL = "if(!this->fill(#)) { REACH_EOS(); }";
    re2c:yyfill:enable = 0;
    re2c:eof = 0;
    re2c:indent:top = 1;
    re2c:indent:string = "    ";

    HEX = [0-9a-fA-F];
    INT = "0" | [1-9] ("_"? [0-9])*;
    HEX_INT = ("0x"|"0X") HEX ("_"? HEX)*;
    FRAC = "." [0-9]+;
    EXP = [eE] [+-] [0-9]+;
    SCHAR = "\\" [^] | [^\\'];
    DCHAR = "\\" [^] | [^\\"];
    TCHAR = "\\" [^] | [^\\`$];
    ID_PART = [0-9A-Za-z_$];
    ID_START = [A-Za-z_$];

    RE_BS_SEQ = "\\" [^\r\n\u2028\u2029];
    RE_CLASS = "[" ( [^\r\n\u2028\u2029\\\]] | RE_BS_SEQ )*  "]";
    RE_FIRST_CHAR = [^\r\n\u2028\u2029*\\/[] | RE_BS_SEQ | RE_CLASS;
    RE_CHAR = [^\r\n\u2028\u2029\\/[] | RE_BS_SEQ | RE_CLASS;
    RE_BODY = RE_FIRST_CHAR RE_CHAR*;
    RE_FLAGS = ID_PART*;
    REGEX = "/" RE_BODY "/" RE_FLAGS;

    SINGLE_COMMENT = "//" [^\r\n\u2028\u2029]*;
    MULTI_COMMENT = "/"[*] ([^*] | ([*] [^/]) )* [*]"/";
  */

  bool foundNewLine = false;

INIT:
  unsigned int startPos = this->getPos();
  JSTokenKind kind = JSTokenKind::INVALID;
  /*!re2c
    <DEFAULT>  "true"                 { RET(TRUE); }
    <DEFAULT>  "false"                { RET(FALSE); }
    <DEFAULT>  "null"                 { RET(NIL); }
    <DEFAULT>  "const"                { RET(CONST); }
    <DEFAULT>  "let"                  { RET(LET); }
    <DEFAULT>  "var"                  { RET(VAR); }
    <DEFAULT>  "return"               { RET(RETURN); }
    <DEFAULT>  "new"                  { RET(NEW); }
    <DEFAULT>  "function"             { RET(FUNCTION); }
    <DEFAULT>  "typeof"               { RET(TYPEOF); }
    <DEFAULT>  "void"                 { RET(VOID); }
    <DEFAULT>  "instanceof"           { RET(INSTANCEOF); }
    <DEFAULT>  "try"                  { RET(TRY); }
    <DEFAULT>  "catch"                { RET(CATCH); }
    <DEFAULT>  "finally"              { RET(FINALLY); }
    <DEFAULT>  "throw"                { RET(THROW); }
    <DEFAULT>  "if"                   { RET(IF); }
    <DEFAULT>  "else"                 { RET(ELSE); }
    <DEFAULT>  "for"                  { RET(FOR); }
    <DEFAULT>  "of"                   { RET(OF); }
    <DEFAULT>  "break"                { RET(BREAK); }
    <DEFAULT>  "continue"             { RET(CONTINUE); }
    <DEFAULT>  "while"                { RET(WHILE); }
    <DEFAULT>  "this"                 { RET(KEYWORD); }
    <DEFAULT>  "case"                 { RET(KEYWORD); }
    <DEFAULT>  "class"                { RET(KEYWORD); }
    <DEFAULT>  "default"              { RET(KEYWORD); }
    <DEFAULT>  "do"                   { RET(KEYWORD); }
    <DEFAULT>  "with"                 { RET(KEYWORD); }
    <DEFAULT>  INT FRAC? EXP?         { RET(NUMBER); }
    <DEFAULT>  HEX_INT                { RET(NUMBER); }
    <DEFAULT>  ['] SCHAR* [']         { UPDATE_LN(); RET(STRING); }
    <DEFAULT>  ["] DCHAR* ["]         { UPDATE_LN(); RET(STRING); }
    <DEFAULT>  REGEX                  { RET(REGEX); }
    <DEFAULT>  ID_START ID_PART*      { RET(IDENTIFIER); }
    <DEFAULT>  "="                    { RET(ASSIGN); }
    <DEFAULT>  "+="                   { RET(ADD_ASSIGN); }
    <DEFAULT>  "-="                   { RET(SUB_ASSIGN); }
    <DEFAULT>  "%="                   { RET(MOD_ASSIGN); }
    <DEFAULT>  "!"                    { RET(NOT); }
    <DEFAULT>  "+"                    { RET(ADD); }
    <DEFAULT>  "-"                    { RET(SUB); }
    <DEFAULT>  "%"                    { RET(MOD); }
    <DEFAULT>  "++"                   { RET(INC); }
    <DEFAULT>  "--"                   { RET(DEC); }
    <DEFAULT>  "==="                  { RET(EQ2); }
    <DEFAULT>  "!=="                  { RET(NE2); }
    <DEFAULT>  "<"                    { RET(LT); }
    <DEFAULT>  "<="                   { RET(LE); }
    <DEFAULT>  ">"                    { RET(GT); }
    <DEFAULT>  ">="                   { RET(GE); }
    <DEFAULT>  "&&"                   { RET(COND_AND); }
    <DEFAULT>  "||"                   { RET(COND_OR); }
    <DEFAULT>  "("                    { RET(LP); }
    <DEFAULT>  ")"                    { RET(RP); }
    <DEFAULT>  "{"                    { PUSH_MODE(DEFAULT); RET(LBC); }
    <DEFAULT>  "}"                    { POP_MODE(); RET(RBC); }
    <DEFAULT>  "["                    { RET(LB); }
    <DEFAULT>  "]"                    { RET(RB); }
    <DEFAULT>  ":"                    { RET(COLON); }
    <DEFAULT>  ";"                    { RET(LINE_END); }
    <DEFAULT>  ","                    { RET(COMMA); }
    <DEFAULT>  "."                    { RET(DOT); }
    <DEFAULT>  "`"                    { PUSH_MODE(TEMPLATE); RET(BACKTICK); }
    <DEFAULT>  [ \t\v\f\u00A0\uFEFF]+ { SKIP(); }
    <DEFAULT>  [\r\n\u2028\u2029]+    { UPDATE_LN(); FIND_NEW_LINE(); }
    <DEFAULT>  SINGLE_COMMENT         { SKIP(); }
    <DEFAULT>  MULTI_COMMENT          { UPDATE_LN(); SKIP(); }

    <TEMPLATE>  TCHAR                 { UPDATE_LN(); RET(STRING); }
    <TEMPLATE>  "$" / [^{]            { RET(STRING); }
    <TEMPLATE>  "${"                  { PUSH_MODE(DEFAULT); RET(START_INTERP); }
    <TEMPLATE>  "`"                   { POP_MODE(); RET(BACKTICK); }

    <DEFAULT,TEMPLATE>  $             { REACH_EOS(); }

    <DEFAULT,TEMPLATE>  *             { RET(INVALID); }
  */

END:
  token.pos = startPos;
  token.size = this->getPos() - startPos;
  goto RET;

EOS:
  kind = JSTokenKind::EOS;
  token.pos = this->getUsedSize();
  token.size = 0;
  this->cursor--;
  goto RET;

RET:
  this->prevNewLine = foundNewLine;
  if (this->verbose) {
    fprintf(stderr, "(%s, %s)\n", re262::toString(kind), this->toTokenText(token).c_str());
  }
  return kind;
}

} // namespace arsh::re262