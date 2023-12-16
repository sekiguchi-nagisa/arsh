/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#include <DescLexer.h>
#include <misc/enum_util.hpp>

// helper macro definition.
#define RET(k)                                                                                     \
  do {                                                                                             \
    kind = DescTokenKind::k;                                                                       \
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

DescTokenKind DescLexer::nextToken(Token &token) {
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

    IDENTIFIER = [_a-zA-Z][_a-zA-Z0-9]*;
  */

INIT:
  unsigned int startPos = this->getPos();
  DescTokenKind kind = DescTokenKind::INVALID;
  /*!re2c
    "//!bind:"             { RET(DESC_PREFIX); }
    "function"             { RET(FUNC); }
    "YDSH_METHOD"          { RET(YDSH_METHOD); }
    "YDSH_METHOD_DECL"     { RET(YDSH_METHOD_DECL); }
    "RuntimeContext"       { RET(RCTX); }
    "where"                { RET(WHERE); }
    IDENTIFIER             { RET(IDENTIFIER); }
    "<"                    { RET(TYPE_OPEN); }
    ">"                    { RET(TYPE_CLOSE); }
    "["                    { RET(PTYPE_OPEN); }
    "]"                    { RET(PTYPE_CLOSE); }
    "$" IDENTIFIER         { RET(VAR_NAME); }
    "("                    { RET(LP); }
    ")"                    { RET(RP); }
    ","                    { RET(COMMA); }
    ":"                    { RET(COLON); }
    ";"                    { RET(SEMI_COLON); }
    "{"                    { RET(LBC); }
    "&"                    { RET(AND); }

    [ \t\r\n]+             { SKIP(); }
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
  return DescTokenKind::EOS;
}

const char *toString(DescTokenKind kind) {
  const char *names[] = {
#define GEN_NAME(TOK) #TOK,
      EACH_DESC_TOKEN(GEN_NAME)
#undef GEN_NAME
  };

  return names[arsh::toUnderlying(kind)];
}