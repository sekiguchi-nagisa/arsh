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

#ifndef ARSH_REGEX_INSTRUCTION_H
#define ARSH_REGEX_INSTRUCTION_H

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace arsh::regex {

#define EACH_RE_OPCODE(OP)                                                                         \
  OP(Nop)                                                                                          \
  OP(Match)                                                                                        \
  OP(Jump)                                                                                         \
  OP(Alt)                                                                                          \
  OP(Start)                                                                                        \
  OP(End)                                                                                          \
  OP(Word)                                                                                         \
  OP(IWord)                                                                                        \
  OP(Any)                                                                                          \
  OP(AnyExceptNL)                                                                                  \
  OP(LBAny)                                                                                        \
  OP(Grapheme)                                                                                     \
  OP(Char)                                                                                         \
  OP(IChar)                                                                                        \
  OP(LBChar)                                                                                       \
  OP(CharSet)                                                                                      \
  OP(ICharSet)                                                                                     \
  OP(LBCharSet)                                                                                    \
  OP(BeginCapture)                                                                                 \
  OP(EndCapture)                                                                                   \
  OP(LBEndCapture)                                                                                 \
  OP(ResetCaptures)                                                                                \
  OP(BackRef)                                                                                      \
  OP(IBackRef)                                                                                     \
  OP(LBBackRef)                                                                                    \
  OP(BeginLoop)                                                                                    \
  OP(EndLoop)                                                                                      \
  OP(BeginLookAround)                                                                              \
  OP(EndLookAround)

enum class OpCode : uint8_t {
#define GEN_ENUM(E) E,
  EACH_RE_OPCODE(GEN_ENUM)
#undef GEN_ENUM
};

struct Inst {
  OpCode op;

  Inst() = default;

protected:
  explicit Inst(OpCode op) : op(op) {}
};

template <OpCode OP>
struct InstWithRtti : Inst {
  static bool classof(const Inst *ins) { return ins->op == OP; }

  InstWithRtti() : Inst(OP) {}
};

/**
 * do nothing
 */
struct NopIns : InstWithRtti<OpCode::Nop> {};

/**
 * successfully match
 */
struct MatchIns : InstWithRtti<OpCode::Match> {};

#define WRITE_TO_INS_FIELD(src, dest)                                                              \
  static_assert(sizeof(this->dest) == sizeof(src));                                                \
  memcpy(this->dest, &(src), sizeof(src))

#define READ_FROM_INS_FIELD(src, dest)                                                             \
  static_assert(sizeof(this->src) == sizeof(dest));                                                \
  memcpy(&(dest), this->src, sizeof(dest))

/**
 * goto specified target
 */
struct JumpIns : InstWithRtti<OpCode::Jump> {
  uint8_t target[4];

  explicit JumpIns(uint32_t target) { WRITE_TO_INS_FIELD(target, target); }

  uint32_t getTarget() const {
    uint32_t t;
    READ_FROM_INS_FIELD(target, t);
    return t;
  }
};

/**
 * push second branch addr
 */
struct AltIns : InstWithRtti<OpCode::Alt> {
  uint8_t second[4]; // second branch

  explicit AltIns(uint32_t target) { WRITE_TO_INS_FIELD(target, second); }

  uint32_t getSecond() const {
    uint32_t t;
    READ_FROM_INS_FIELD(second, t);
    return t;
  }
};

/**
 * ^
 */
struct StartIns : InstWithRtti<OpCode::Start> {
  bool multiline;

  explicit StartIns(bool ml) : multiline(ml) {}
};

/**
 * $
 */
struct EndIns : InstWithRtti<OpCode::End> {
  bool multiline;

  explicit EndIns(bool ml) : multiline(ml) {}
};

/**
 * for word boundary (\\b \\B)
 */
struct WordIns : InstWithRtti<OpCode::Word> {
  bool invert;

  explicit WordIns(bool invert) : invert(invert) {}
};

/**
 * for word boundary (\\b \\B, ignore-case)
 */
struct IWordIns : InstWithRtti<OpCode::IWord> {
  bool invert;

  explicit IWordIns(bool invert) : invert(invert) {}
};

/**
 * match any code point (. with dot-all)
 */
struct AnyIns : InstWithRtti<OpCode::Any> {};

/**
 * match any code point except for newlines
 */
struct AnyExceptNLIns : InstWithRtti<OpCode::AnyExceptNL> {};

/**
 * match any code point within look-behind
 */
struct LBAnyIns : InstWithRtti<OpCode::LBAny> {
  bool dotAll;

  explicit LBAnyIns(bool dotAll) : dotAll(dotAll) {}
};

struct GraphemeIns : InstWithRtti<OpCode::Grapheme> {};

/**
 * match a single code point
 */
struct CharIns : InstWithRtti<OpCode::Char> {
  uint8_t code[4];

  explicit CharIns(int codePoint) { WRITE_TO_INS_FIELD(codePoint, code); }

  int32_t getCodePoint() const {
    int32_t codePoint;
    READ_FROM_INS_FIELD(code, codePoint);
    return codePoint;
  }
};

/**
 * match a single code point (ignore-case)
 */
struct ICharIns : InstWithRtti<OpCode::IChar> {
  uint8_t code[4];

  explicit ICharIns(int codePoint) { WRITE_TO_INS_FIELD(codePoint, code); }

  int32_t getCodePoint() const {
    int32_t codePoint;
    READ_FROM_INS_FIELD(code, codePoint);
    return codePoint;
  }
};

struct LBCharIns : InstWithRtti<OpCode::LBChar> {
  bool ignoreCase;
  uint8_t code[4];

  LBCharIns(int codePoint, bool ignoreCase) : ignoreCase(ignoreCase) {
    WRITE_TO_INS_FIELD(codePoint, code);
  }

  int32_t getCodePoint() const {
    int32_t codePoint;
    READ_FROM_INS_FIELD(code, codePoint);
    return codePoint;
  }
};

struct CharSetIns : InstWithRtti<OpCode::CharSet> {
  bool invert;
  uint8_t index[2];

  CharSetIns(uint16_t matcherIndex, bool invert) : invert(invert) {
    WRITE_TO_INS_FIELD(matcherIndex, index);
  }

  uint16_t getMatcherIndex() const {
    uint16_t matcherIndex;
    READ_FROM_INS_FIELD(index, matcherIndex);
    return matcherIndex;
  }
};

struct ICharSetIns : InstWithRtti<OpCode::ICharSet> {
  bool invert;
  uint8_t index[2];

  ICharSetIns(uint16_t matcherIndex, bool invert) : invert(invert) {
    WRITE_TO_INS_FIELD(matcherIndex, index);
  }

  uint16_t getMatcherIndex() const {
    uint16_t matcherIndex;
    READ_FROM_INS_FIELD(index, matcherIndex);
    return matcherIndex;
  }
};

struct LBCharSetIns : InstWithRtti<OpCode::LBCharSet> {
  bool invert;
  bool ignoreCase;
  uint8_t index[2];

  LBCharSetIns(uint16_t matcherIndex, bool invert, bool ignoreCase)
      : invert(invert), ignoreCase(ignoreCase) {
    WRITE_TO_INS_FIELD(matcherIndex, index);
  }

  uint16_t getMatcherIndex() const {
    uint16_t matcherIndex;
    READ_FROM_INS_FIELD(index, matcherIndex);
    return matcherIndex;
  }
};

struct BeginCaptureIns : InstWithRtti<OpCode::BeginCapture> {
  uint8_t captureIndex[2];

  explicit BeginCaptureIns(uint16_t index) { WRITE_TO_INS_FIELD(index, captureIndex); }

  uint16_t getCaptureIndex() const {
    uint16_t index;
    READ_FROM_INS_FIELD(captureIndex, index);
    return index;
  }
};

struct EndCaptureIns : InstWithRtti<OpCode::EndCapture> {
  uint8_t captureIndex[2];

  explicit EndCaptureIns(uint16_t index) { WRITE_TO_INS_FIELD(index, captureIndex); }

  uint16_t getCaptureIndex() const {
    uint16_t index;
    READ_FROM_INS_FIELD(captureIndex, index);
    return index;
  }
};

struct LBEndCaptureIns : InstWithRtti<OpCode::LBEndCapture> {
  uint8_t captureIndex[2];

  explicit LBEndCaptureIns(uint16_t index) { WRITE_TO_INS_FIELD(index, captureIndex); }

  uint16_t getCaptureIndex() const {
    uint16_t index;
    READ_FROM_INS_FIELD(captureIndex, index);
    return index;
  }
};

struct ResetCapturesIns : InstWithRtti<OpCode::ResetCaptures> {
  uint8_t firstIndex[2];
  uint8_t lastIndex[2];

  ResetCapturesIns(uint16_t first, uint16_t last) {
    WRITE_TO_INS_FIELD(first, firstIndex);
    WRITE_TO_INS_FIELD(last, lastIndex);
  }

  uint16_t getFirstIndex() const {
    uint16_t index;
    READ_FROM_INS_FIELD(firstIndex, index);
    return index;
  }

  uint16_t getLastIndex() const {
    uint16_t index;
    READ_FROM_INS_FIELD(lastIndex, index);
    return index;
  }
};

struct BackRefIns : InstWithRtti<OpCode::BackRef> {
  bool named;
  uint8_t index[2];

  BackRefIns(uint16_t refIndex, bool named) : named(named) { WRITE_TO_INS_FIELD(refIndex, index); }

  uint16_t getRefIndex() const {
    uint16_t refIndex;
    READ_FROM_INS_FIELD(index, refIndex);
    return refIndex;
  }
};

struct IBackRefIns : InstWithRtti<OpCode::IBackRef> {
  bool named;
  uint8_t index[2];

  IBackRefIns(uint16_t refIndex, bool named) : named(named) { WRITE_TO_INS_FIELD(refIndex, index); }

  uint16_t getRefIndex() const {
    uint16_t refIndex;
    READ_FROM_INS_FIELD(index, refIndex);
    return refIndex;
  }
};

struct LBBackRefIns : InstWithRtti<OpCode::LBBackRef> {
  bool named;
  bool ignoreCase;
  uint8_t index[2];

  LBBackRefIns(uint16_t refIndex, bool named, bool ignoreCase)
      : named(named), ignoreCase(ignoreCase) {
    WRITE_TO_INS_FIELD(refIndex, index);
  }

  uint16_t getRefIndex() const {
    uint16_t refIndex;
    READ_FROM_INS_FIELD(index, refIndex);
    return refIndex;
  }
};

struct BeginLoopIns : InstWithRtti<OpCode::BeginLoop> {
  bool greedy;
  uint8_t loopIndex[2];
  uint8_t min[2];
  uint8_t max[4];
  uint8_t outer[4];

  BeginLoopIns(uint16_t loopIndex, uint16_t min, uint32_t max, bool greedy, uint32_t outer)
      : greedy(greedy) {
    WRITE_TO_INS_FIELD(loopIndex, loopIndex);
    WRITE_TO_INS_FIELD(min, min);
    WRITE_TO_INS_FIELD(max, max);
    WRITE_TO_INS_FIELD(outer, outer);
  }

  uint16_t getLoopIndex() const {
    uint16_t index;
    READ_FROM_INS_FIELD(loopIndex, index);
    return index;
  }

  uint16_t getMin() const {
    uint16_t v;
    READ_FROM_INS_FIELD(min, v);
    return v;
  }

  uint32_t getMax() const {
    uint32_t v;
    READ_FROM_INS_FIELD(max, v);
    return v;
  }

  uint32_t getOuter() const {
    uint32_t v;
    READ_FROM_INS_FIELD(outer, v);
    return v;
  }
};

struct EndLoopIns : InstWithRtti<OpCode::EndLoop> {
  uint8_t target[4]; // must be begin loop addr

  explicit EndLoopIns(uint32_t addr) { WRITE_TO_INS_FIELD(addr, target); }

  uint32_t getTarget() const {
    uint32_t t;
    READ_FROM_INS_FIELD(target, t);
    return t;
  }
};

struct BeginLookAroundIns : InstWithRtti<OpCode::BeginLookAround> {
  bool negate;
  uint8_t target[4]; // must be the end of look-ahead addr

  BeginLookAroundIns(uint32_t target, bool negate) : negate(negate) {
    WRITE_TO_INS_FIELD(target, target);
  }

  uint32_t getTarget() const {
    uint32_t t;
    READ_FROM_INS_FIELD(target, t);
    return t;
  }
};

struct EndLookAroundIns : InstWithRtti<OpCode::EndLookAround> {};

#define GEN_TYPE_ASSERT(E)                                                                         \
  static_assert(std::is_trivially_copyable_v<E##Ins> && std::is_trivially_destructible_v<E##Ins>); \
  static_assert(alignof(E##Ins) == alignof(Inst));

EACH_RE_OPCODE(GEN_TYPE_ASSERT)

#undef GEN_TYPE_ASSERT

} // namespace arsh::regex

#endif // ARSH_REGEX_INSTRUCTION_H
