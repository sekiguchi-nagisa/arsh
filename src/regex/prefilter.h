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

#ifndef ARSH_REGEX_PREFILTER_H
#define ARSH_REGEX_PREFILTER_H

#include <array>
#include <bitset>

#include "misc/array_ref.hpp"
#include "misc/enum_util.hpp"
#include "misc/string_ref.hpp"

namespace arsh::regex {

struct Inst;
class Matcher;

using FirstBitSet = std::bitset<256>;

class Prefilter {
public:
  enum class Type : unsigned char {
    NONE,   // do nothing
    BITSET, // UTF-8 first bitset
    CHAR,   // single code point
    STRING, // string
  }; // must not change enum order

  struct CodePoint {
    std::array<char, 4> data; // must be single UTF-8
    unsigned char size;       // up to 4
  };

private:
  Type type;
  unsigned int maxPrevCodePoints;
  union {
    CodePoint codePoint;
    StringRef ref;
    FirstBitSet set;
  };

public:
  Prefilter() : type(Type::NONE), maxPrevCodePoints(0), ref({}) {}

  Prefilter(const FirstBitSet &set, unsigned int maxCodePoints)
      : type(Type::BITSET), maxPrevCodePoints(maxCodePoints), set(set) {}

  Prefilter(const CodePoint &codePoint, unsigned int maxCodePoints)
      : type(Type::CHAR), maxPrevCodePoints(maxCodePoints), codePoint(codePoint) {}

  Prefilter(StringRef ref, unsigned int maxCodePoints)
      : type(Type::STRING), maxPrevCodePoints(maxCodePoints), ref(ref) {}

  Type getType() const { return this->type; }

  unsigned int getMaxPrevCodePoints() const { return this->maxPrevCodePoints; }

  const auto &asBitSet() const { return this->set; }

  const CodePoint &asCodePoint() const { return this->codePoint; }

  StringRef asStrRef() const { return this->ref; }

  bool isLowPriorityThan(const Prefilter &other) const {
    if (toUnderlying(this->type) < toUnderlying(other.getType())) {
      return true;
    }
    if (this->type == other.getType()) {
      switch (this->type) {
      case Type::NONE:
        return this->getMaxPrevCodePoints() < other.getMaxPrevCodePoints();
      case Type::CHAR:
        return this->asCodePoint().size < other.asCodePoint().size;
      case Type::BITSET:
        return this->asBitSet().count() > other.asBitSet().count();
      case Type::STRING:
        return this->asStrRef() < other.asStrRef();
      }
    }
    return false;
  }
};

FirstBitSet createFirstBitSet(ArrayRef<Inst> instSeq, ArrayRef<Matcher> matchers);

Prefilter createPrefilter(ArrayRef<Inst> instSeq, ArrayRef<Matcher> matchers);

} // namespace arsh::regex

#endif // ARSH_REGEX_PREFILTER_H
