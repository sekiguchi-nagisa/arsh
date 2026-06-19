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

namespace arsh::re262 {

// #####################
// ##     JSLexer     ##
// #####################

JSTokenKind JSLexer::nextToken(Token &token) {
  /*!re2c
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

    HEX = [0-9a-fA-F];
    INT = "0" | [1-9] ("_"? [0-9])*;
    HEX_INT = ("0x"|"0X") HEX ("_"? HEX)*;
    FRAC = "." [0-9]+;
    EXP = [eE] [+-] [0-9]+;
    SCHAR = "\\" [^\000] | [^\\'\000];
    DCHAR = "\\" [^\000] | [^\\"\000];
    ID_PART = [0-9A-Za-z_$];
    ID_START = [A-Za-z_$];

    RE_BS_SEQ = "\\" [^\000\r\n\u2028\u2029];
    RE_CLASS = "[" ( [^\000\r\n\u2028\u2029\\\]] | RE_BS_SEQ )*  "]";
    RE_FIRST_CHAR = [^\000\r\n\u2028\u2029*\\/[] | RE_BS_SEQ | RE_CLASS;
    RE_CHAR = [^\000\r\n\u2028\u2029\\/[] | RE_BS_SEQ | RE_CLASS;
    RE_BODY = RE_FIRST_CHAR RE_CHAR*;
    RE_FLAGS = ID_PART*;
    REGEX = "/" RE_BODY "/" RE_FLAGS;

    SINGLE_COMMENT = "//" [^\000\r\n\u2028\u2029]*;
    MULTI_COMMENT = "/"[*] [^\000]* [*]"/";
  */

  bool foundNewLine = false;

INIT:
  unsigned int startPos = this->getPos();
  JSTokenKind kind = JSTokenKind::INVALID;
  /*!re2c
    "true"                 { RET(TRUE); }
    "false"                { RET(FALSE); }
    "null"                 { RET(NIL); }
    "const"                { RET(CONST); }
    "this"                 { RET(KEYWORD); }
    "throw"                { RET(KEYWORD); }
    INT FRAC? EXP?         { RET(NUMBER); }
    HEX_INT                { RET(NUMBER); }
    ['] SCHAR* [']         { UPDATE_LN(); RET(STRING); }
    ["] DCHAR* ["]         { UPDATE_LN(); RET(STRING); }
    REGEX                  { RET(REGEX); }
    ID_START ID_PART*      { RET(IDENTIFIER); }
    "="                    { RET(ASSIGN); }
    "!"                    { RET(NOT); }
    "("                    { RET(LP); }
    ")"                    { RET(RP); }
    "{"                    { RET(LBC); }
    "}"                    { RET(RBC); }
    "["                    { RET(LB); }
    "]"                    { RET(RB); }
    ":"                    { RET(COLON); }
    ";"                    { RET(LINE_END); }
    ","                    { RET(COMMA); }
    "."                    { RET(DOT); }

    [ \t\v\f\u00A0\uFEFF]+ { SKIP(); }
    [\r\n\u2028\u2029]+    { UPDATE_LN(); FIND_NEW_LINE(); }
    SINGLE_COMMENT         { SKIP(); }
    MULTI_COMMENT          { UPDATE_LN(); SKIP(); }
    "\000"                 { REACH_EOS(); }

    *                      { RET(INVALID); }
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