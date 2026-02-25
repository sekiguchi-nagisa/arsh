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
  uint64_t high; // 64-127
  uint64_t low;  // 0-63

public:
  AsciiSet(uint64_t head, uint64_t tail) : high(head), low(tail) {}

  bool contains(const int codePoint) const {
    if (codePoint < 128 && codePoint > -1) {
      if (codePoint < 64) {
        return hasFlag(this->low, static_cast<uint64_t>(1) << static_cast<uint64_t>(codePoint));
      }
      return hasFlag(this->high, static_cast<uint64_t>(1) << static_cast<uint64_t>(codePoint - 64));
    }
    return false;
  }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_MATCHER_H
