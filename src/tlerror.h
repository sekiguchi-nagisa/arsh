/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
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

#ifndef YDSH_TLERROR_H
#define YDSH_TLERROR_H

#include "misc/resource.hpp"

namespace ydsh {

class TypeLookupError {
private:
  const char *kind;
  CStrPtr message;
  int elementIndex{-1}; // for invalid type element

public:
  TypeLookupError(const char *kind, CStrPtr &&message) : kind(kind), message(std::move(message)) {}

  void setElementIndex(unsigned int index) { this->elementIndex = static_cast<int>(index); }

  int getElementIndex() const { return this->elementIndex; }

  ~TypeLookupError() = default;

  const char *getKind() const { return this->kind; }

  const char *getMessage() const { return this->message.get(); }

  CStrPtr takeMessage() && { return std::move(this->message); }
};

struct TLError {};

template <typename T, typename B>
using base_of_t = std::enable_if_t<std::is_base_of_v<B, T>, T>;

#define DEFINE_TLError(E, fmt)                                                                     \
  struct E : TLError {                                                                             \
    static constexpr const char *kind = #E;                                                        \
    static constexpr const char *value = fmt;                                                      \
  }

DEFINE_TLError(ElementLimit, "number of type elements reaches limit");
DEFINE_TLError(DefinedType, "already defined type: `%s'");
DEFINE_TLError(NotTemplate, "illegal type template: `%s'");
DEFINE_TLError(InvalidElement, "invalid type element: `%s'");
DEFINE_TLError(InvalidArgElement, "invalid type for ArgParser element: `%s', "
                                  "element type must have `ArgsDef' attribute");
DEFINE_TLError(InvalidArrayElement, "invalid type for Array element: `%s'");
DEFINE_TLError(InvalidMapKey, "invalid type for Map key: `%s', "
                              "Map key only allow value types (such as Int, String, ...)");
DEFINE_TLError(InvalidMapValue, "invalid type for Map element: `%s'");
DEFINE_TLError(InvalidTupleElement, "invalid type for Tuple elements: `%s'");
DEFINE_TLError(InvalidFuncParam, "invalid type for function parameters: `%s'");
DEFINE_TLError(UnmatchElement, "not match type element, `%s' requires %d type element, but is %d");

#undef DEFINE_TLError

std::unique_ptr<TypeLookupError> createTLErrorImpl(const char *kind, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

template <typename T, typename... Arg, typename = base_of_t<T, TLError>>
inline std::unique_ptr<TypeLookupError> createTLError(Arg &&...arg) {
  return createTLErrorImpl(T::kind, T::value, std::forward<Arg>(arg)...);
}

#define RAISE_TL_ERROR(e, ...) return Err(createTLError<e>(__VA_ARGS__))

} // namespace ydsh

#endif // YDSH_TLERROR_H
