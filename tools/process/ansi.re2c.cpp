/*
 * Copyright (C) 2018-2020 Nagisa Sekiguchi
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
#include <misc/fatal.h>
#include <misc/num_util.hpp>

namespace process {

#define NEXT()                                                                                     \
  do {                                                                                             \
    this->state = -1;                                                                              \
    goto INIT;                                                                                     \
  } while (false)
#define ERROR(m) fatal("unsupported escape sequence: %s\n", m)
#define RET(S)                                                                                     \
  do {                                                                                             \
    this->start = this->cursor;                                                                    \
    return S;                                                                                      \
  } while (false)

/**
 * must be valid number
 * @param begin
 * @param end
 * @return
 */
static unsigned int toNum(const char *begin, const char *end) {
  unsigned int value = 0;
  for (; begin != end; ++begin) {
    char ch = *begin;
    assert(arsh::isDecimal(ch));
    unsigned int n = arsh::hexToNum(ch);
    value *= 10;
    value += n;
  }
  return value;
}

static unsigned int parseEscape(const char *begin, const char *end) {
  assert(strstr(begin, "\x1b[") == begin);
  begin += strlen("\x1b[");
  end -= 1;
  return toNum(begin, end);
}

static std::pair<unsigned int, unsigned int> parseEscape2(const char *begin, const char *end) {
  assert(strstr(begin, "\x1b[") == begin);
  begin += strlen("\x1b[");
  end -= 1;
  auto *ptr = strchr(begin, ';');
  assert(ptr);
  auto v1 = toNum(begin, ptr);
  auto v2 = toNum(ptr + 1, end);
  return {v1, v2};
}

Screen::Result Screen::interpret(const char *data, unsigned int size) {
  this->appendToBuf(data, size);

#define YYGETSTATE() this->state
#define YYSETSTATE(s) this->state = s
#define YYFILL(n) return Result::NEED_MORE

INIT:
  if (this->state == -1) {
    this->start = this->cursor;
  }

  /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:define:YYCURSOR = this->cursor;
    re2c:define:YYLIMIT = this->limit;
    re2c:define:YYMARKER = this->marker;
    re2c:variable:yych = this->yych;
    re2c:eof = 0;
    re2c:indent:top = 1;
    re2c:indent:string = "    ";

    DECIMAL = "0" | [1-9][0-9]*;

    "\x1b[" DECIMAL "A"                     { this->up(parseEscape(start, cursor)); NEXT(); }
    "\x1b[" DECIMAL "B"                     { this->down(parseEscape(start, cursor)); NEXT(); }
    "\x1b[" DECIMAL "C"                     { this->right(parseEscape(start, cursor)); NEXT(); }
    "\x1b[" DECIMAL "D"                     { this->left(parseEscape(start, cursor)); NEXT(); }
    "\x1b[" DECIMAL ";" DECIMAL "H"         { auto p = parseEscape2(start, cursor);
                                              this->setCursor(Pos{.row=p.first, .col=p.second});
                                              NEXT(); }
    "\x1b[" DECIMAL (";" DECIMAL)* "m"      { NEXT(); }
    "\x1b[H"                                { this->setCursor(); NEXT(); }
    "\x1b[J"                                { this->clearFromDown(); NEXT(); }
    "\x1b[0J"                               { this->clearFromDown(); NEXT(); }
    "\x1b[2J"                               { this->clear(); NEXT(); }
    "\x1b[0K"                               { this->clearLineFrom(); NEXT(); }
    "\x1b[2K"                               { this->clearLine(); NEXT(); }
    "\x1b[6n"                               { this->reportPos(); NEXT(); }
    "\x1b[?2004h"                           { NEXT(); }
    "\x1b[?2004l"                           { NEXT(); }
    "\x1b[?" [0-9]+ "h"                     { NEXT(); }
    "\x1b[?" [0-9]+ "l"                     { NEXT(); }

    [^]                                     { this->addCodePoint(start, cursor); NEXT(); }
    *                                       { RET(Result::INVALID); }
    $                                       { RET(Result::REACH_EOS); }
  */

  fatal("normally unreachable\n");
}

} // namespace process