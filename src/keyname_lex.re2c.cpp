/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#include "keyname_lex.h"

// helper macro definition.
#define RET(k)                                                                                     \
  do {                                                                                             \
    kind = KeyNameTokenKind::k;                                                                    \
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

#define SKIP() goto INIT

#define ERROR()                                                                                    \
  do {                                                                                             \
    RET(INVALID);                                                                                  \
  } while (false)

namespace arsh {

KeyNameTokenKind KeyNameLexer::nextToken(Token &token) {
  /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = this->cursor;
    re2c:define:YYLIMIT = this->limit;
    re2c:define:YYMARKER = this->marker;
    re2c:define:YYCTXMARKER = this->ctxMarker;
    re2c:define:YYFILL:naked = 1;
    re2c:define:YYFILL@len = #;
    re2c:yyfill:enable = 0;
    re2c:indent:top = 1;
    re2c:indent:string = "  ";

    PRINTABLE_ASCII_WO_PLUS_MINUS_SP = [!"#$%&'()*,./:;<=>?@[\\\]^_`{|}~a-zA-Z0-9];
  */

INIT:
  unsigned int startPos = this->getPos();
  auto kind = KeyNameTokenKind::INVALID;
  /*!re2c
    "+"                              { RET(PLUS); }
    "-"                              { RET(MINUS); }
    PRINTABLE_ASCII_WO_PLUS_MINUS_SP { RET(ASCII_CHAR); }
    [_a-zA-Z][_a-zA-Z0-9]+           { RET(IDENTIFIER); }

    [ \t\r\n]+                       { SKIP(); }
    "\000"                           { REACH_EOS(); }

    *                                { RET(INVALID); }
  */

END:
  token.pos = startPos;
  token.size = this->getPos() - startPos;
  return kind;

EOS:
  token.pos = this->getUsedSize();
  token.size = 0;
  this->cursor--;
  return KeyNameTokenKind::EOS;
}

} // namespace arsh