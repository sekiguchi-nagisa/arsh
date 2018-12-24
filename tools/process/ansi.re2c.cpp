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

#include <cassert>

#include <ansi.h>
#include <misc/num.h>
#include <misc/fatal.h>

namespace process {

#define NEXT() continue
#define ERROR(m) fatal("unsupported escape sequence: %s\n", m);

/**
 * must be valid number
 * @param begin
 * @param end
 * @return
 */
static unsigned int toNum(const char *begin, const char *end) {
    unsigned int value = 0;
    for(; begin != end; ++begin) {
        char ch = *begin;
        assert(ydsh::isDecimal(ch));
        unsigned int n = ydsh::hexToNum(ch);
        value *= 10;
        value += n;
    }
    return value;
}

void Screen::interpret(const char *data, unsigned int size) {
    const char *begin = data;
    const char *end = begin + size;
    const char *marker = nullptr;

    const char *s0 = nullptr;
    const char *s1 = nullptr;
    const char *s2 = nullptr;
    const char *s3 = nullptr;


    /*!stags:re2c format = "const char *@@;\n"; */

#define YYCTYPE      char
#define YYPEEK()     *begin
#define YYSKIP()     do { if(begin++ == end) { return; } } while(false)
#define YYBACKUP()   marker = begin
#define YYRESTORE()  begin = marker
#define YYSTAGP(t)   t = begin


    while(true) {
        const char *start = begin;

    /*!re2c
      re2c:define:YYFILL:naked = 1;
      re2c:define:YYFILL@len = #;
      re2c:yyfill:enable = 0;
      re2c:indent:top = 1;
      re2c:indent:string = "    ";

      DECIMAL = '0' | [1-9][0-9]*;

      "\x1b[" @s0 DECIMAL @s1 'A'                     { ERROR("\\e[PnA"); }
      "\x1b[" @s0 DECIMAL @s1 'B'                     { ERROR("\\e[PnB"); }
      "\x1b[" @s0 DECIMAL @s1 'C'                     { this->right(toNum(s0, s1)); NEXT(); }
      "\x1b[" @s0 DECIMAL @s1 'D'                     { this->left(toNum(s0, s1)); NEXT(); }
      "\x1b[" @s0 DECIMAL @s1 ';' @s2 DECIMAL @s3 'H' { this->setCursor(toNum(s0, s1), toNum(s2, s3)); NEXT(); }
      "\x1b[H"                                        { this->setCursor(); NEXT(); }
      "\x1b[2J"                                       { this->clear(); NEXT(); }
      "\x1b[0K"                                       { this->clearLineFrom(); NEXT(); }
      "\x1b[6n"                                       { this->reportPos(); NEXT(); }

      *                                               { assert(begin - start == 1); this->addChar(*start); NEXT(); }
    */
    }
}

} // namespace process