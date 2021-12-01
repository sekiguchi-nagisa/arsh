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

#include <cstring>
#include <utility>

#include <uri.h>

namespace ydsh::uri {

#define ERROR()                                                                                    \
  do {                                                                                             \
    goto ERROR;                                                                                    \
  } while (false)

static std::string create(const char *begin, const char *end) {
  if (begin == nullptr || end == nullptr) {
    return "";
  }
  return URI::decode(begin, end);
}

using Range = std::pair<const char *, const char *>;

static std::string createPath(Range &&r1, Range &&r2) {
  if (r1.first != nullptr && r1.second != nullptr) {
    return create(r1.first, r1.second);
  }
  return create(r2.first, r2.second);
}

URI URI::fromString(const std::string &str) {
  // for scheme
  const char *s0 = nullptr;
  const char *s1 = nullptr;
  // for user_info
  const char *u0 = nullptr;
  const char *u1 = nullptr;
  // for host
  const char *h0 = nullptr;
  const char *h1 = nullptr;
  // for port
  const char *o0 = nullptr;
  const char *o1 = nullptr;
  // for path
  const char *p0 = nullptr;
  const char *p1 = nullptr;
  const char *p2 = nullptr;
  const char *p3 = nullptr;
  // for query
  const char *q0 = nullptr;
  const char *q1 = nullptr;
  // for fragment
  const char *f0 = nullptr;
  const char *f1 = nullptr;

  /*!stags:re2c format = "const char *@@;\n"; */

  const char *cursor = str.c_str();
  const char *marker = nullptr;

  /**
   * see. http://re2c.org/examples/example_10.html
   *      https://tools.ietf.org/html/rfc3986
   */

  /*!re2c
    re2c:define:YYCTYPE = "char";
    re2c:define:YYCURSOR = cursor;
    re2c:define:YYLIMIT = limit;
    re2c:define:YYMARKER = marker;
    re2c:define:YYFILL:naked = 1;
    re2c:define:YYFILL@len = #;
    re2c:yyfill:enable = 0;
    re2c:indent:top = 1;
    re2c:indent:string = "    ";

    ALPHA = [a-zA-Z];
    CR = "\r";
    DIGIT = [0-9];
    DQUOTE = ["];
    HEXDIG = DIGIT | [a-fA-F];
    LF = "\n";
    SP = " ";

    pct_encoded = "%" HEXDIG HEXDIG;
    gen_delims = [:/?#[\]@];
    sub_delims = [!$&'()*+,;=];
    reserved = gen_delims | sub_delims;
    unreserved = ALPHA | DIGIT | [-._~];
    pchar = unreserved | pct_encoded | sub_delims | ":" | "@";

    scheme = @s0 ALPHA ( ALPHA | DIGIT | "+" | "-" | "." )* @s1;

    user_info = @u0 (unreserved | pct_encoded | sub_delims | ":")* @u1;
    port = @o0 DIGIT* @o1;

    dec_octet = DIGIT
              | [\x31-\x39] DIGIT
              | "1" DIGIT{2}
              | "2" [\x30-\x34] DIGIT
              | "25" [\x30-\x35];
    IPv4address = dec_octet "." dec_octet "." dec_octet "." dec_octet;
    IPvFuture = "v" HEXDIG+ "." (unreserved | sub_delims | ":")+;
    h16 = HEXDIG{1,4};
    ls32 = (h16 ":" h16) | IPv4address;
    IPv6address =                            (h16 ":"){6} ls32
                |                       "::" (h16 ":"){5} ls32
                | (               h16)? "::" (h16 ":"){4} ls32
                | ((h16 ":"){0,1} h16)? "::" (h16 ":"){3} ls32
                | ((h16 ":"){0,2} h16)? "::" (h16 ":"){2} ls32
                | ((h16 ":"){0,3} h16)? "::"  h16 ":"     ls32
                | ((h16 ":"){0,4} h16)? "::"              ls32
                | ((h16 ":"){0,5} h16)? "::"              h16
                | ((h16 ":"){0,6} h16)? "::";
    IP_literal = "[" (IPv6address | IPvFuture) "]";
    reg_name = (unreserved | pct_encoded | sub_delims)*;
    host = @h0 (IP_literal | IPv4address | reg_name) @h1;
    authority = (user_info "@")? host (":" port)?;

    segment = pchar*;
    segment_nz = pchar+;
    segment_nz_nc = (unreserved | pct_encoded | sub_delims | "@")+;
    path_abempty = ("/" segment)*;
    path_absolute = "/" (segment_nz ("/" segment)* )?;
    path_rootless = segment_nz ("/" segment)*;
    path_empty = "";
    hier_part = "//" authority @p0 path_abempty @p1
              | @p2 (path_absolute | path_rootless | path_empty) @p3;

    query = @q0 (pchar | "/" | "?")* @q1;
    fragment = @f0 (pchar | "/" | "?")* @f1;

    uri = scheme ":" hier_part ("?" query)? ("#" fragment)?;

    uri    {
        auto auth = Authority(create(u0, u1), create(h0, h1), create(o0, o1));
        auto path = createPath({p0, p1}, {p2, p3});
        return URI(create(s0, s1), std::move(auth), std::move(path), create(q0, q1), create(f0,
    f1));
     }
    "\000"   { ERROR(); }

    *        { ERROR(); }
  */

ERROR:
  return URI();
}

} // namespace ydsh::uri