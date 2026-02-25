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

#ifndef ARSH_REGEX_MATCHER_H
#define ARSH_REGEX_MATCHER_H

#include "misc/codepoint_set.hpp"
#include "misc/flag_util.hpp"

namespace arsh::regex {

inline bool isLineTerminator(int codePoint) {
  return codePoint == '\n' || codePoint == '\r' || codePoint == 0x2028 || codePoint == 0x2029;
}

inline bool isWord(int codePoint) {
  return (codePoint >= 'A' && codePoint <= 'Z') || (codePoint >= 'a' && codePoint <= 'z') ||
         (codePoint >= '0' && codePoint <= '9') || codePoint == '_';
}

class AsciiSet {
private:
  uint64_t sets[2]{};

public:
  AsciiSet(uint64_t first, uint64_t second) : sets{first, second} {}

  void add(int codePoint) {
    if (codePoint <= 127 && codePoint > -1) {
      setFlag(this->sets[static_cast<unsigned int>(codePoint) / 64],
              static_cast<uint64_t>(1) << (static_cast<unsigned int>(codePoint) % 64));
    }
  }

  bool contains(int codePoint) const {
    if (codePoint <= 127 && codePoint > -1) {
      return hasFlag(this->sets[static_cast<unsigned int>(codePoint) / 64],
                     static_cast<uint64_t>(1) << (static_cast<unsigned int>(codePoint) % 64));
    }
    return false;
  }

  const auto &getSets() const { return this->sets; }
};

enum class MatcherType : unsigned char {
  ASCII,
  OWNED_CODE_POINT_SET,
  BORROWED_CODE_POINT_SET,
};

} // namespace arsh::regex

#endif // ARSH_REGEX_MATCHER_H
