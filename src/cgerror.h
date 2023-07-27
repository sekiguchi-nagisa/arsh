/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef YDSH_CGERROR_H
#define YDSH_CGERROR_H

#include "misc/resource.hpp"
#include "node.h"

namespace ydsh {
/**
 * for code generation error reporting
 */
class CodeGenError {
private:
  Token token;

  const char *kind{nullptr};

  CStrPtr message;

public:
  CodeGenError() = default;

  CodeGenError(Token token, const char *kind, CStrPtr &&message) noexcept
      : token(token), kind(kind), message(std::move(message)) {}

  CodeGenError(CodeGenError &&o) noexcept
      : token(o.token), kind(o.kind), message(std::move(o.message)) {
    o.kind = nullptr;
  }

  ~CodeGenError() = default;

  CodeGenError &operator=(CodeGenError &&o) noexcept {
    this->swap(o);
    return *this;
  }

  void swap(CodeGenError &o) {
    std::swap(this->token, o.token);
    std::swap(this->kind, o.kind);
    std::swap(this->message, o.message);
  }

  Token getToken() const { return this->token; }

  const char *getKind() const { return this->kind; }

  const char *getMessage() const { return this->message.get(); }

  explicit operator bool() const { return this->kind != nullptr; }
};

struct CGError {};

template <typename T, typename B>
using base_of_t = std::enable_if_t<std::is_base_of_v<B, T>, T>;

#define DEFINE_CGError(E, fmt)                                                                     \
  struct E : CGError {                                                                             \
    static constexpr const char *kind = #E;                                                        \
    static constexpr const char *value = fmt;                                                      \
  }

DEFINE_CGError(TooLargeFunc, "too large function: `%s'");
DEFINE_CGError(TooLargeUdc, "too large user-defined command: `%s'");
DEFINE_CGError(TooLargeToplevel, "too large top-level script: `%s'");

#undef DEFINE_CGError

} // namespace ydsh

#endif // YDSH_CGERROR_H
