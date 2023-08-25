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

#include "type_checker.h"

namespace ydsh {

const char *toString(Attribute::Param p) {
  const char *table[] = {
#define GEN_STR(E, S, T) S,
      EACH_ATTRIBUTE_PARAM(GEN_STR)
#undef GEN_STR
  };
  return table[toUnderlying(p)];
}

const DSType &getRequiredParamType(const TypePool &pool, Attribute::Param p) {
  const TYPE table[] = {
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

static void defineAttribute(StrRefMap<std::unique_ptr<Attribute>> &values, const char *name,
                            Attribute::Kind kind, Attribute::Loc loc,
                            std::vector<Attribute::Param> &&params,
                            std::vector<const DSType *> &&types) {
  StrRefMap<Attribute::Param> paramMap;
  for (auto &p : params) {
    paramMap.emplace(toString(p), p);
  }
  auto attr = std::make_unique<Attribute>(name, kind, loc, std::move(paramMap), std::move(types));
  StringRef attrName = attr->getName();
  values.emplace(attrName, std::move(attr));
}

AttributeMap AttributeMap::create(const TypePool &) {
  StrRefMap<std::unique_ptr<Attribute>> values;

  defineAttribute(values, "CLI", Attribute::Kind::CLI, Attribute::Loc::CONSTRUCTOR, {}, {});

  return AttributeMap(std::move(values));
}

void TypeChecker::visitAttributeNode(AttributeNode &node) {
  if (node.getLoc() == AttributeNode::Loc::OTHER) {
    this->reportError<AttrLoc>(node);
    return;
  }

  auto *attr = this->attributeMap.lookup(node.getAttrName());
  if (!attr) {
    auto &nameInfo = node.getAttrNameInfo();
    this->reportError<UndefinedAttr>(nameInfo.getToken(), nameInfo.getName().c_str());
    return;
  }

  // check attribute params
  AttributeParamSet paramSet;
  const unsigned int paramSize = node.getKeys().size();
  unsigned int validCount = 0;
  for (unsigned int i = 0; i < paramSize; i++) {
    // check param existence
    auto &key = node.getKeys()[i];
    auto *p = attr->lookupParam(key.getName());
    if (!p) {
      this->reportError<UndefinedAttrParam>(key.getToken(), key.getName().c_str(),
                                            attr->getName().c_str());
      break;
    }
    if (paramSet.has(*p)) {
      this->reportError<DupAttrParam>(key.getToken(), key.getName().c_str());
      break;
    }
    paramSet.add(*p);

    // check param type
    auto &paramType = getRequiredParamType(this->pool, *p); // FIXME: replace Void
    auto &exprNode = node.getValueNodes()[i];
    if (this->checkType(paramType, *exprNode).isUnresolved()) {
      continue;
    }

    validCount++;
  }
  if (validCount == paramSize) {
    node.setAttrKind(attr->getKind());
    node.setType(this->typePool().get(TYPE::Void));
  }
}

} // namespace ydsh