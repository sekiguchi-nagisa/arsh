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

class CodeGen {
private:
  std::string err;
  InstructionBuilder builder;
  InlinedStack<Modifier, 4> modifierStack;

public:
  const auto &getError() const { return this->err; }

  Optional<Regex> operator()(SyntaxTree &&tree);

private:
  [[nodiscard]] bool generate(const Node &node); // actual entry point

  Modifier modifiers() const { return this->modifierStack.back(); }

  void todo(const Node &node, const char *str = nullptr); // TODO: remove
};

} // namespace arsh::regex

#endif // ARSH_REGEX_EMIT_H
