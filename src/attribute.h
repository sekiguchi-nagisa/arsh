/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef YDSH_ATTRIBUTE_H
#define YDSH_ATTRIBUTE_H

#include "type.h"

#define EACH_ATTRIBUTE_KIND(OP)                                                                    \
  OP(NONE, "None")                                                                                 \
  OP(CLI, "CLI")                                                                                   \
  OP(FLAG, "Flag")                                                                                 \
  OP(OPTION, "Option")                                                                             \
  OP(ARG, "Arg")

#define EACH_ATTRIBUTE_PARAM(OP)                                                                   \
  OP(VERBOSE, "verbose", TYPE::Bool)                                                               \
  OP(HELP, "help", TYPE::String)                                                                   \
  OP(SHORT, "short", TYPE::String)                                                                 \
  OP(LONG, "long", TYPE::String)                                                                   \
  OP(REQUIRED, "required", TYPE::Bool)                                                             \
  OP(STORE, "store", TYPE::Bool)                                                                   \
  OP(OPT, "opt", TYPE::Bool)                                                                       \
  OP(STOP, "stop", TYPE::Bool)                                                                     \
  OP(DEFAULT, "default", TYPE::String)                                                             \
  OP(PLACE_HOLDER, "placeholder", TYPE::String)                                                    \
  OP(RANGE, "range", TYPE::Void /* dummy type */)                                                  \
  OP(CHOICE, "choice", TYPE::StringArray)

namespace ydsh {

enum class AttributeKind : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_ATTRIBUTE_KIND(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(AttributeKind k);

class Attribute {
public:
  enum class Loc : unsigned char {
    NONE, // unresolved
    CONSTRUCTOR,
    FIELD,
  };

  enum class Param : unsigned char {
#define GEN_ENUM(E, S, T) E,
    EACH_ATTRIBUTE_PARAM(GEN_ENUM)
#undef GEN_ENUM
  };

private:
  const AttributeKind kind;
  const Loc loc;
  const StrRefMap<Param> params;
  const std::vector<const DSType *> types; // for field attributes

public:
  Attribute(AttributeKind kind, Loc loc, StrRefMap<Param> &&params,
            std::vector<const DSType *> &&types)
      : kind(kind), loc(loc), params(std::move(params)), types(std::move(types)) {}

  const char *getName() const { return toString(this->getKind()); }

  AttributeKind getKind() const { return this->kind; }

  Loc getLoc() const { return this->loc; }

  const auto &getParams() const { return this->params; }

  const Param *lookupParam(StringRef paramName) const;

  const auto &getTypes() const { return this->types; }
};

constexpr unsigned int getNumOfAttributeParams() {
  constexpr Attribute::Param params[] = {
#define GEN_TABLE(E, S, T) Attribute::Param::E,
      EACH_ATTRIBUTE_PARAM(GEN_TABLE)
#undef GEN_TABLE
  };
  return std::size(params);
}

const char *toString(Attribute::Param p);

const DSType &getRequiredParamType(const TypePool &pool, Attribute::Param p);

class AttributeMap {
private:
  StrRefMap<std::unique_ptr<Attribute>> values;

public:
  static AttributeMap create(const TypePool &pool);

  explicit AttributeMap(StrRefMap<std::unique_ptr<Attribute>> &&values)
      : values(std::move(values)) {}

  const Attribute *lookup(StringRef name) const;
};

class AttributeParamSet {
private:
  static_assert(getNumOfAttributeParams() < 32);

  unsigned int value{0};

public:
  void add(Attribute::Param p) {
    unsigned int v = toUnderlying(p);
    assert(v < 32);
    setFlag(this->value, 1u << v);
  }

  bool has(Attribute::Param p) const {
    unsigned int v = toUnderlying(p);
    assert(v < 32);
    return hasFlag(this->value, 1u << v);
  }

  template <typename Walker>
  static constexpr bool walker_requirement_v =
      std::is_same_v<void, std::invoke_result_t<Walker, Attribute::Param>>;

  template <typename Func, enable_when<walker_requirement_v<Func>> = nullptr>
  void iterate(Func func) const {
    constexpr unsigned int N = getNumOfAttributeParams();
    for (unsigned int i = 0; i < N; i++) {
      auto p = static_cast<Attribute::Param>(i);
      if (this->has(p)) {
        func(p);
      }
    }
  }
};

} // namespace ydsh

#endif // YDSH_ATTRIBUTE_H
