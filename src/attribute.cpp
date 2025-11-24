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

#include "attribute.h"
#include "type_pool.h"

namespace arsh {

const char *toString(AttributeKind k) {
  constexpr const char *table[] = {
#define GEN_STR(E, S) S,
      EACH_ATTRIBUTE_KIND(GEN_STR)
#undef GEN_STR
  };
  return table[toUnderlying(k)];
}

const char *toString(Attribute::Param p) {
  constexpr const char *table[] = {
#define GEN_STR(E, S, T) S,
      EACH_ATTRIBUTE_PARAM(GEN_STR)
#undef GEN_STR
  };
  return table[toUnderlying(p)];
}

const Type &getRequiredParamType(const TypePool &pool, Attribute::Param p) {
  constexpr TYPE table[] = {
#define GEN_TABLE(E, S, T) T,
      EACH_ATTRIBUTE_PARAM(GEN_TABLE)
#undef GEN_TABLE
  };
  return pool.get(table[toUnderlying(p)]);
}

const Attribute::Param *Attribute::lookupParam(StringRef paramName) const {
  if (auto iter = this->params.find(paramName); iter != this->params.end()) {
    return &iter->second;
  }
  return nullptr;
}

// ##########################
// ##     AttributeMap     ##
// ##########################

const Attribute *AttributeMap::lookup(StringRef name) const {
  if (auto iter = this->values.find(name); iter != this->values.end()) {
    return iter->second.get();
  }
  return nullptr;
}

static void defineAttribute(StrRefMap<std::unique_ptr<Attribute>> &values, AttributeKind kind,
                            Attribute::Loc loc, std::vector<Attribute::Param> &&params,
                            std::vector<TYPE> &&types) {
  StrRefMap<Attribute::Param> paramMap;
  for (auto &p : params) {
    paramMap.emplace(toString(p), p);
  }
  auto attr = std::make_unique<Attribute>(kind, loc, std::move(paramMap), std::move(types));
  StringRef attrName = attr->getName();
  values.emplace(attrName, std::move(attr));
}

AttributeMap AttributeMap::create() {
  StrRefMap<std::unique_ptr<Attribute>> values;

  defineAttribute(values, AttributeKind::CLI, Attribute::Loc::CONSTRUCTOR,
                  {
                      Attribute::Param::TOPLEVEL,
                      Attribute::Param::VERBOSE,
                      Attribute::Param::DESC,
                  },
                  {});
  defineAttribute(values, AttributeKind::FLAG, Attribute::Loc::FIELD,
                  {
                      Attribute::Param::SHORT,
                      Attribute::Param::LONG,
                      Attribute::Param::REQUIRED,
                      Attribute::Param::STOP,
                      Attribute::Param::STORE,
                      Attribute::Param::XOR,
                      Attribute::Param::HELP,
                  },
                  {TYPE::Bool});
  defineAttribute(values, AttributeKind::OPTION, Attribute::Loc::FIELD,
                  {
                      Attribute::Param::SHORT,
                      Attribute::Param::LONG,
                      Attribute::Param::REQUIRED,
                      Attribute::Param::OPT,
                      Attribute::Param::STOP,
                      Attribute::Param::DEFAULT,
                      Attribute::Param::PLACE_HOLDER,
                      Attribute::Param::RANGE,
                      Attribute::Param::CHOICE,
                      Attribute::Param::XOR,
                      Attribute::Param::HELP,
                      Attribute::Param::COMP,
                  },
                  {TYPE::String, TYPE::Int});
  defineAttribute(values, AttributeKind::ARG, Attribute::Loc::FIELD,
                  {
                      Attribute::Param::REQUIRED,
                      Attribute::Param::PLACE_HOLDER,
                      Attribute::Param::RANGE,
                      Attribute::Param::CHOICE,
                      Attribute::Param::HELP,
                  },
                  {TYPE::StringArray, TYPE::String, TYPE::Int});

  defineAttribute(values, AttributeKind::SUBCMD, Attribute::Loc::FIELD,
                  {
                      Attribute::Param::NAME,
                      Attribute::Param::HELP,
                  },
                  {TYPE::CLI});

  return AttributeMap(std::move(values));
}

} // namespace arsh