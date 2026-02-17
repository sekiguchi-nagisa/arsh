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
  OP(Any)                                                                                          \
  OP(AnyExceptNL)                                                                                  \
  OP(Char)

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

/**
 * goto specified target
 */
struct JumpIns : InstWithRtti<OpCode::Jump> {
  uint8_t target[4];

  explicit JumpIns(uint32_t target) { memcpy(this->target, &target, sizeof(uint32_t)); }

  uint32_t getTarget() const {
    uint32_t t;
    memcpy(&t, this->target, sizeof(uint32_t));
    return t;
  }
};

/**
 * push second branch addr
 */
struct AltIns : InstWithRtti<OpCode::Alt> {
  uint8_t second[4]; // second branch

  explicit AltIns(uint32_t target) { memcpy(this->second, &target, sizeof(uint32_t)); }

  uint32_t getSecond() const {
    uint32_t t;
    memcpy(&t, this->second, sizeof(uint32_t));
    return t;
  }
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
 * match a single code point
 */
struct CharIns : InstWithRtti<OpCode::Char> {
  uint8_t code[4];

  explicit CharIns(int codePoint) { memcpy(this->code, &codePoint, sizeof(uint32_t)); }

  int32_t getCodePoint() const {
    int32_t codePoint;
    memcpy(&codePoint, this->code, sizeof(uint32_t));
    return codePoint;
  }
};

#define GEN_TYPE_ASSERT(E)                                                                         \
  static_assert(std::is_trivially_copyable_v<E##Ins> && std::is_trivially_destructible_v<E##Ins>);

EACH_RE_OPCODE(GEN_TYPE_ASSERT)

#undef GEN_TYPE_ASSERT

} // namespace arsh::regex

#endif // ARSH_REGEX_INSTRUCTION_H
