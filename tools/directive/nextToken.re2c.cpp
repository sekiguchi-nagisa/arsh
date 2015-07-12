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

#include <directive_lexer.h>

// helper macro definition.
#define RET(k) do { kind = k; goto END; } while(0)

#define REACH_EOS() do { this->endOfString = true; goto EOS; } while(0)

#define SKIP() goto INIT

namespace ydsh {
namespace directive {

void Lexer::nextToken(Token & token) {
    /*!re2c
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

      IDENTIFIER = [_a-zA-Z][_a-zA-Z0-9]*;
      NUM = "0" | [1-9] [0-9]*;
      SQUOTE_CHAR = [^\r\n'\\] | '\\' [btnfr'\\];
      DQUOTE_CHAR = [^\r\n"\\] | '\\' [btnfr"\\];

      OTHER = .;
    */

    INIT:
    unsigned int startPos = this->getPos();
    TokenKind kind = INVALID;
    /*!re2c
      "$" IDENTIFIER       { RET(APPLIED_NAME); }
      [+-]? NUM            { RET(INT_LITERAL); }
      ['] SQUOTE_CHAR* ['] { RET(STRING_LITERAL); }
      ["] DQUOTE_CHAR* ["] { RET(STRING_LITERAL); }
      "["                  { RET(ARRAY_OPEN); }
      "]"                  { RET(ARRAY_CLOSE); }
      ","                  { RET(COMMA); }
      "="                  { RET(ASSIGN); }
      "("                  { RET(LP); }
      ")"                  { RET(RP); }
      "TRUE"               { RET(TRUE_LITERAL); }
      "True"               { RET(TRUE_LITERAL); }
      "true"               { RET(TRUE_LITERAL); }
      "FALSE"              { RET(FALSE_LITERAL); }
      "False"              { RET(FALSE_LITERAL); }
      "false"              { RET(FALSE_LITERAL); }

      [ \t]+               { SKIP(); }
      "\000"               { REACH_EOS(); }

      OTHER                { RET(INVALID); }
    */

    END:
    token.startPos = startPos;
    token.size = this->getPos() - startPos;
    token.kind = kind;
    return;

    EOS:
    token.startPos = this->limit - this->buf;
    token.size = 0;
    token.kind = EOS;
    return;
}

} // namespace directive
} // namespace ydsh

