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

#ifndef ARSH_REGEX_EMIT_H
#define ARSH_REGEX_EMIT_H

#include "instruction.h"
#include "misc/buffer.hpp"
#include "misc/inlined_stack.hpp"
#include "misc/result.hpp"
#include "node.h"
#include "regex.h"

namespace arsh::regex {

class InstructionBuilder {
public:
  struct ReservedPoint {
    const unsigned int offset;
    const unsigned int size;
  };

private:
  FlexBuffer<Inst> buf;

public:
  unsigned int currentAddr() const { return this->buf.size(); }

  template <typename T>
  ReservedPoint emitReservedPoint() {
    static_assert(std::is_base_of_v<Inst, T>);
    constexpr unsigned int instSize = sizeof(T);
    const unsigned int oldSize = this->buf.size();
    this->buf.resize(instSize + oldSize);
    return {.offset = oldSize, .size = instSize};
  }

  template <typename T, typename... Arg>
  void emitAt(ReservedPoint point, Arg &&...arg) {
    static_assert(std::is_base_of_v<Inst, T>);
    assert(point.size == sizeof(T));
    new (this->buf.data() + point.offset) T(std::forward<Arg>(arg)...);
  }

  template <typename T, typename... Arg>
  void emit(Arg &&...arg) {
    static_assert(std::is_base_of_v<Inst, T>);
    auto point = this->emitReservedPoint<T>();
    this->emitAt<T>(point, std::forward<Arg>(arg)...);
  }

  FlexBuffer<Inst> build() && { return std::move(this->buf); }
};

class StrSetNode;

class CodeGen {
private:
  std::string err;
  InstructionBuilder builder;
  std::vector<Matcher> matchers;
  std::vector<bool> directions; // true: forward, false: backward

public:
  const auto &getError() const { return this->err; }

  Optional<Regex> operator()(SyntaxTree &&tree);

private:
  [[nodiscard]] bool generate(Node &node); // actual entry point

  bool direction() const { return this->directions.back(); }

  bool inLookBehind() const { return !this->direction(); }

  bool generateSeq(const SeqNode &node);

  bool generateAlt(const AltNode &node);

  bool generateGroup(const GroupNode &node);

  bool generateStrSet(const StrSetNode &node);

  bool generateRepeat(const RepeatNode &node);

  bool generateLookAround(const LookAroundNode &node);

  Optional<unsigned short> emitMatcher(Matcher &&matcher);
};

} // namespace arsh::regex

#endif // ARSH_REGEX_EMIT_H
