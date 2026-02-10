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

#include "flag.h"
#include "misc/unicode.hpp"

namespace arsh::regex {

// ##################
// ##     Flag     ##
// ##################

std::string Flag::str() const {
  unsigned int set = 0;
  switch (this->mode()) {
  case Mode::LEGACY:
    break; // do nothing
  case Mode::UNICODE:
    setFlag(set, 1u << ('u' - 'a'));
    break;
  case Mode::UNICODE_SET:
    setFlag(set, 1u << ('v' - 'a'));
    break;
  }

#define GEN_IF(E, C, D)                                                                            \
  if (this->has(Modifier::E)) {                                                                    \
    setFlag(set, 1u << ((C) - 'a'));                                                               \
  }
  EACH_RE_MODIFIER(GEN_IF)
#undef GEN_IF

  std::string value;
  for (char c = 'a'; c <= 'z'; c++) {
    if (hasFlag(set, 1u << (c - 'a'))) {
      value += c;
    }
  }
  return value;
}

Optional<Flag> Flag::parse(const StringRef ref, const Mode defaultMode, std::string *err,
                           bool onlyModifier) {
  Optional<Mode> mode;
  Modifier modifiers = Modifier::NONE;
  const char *iter = ref.begin();
  const char *const end = ref.end();
  while (iter != end) {
    int codePoint = -1;
    StringRef utf8;
    if (unsigned int len = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint)) {
      utf8 = StringRef(iter, len);
      iter += len;
    } else {
      if (err) {
        *err += "invalid utf8 byte";
      }
      return {};
    }
    switch (codePoint) {
#define GEN_CASE(E, S, D)                                                                          \
  case S:                                                                                          \
    if (hasFlag(modifiers, Modifier::E)) {                                                         \
      if (err) {                                                                                   \
        *err += "`";                                                                               \
        *err += (S);                                                                               \
        *err += "' modifier has already been specified";                                           \
      }                                                                                            \
      return {};                                                                                   \
    }                                                                                              \
    setFlag(modifiers, Modifier::E);                                                               \
    continue;
      EACH_RE_MODIFIER(GEN_CASE)
#undef GEN_CASE
    case 'u':
      if (!onlyModifier) {
        if (mode.hasValue()) {
          if (err) {
            *err += "flag has already been specified";
          }
          return {};
        }
        mode = Mode::UNICODE;
        continue;
      }
      break;
    case 'v':
      if (!onlyModifier) {
        if (mode.hasValue()) {
          if (err) {
            *err += "flag has already been specified";
          }
          return {};
        }
        mode = Mode::UNICODE_SET;
        continue;
      }
      break;
    default:
      break;
    }
    if (err) {
      *err += onlyModifier ? "invalid modifier: `" : "invalid regex flag: `";
      *err += utf8;
      *err += '\'';
    }
    return {};
  }
  return Flag(mode.hasValue() ? mode.unwrap() : defaultMode, modifiers);
}

} // namespace arsh::regex
