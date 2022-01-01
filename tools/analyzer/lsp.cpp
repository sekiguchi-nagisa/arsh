/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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

#include "lsp.h"

namespace ydsh::lsp {

const char *toString(TraceValue setting) {
  switch (setting) {
#define GEN_CASE(E)                                                                                \
  case TraceValue::E:                                                                              \
    return #E;
    EACH_TRACE_VALUE(GEN_CASE)
#undef GEN_CASE
  default:
    return "off";
  }
}

bool toEnum(const char *str, TraceValue &setting) {
  StringRef ref = str;
  TraceValue settings[] = {
#define GEN_ENUM(E) TraceValue::E,
      EACH_TRACE_VALUE(GEN_ENUM)
#undef GEN_ENUM
  };
  for (auto &e : settings) {
    if (ref == toString(e)) {
      setting = e;
      return true;
    }
  }
  setting = TraceValue::off;
  return false;
}

std::string Position::toString() const {
  std::string ret;
  ret += std::to_string(this->line);
  ret += ":";
  ret += std::to_string(this->character);
  return ret;
}

std::string Range::toString() const {
  std::string ret = "(";
  ret += this->start.toString();
  ret += "~";
  ret += this->end.toString();
  ret += ")";
  return ret;
}

const char *toString(const MarkupKind &kind) {
  switch (kind) {
#define GEN_CASE(E, V)                                                                             \
  case MarkupKind::E:                                                                              \
    return V;
    EACH_MARKUP_KIND(GEN_CASE)
#undef GEN_CASE
  default:
    return "";
  }
}

bool toEnum(const char *str, MarkupKind &kind) {
  StringRef ref = str;
  MarkupKind kinds[] = {
#define GEN_ENUM(E, V) MarkupKind::E,
      EACH_MARKUP_KIND(GEN_ENUM)
#undef GEN_ENUM
  };
  for (auto &e : kinds) {
    if (ref == toString(e)) {
      kind = e;
      return true;
    }
  }
  kind = MarkupKind::PlainText;
  return false;
}

} // namespace ydsh::lsp

namespace ydsh {

bool toEnum(const char *str, LogLevel &level) {
  LogLevel levels[] = {LogLevel::DEBUG, LogLevel::INFO, LogLevel::WARNING, LogLevel::ERROR,
                       LogLevel::FATAL};
  for (auto &l : levels) {
    const char *ls = toString(l);
    if (strcasecmp(ls, str) == 0) {
      level = l;
      return true;
    }
  }
  level = LogLevel::WARNING;
  return false;
}

} // namespace ydsh