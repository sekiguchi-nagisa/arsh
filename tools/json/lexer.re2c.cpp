/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#include "json.h"

// helper macro definition.
#define RET(k)                                                                                     \
  do {                                                                                             \
    kind = JSONTokenKind::k;                                                                       \
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

#define ERROR()                                                                                    \
  do {                                                                                             \
    RET(INVALID);                                                                                  \
  } while (false)

namespace arsh::json {

JSONTokenKind JSONLexer::nextToken(Token &token) {
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

    INT = "0" | [1-9] [0-9]*;
    FRAC = "." [0-9]+;
    EXP = [eE] [+-] [0-9]+;
    UNESCAPED = [\x20\x21\x23-\x5B\x5D-\U0010FFFF];
    HEX = [0-9a-fA-F];
    CHAR = UNESCAPED | "\\" ( ["\\/bfnrt] | "u" HEX{4} );
  */

INIT:
  unsigned int startPos = this->getPos();
  JSONTokenKind kind = JSONTokenKind::INVALID;
  /*!re2c
    "true"                 { RET(TRUE); }
    "false"                { RET(FALSE); }
    "null"                 { RET(NIL); }
    "-"? INT FRAC? EXP?    { RET(NUMBER); }
    ["] CHAR* ["]          { RET(STRING); }
    "["                    { RET(ARRAY_OPEN); }
    "]"                    { RET(ARRAY_CLOSE); }
    "{"                    { RET(OBJECT_OPEN); }
    "}"                    { RET(OBJECT_CLOSE); }
    ","                    { RET(COMMA); }
    ":"                    { RET(COLON); }

    [ \t\r\n]+             { UPDATE_LN(); SKIP(); }
    "\000"                 { REACH_EOS(); }

    *                      { RET(INVALID); }
  */

END:
  token.pos = startPos;
  token.size = this->getPos() - startPos;
  return kind;

EOS:
  token.pos = this->getUsedSize();
  token.size = 0;
  this->cursor--;
  return JSONTokenKind::EOS;
}

} // namespace arsh::json