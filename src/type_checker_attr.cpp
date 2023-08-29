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

#include "arg_parser_base.h"
#include "type_checker.h"

namespace ydsh {

const char *toString(AttributeKind k) {
  const char *table[] = {
#define GEN_STR(E, S) S,
      EACH_ATTRIBUTE_KIND(GEN_STR)
#undef GEN_STR
  };
  return table[toUnderlying(k)];
}

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

static void defineAttribute(StrRefMap<std::unique_ptr<Attribute>> &values, AttributeKind kind,
                            Attribute::Loc loc, std::vector<Attribute::Param> &&params,
                            std::vector<const DSType *> &&types) {
  StrRefMap<Attribute::Param> paramMap;
  for (auto &p : params) {
    paramMap.emplace(toString(p), p);
  }
  auto attr = std::make_unique<Attribute>(kind, loc, std::move(paramMap), std::move(types));
  StringRef attrName = attr->getName();
  values.emplace(attrName, std::move(attr));
}

AttributeMap AttributeMap::create(const TypePool &pool) {
  StrRefMap<std::unique_ptr<Attribute>> values;

  defineAttribute(values, AttributeKind::CLI, Attribute::Loc::CONSTRUCTOR, {}, {});
  defineAttribute(values, AttributeKind::FLAG, Attribute::Loc::FIELD,
                  {
                      Attribute::Param::SHORT,
                      Attribute::Param::LONG,
                      Attribute::Param::REQUIRED,
                      Attribute::Param::STORE,
                      Attribute::Param::HELP,
                  },
                  {&pool.get(TYPE::Bool)});
  defineAttribute(values, AttributeKind::OPTION, Attribute::Loc::FIELD,
                  {
                      Attribute::Param::SHORT,
                      Attribute::Param::LONG,
                      Attribute::Param::REQUIRED,
                      Attribute::Param::OPT,
                      Attribute::Param::DEFAULT,
                      Attribute::Param::PLACE_HOLDER,
                      Attribute::Param::RANGE,
                      Attribute::Param::CHOICE,
                      Attribute::Param::HELP,
                  },
                  {&pool.get(TYPE::String), &pool.get(TYPE::Int)});
  defineAttribute(values, AttributeKind::ARG, Attribute::Loc::FIELD,
                  {
                      Attribute::Param::REQUIRED,
                      Attribute::Param::PLACE_HOLDER,
                      Attribute::Param::RANGE,
                      Attribute::Param::CHOICE,
                  },
                  {&pool.get(TYPE::StringArray), &pool.get(TYPE::String), &pool.get(TYPE::Int)});
  return AttributeMap(std::move(values));
}

static const DSType &createIntPairType(TypePool &pool) {
  auto ret = pool.createTupleType({&pool.get(TYPE::Int), &pool.get(TYPE::Int)});
  return *ret.asOk();
}

static const char *toString(Attribute::Loc loc) {
  switch (loc) {
  case Attribute::Loc::NONE:
    return "none";
  case Attribute::Loc::CONSTRUCTOR:
    return "constructor";
  case Attribute::Loc::FIELD:
    return "field";
  }
  return ""; // unreachable
}

void TypeChecker::visitAttributeNode(AttributeNode &node) {
  auto *attr = this->attributeMap.lookup(node.getAttrName());
  if (!attr) {
    auto &nameInfo = node.getAttrNameInfo();
    this->reportError<UndefinedAttr>(nameInfo.getToken(), nameInfo.getName().c_str());
    return;
  }
  if (attr->getLoc() != node.getLoc()) {
    this->reportError<InvalidAttrLoc>(node, attr->getName(), toString(attr->getLoc()));
    return;
  }
  if (attr->getLoc() == Attribute::Loc::FIELD) {
    if (!this->funcCtx->withinConstructor() ||
        !isa<CLIRecordType>(this->funcCtx->getReturnType())) {
      this->reportError<NeedCLIAttr>(node, node.getAttrName().c_str());
      return;
    }
  }

  // check attribute params
  AttributeParamSet paramSet;
  const unsigned int paramSize = node.getKeys().size();
  std::vector<std::unique_ptr<Node>> constNodes;
  for (unsigned int i = 0; i < paramSize; i++) {
    // check param existence
    auto &key = node.getKeys()[i];
    auto *p = attr->lookupParam(key.getName());
    if (!p) {
      this->reportError<UndefinedAttrParam>(key.getToken(), key.getName().c_str(), attr->getName());
      break;
    }
    if (paramSet.has(*p)) {
      this->reportError<DupAttrParam>(key.getToken(), key.getName().c_str());
      break;
    }
    paramSet.add(*p);

    // check param type
    auto &paramType = *p == Attribute::Param::RANGE ? createIntPairType(this->pool)
                                                    : getRequiredParamType(this->pool, *p);
    auto &exprNode = node.getValueNodes()[i];
    if (this->checkType(paramType, *exprNode).isUnresolved()) {
      continue;
    }

    auto constNode = this->evalConstant(*exprNode);
    if (!constNode) {
      continue;
    }
    constNodes.push_back(std::move(constNode));
  }
  if (constNodes.size() == paramSize) {
    node.setConstNodes(std::move(constNodes));
    node.setAttrKind(attr->getKind());
    node.setType(this->typePool().get(TYPE::Void));
  }
}

static std::string concatTypeNames(const std::vector<const DSType *> &types) {
  std::string value;
  unsigned int size = types.size();
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      value += ", ";
    }
    value += '`';
    value += types[i]->getNameRef();
    value += '\'';
  }
  return value;
}

void TypeChecker::checkFieldAttributes(const VarDeclNode &varDeclNode) {
  const bool privateField = StringRef(varDeclNode.getVarName()).startsWith("_");
  const bool readOnlyField = varDeclNode.getKind() == VarDeclNode::LET;

  auto &fieldType = varDeclNode.getExprNode()->getType();
  for (auto &e : varDeclNode.getAttrNodes()) {
    if (e->getAttrKind() == AttributeKind::NONE) {
      continue;
    }
    auto *attribute = this->attributeMap.lookup(e->getAttrName());
    assert(attribute);
    if (privateField) {
      this->reportError<FieldAttrPrivate>(*e, attribute->getName());
      break;
    }
    if (readOnlyField) {
      this->reportError<FieldAttrReadOnly>(*e, attribute->getName());
      break;
    }

    for (auto &t : attribute->getTypes()) {
      if (fieldType.isSameOrBaseTypeOf(*t)) {
        e->setValidType(true);
        break;
      }
    }
    if (!e->isValidType()) {
      auto value = concatTypeNames(attribute->getTypes());
      this->reportError<FieldAttrType>(*e, e->getAttrName().c_str(), value.c_str());
    }
  }
}

static bool isOptionOrBase(const DSType &type, TYPE target) {
  return type.is(target) ||
         (isa<OptionType>(type) && cast<OptionType>(type).getElementType().is(target));
}

static bool isValidLongOpt(const std::string &opt) {
  if (opt.size() < 2) {
    return false;
  }
  // [a-zA-Z] [a-zA-Z0-9-]+
  if (!std::isalpha(opt[0])) {
    return false;
  }
  for (size_t i = 1; i < opt.size(); i++) {
    char ch = opt[i];
    if (!std::isalnum(ch) && ch != '-') {
      return false;
    }
  }
  return true;
}

static bool isLower(char ch) { return ch >= 'a' && ch <= 'z'; }

static bool isLowerOrDigit(char ch) { return isLower(ch) || (ch >= '0' && ch <= '9'); }

static bool isUpper(char ch) { return ch >= 'A' && ch <= 'Z'; }

static std::string toLongOpt(const std::string &name) {
  assert(!name.empty());
  assert(name[0] != '_');
  std::string opt;

  /**
   * camelCase -> camel-case
   * CamelCase -> camel-case
   * snake__case -> snake-case
   */
  char prev = '\0';
  for (char ch : name) {
    if (ch == '_') {
      if (prev != '_') {
        opt += '-';
      }
    } else {
      if (isLowerOrDigit(prev) && isUpper(ch)) {
        opt += '-';
      }
      char l = ch;
      if (isUpper(ch)) {
        l = static_cast<char>('a' + (ch - 'A'));
      }
      opt += l;
    }
    prev = ch;
  }
  if (opt.size() == 1) {
    opt += opt;
  }
  return opt;
}

static std::string toArgName(const std::string &name) {
  assert(!name.empty());
  std::string value;

  /**
   * camelCase -> CAMEL_CASE
   * CamelCase -> CAMEL_CASE
   * snake__case -> SNAKE_CASE
   */
  char prev = '\0';
  for (char ch : name) {
    if (ch == '_') {
      if (prev != '_') {
        value += '_';
      }
    } else {
      if (isLowerOrDigit(prev) && isUpper(ch)) {
        value += '_';
      }
      char u = ch;
      if (isLower(ch)) {
        u = static_cast<char>('A' + (ch - 'a'));
      }
      value += u;
    }
    prev = ch;
  }
  return value;
}

void TypeChecker::resolveArgEntry(std::unordered_set<std::string> &foundOptionSet,
                                  unsigned int offset, const AttributeNode &attrNode,
                                  const VarDeclNode &declNode, std::vector<ArgEntry> &entries) {
  ArgEntry entry(static_cast<ArgEntryIndex>(entries.size()),
                 declNode.getHandle()->getIndex() - offset);
  ArgEntryAttr argEntryAttr{};
  switch (attrNode.getAttrKind()) {
  case AttributeKind::NONE:
  case AttributeKind::CLI:
    break; // unreachable
  case AttributeKind::FLAG:
    entry.setParseOp(OptParseOp::NO_ARG);
    break;
  case AttributeKind::OPTION:
    entry.setParseOp(OptParseOp::HAS_ARG);
    break;
  case AttributeKind::ARG:
    setFlag(argEntryAttr, ArgEntryAttr::POSITIONAL);
    break;
  }
  if (isOptionOrBase(declNode.getExprNode()->getType(), TYPE::Int)) {
    entry.setIntRange(INT64_MIN, INT64_MAX);
  }

  // fill ArgEntry
  auto *attr = this->attributeMap.lookup(attrNode.getAttrName());
  assert(attr);
  const unsigned int size = attrNode.getKeys().size();
  for (unsigned int i = 0; i < size; i++) {
    auto &paramInfo = attrNode.getKeys()[i];
    auto *param = attr->lookupParam(paramInfo.getName());
    assert(param);
    auto &constNode = *attrNode.getConstNodes()[i];
    switch (*param) {
    case Attribute::Param::HELP:
      entry.setDetail(cast<StringNode>(constNode).getValue().c_str());
      continue;
    case Attribute::Param::SHORT: {
      auto &optName = cast<StringNode>(constNode).getValue();
      if (optName.size() != 1 || !std::isalpha(optName[0])) {
        this->reportError<InvalidShortOpt>(constNode, optName.c_str());
        return;
      }
      if (foundOptionSet.find(optName) != foundOptionSet.end()) { // already found
        this->reportError<DefinedOpt>(constNode, optName.c_str());
        return;
      } else {
        foundOptionSet.emplace(optName);
        entry.setShortName(optName[0]);
      }
      continue;
    }
    case Attribute::Param::LONG: {
      auto &optName = cast<StringNode>(constNode).getValue();
      if (!isValidLongOpt(optName)) {
        this->reportError<InvalidLongOpt>(constNode, optName.c_str());
        return;
      }
      if (foundOptionSet.find(optName) != foundOptionSet.end()) { // already found
        this->reportError<DefinedOpt>(constNode, optName.c_str());
        return;
      } else {
        foundOptionSet.emplace(optName);
        entry.setLongName(optName.c_str());
      }
      continue;
    }
    case Attribute::Param::REQUIRED:
      if (cast<NumberNode>(constNode).getIntValue()) {
        setFlag(argEntryAttr, ArgEntryAttr::REQUIRE);
      } else {
        unsetFlag(argEntryAttr, ArgEntryAttr::REQUIRE);
      }
      continue;
    case Attribute::Param::STORE:
      if (cast<NumberNode>(constNode).getIntValue()) {
        unsetFlag(argEntryAttr, ArgEntryAttr::STORE_FALSE);
      } else {
        setFlag(argEntryAttr, ArgEntryAttr::STORE_FALSE);
      }
      continue;
    case Attribute::Param::OPT: {
      bool opt = cast<NumberNode>(constNode).getIntValue() > 0;
      entry.setParseOp(opt ? OptParseOp::OPT_ARG : OptParseOp::HAS_ARG);
      continue;
    }
    case Attribute::Param::DEFAULT:
      entry.setDefaultValue(cast<StringNode>(constNode).getValue().c_str());
      continue;
    case Attribute::Param::PLACE_HOLDER:
      entry.setArgName(cast<StringNode>(constNode).getValue().c_str());
      continue;
    case Attribute::Param::RANGE: {
      auto &type = this->typePool().get(TYPE::Int);
      if (declNode.getExprNode()->getType().isSameOrBaseTypeOf(type)) {
        auto &tupleNode = cast<TupleNode>(constNode);
        assert(tupleNode.getNodes().size() == 2);
        int64_t min = cast<NumberNode>(*tupleNode.getNodes()[0]).getIntValue();
        int64_t max = cast<NumberNode>(*tupleNode.getNodes()[1]).getIntValue();
        if (min > max) {
          std::swap(min, max);
        }
        entry.setIntRange(min, max);
      } else {
        this->reportError<FieldAttrParamType>(paramInfo.getToken(), paramInfo.getName().c_str(),
                                              type.getName());
        return;
      }
      continue;
    }
    case Attribute::Param::CHOICE: {
      auto &type = this->typePool().get(TYPE::String);
      if (declNode.getExprNode()->getType().isSameOrBaseTypeOf(type)) {
        auto &arrayNode = cast<ArrayNode>(constNode);
        std::unordered_set<StringRef, StrRefHash> choiceSet;
        for (auto &e : arrayNode.getExprNodes()) { // FIXME: choice size limit
          StringRef ref = cast<StringNode>(*e).getValue();
          if (ref.hasNullChar()) {
            this->reportError<NulChoiceElement>(*e);
            return;
          }
          if (!choiceSet.emplace(ref).second) { // already found
            this->reportError<DupChoiceElement>(*e, ref.data());
            return;
          }
          entry.addChoice(strdup(ref.data()));
        }
      } else {
        this->reportError<FieldAttrParamType>(paramInfo.getToken(), paramInfo.getName().c_str(),
                                              type.getName());
        return;
      }
      continue;
    }
    }
  }

  // add default option/arg name
  if (attrNode.getAttrKind() != AttributeKind::ARG && entry.getShortName() == '\0' &&
      entry.getLongName().empty()) {
    auto &varName = declNode.getVarName();
    auto optName = toLongOpt(varName);
    if (foundOptionSet.find(optName) != foundOptionSet.end()) {
      this->reportError<DefinedAutoOpt>(attrNode.getAttrNameInfo().getToken(), optName.c_str(),
                                        varName.c_str());
      return;
    } else {
      foundOptionSet.emplace(optName);
      entry.setLongName(optName.c_str());
    }
  } else if (attrNode.getAttrKind() == AttributeKind::ARG) {
    if (foundOptionSet.find("<remain>") != foundOptionSet.end()) { // already found remain arg
      Token token = attrNode.getAttrNameInfo().getToken();
      assert(!entries.empty());
      auto &last = entries.back();
      assert(last.isRemainArg());
      if (entry.getArgName().empty()) {
        auto &varName = declNode.getVarName();
        auto name = toArgName(varName);
        this->reportError<UnrecogAutoArg>(token, name.c_str(), varName.c_str(),
                                          last.getArgName().c_str());
      } else {
        this->reportError<UnrecogArg>(token, entry.getArgName().c_str(), last.getArgName().c_str());
      }
      return;
    } else {
      if (isOptionOrBase(declNode.getExprNode()->getType(), TYPE::StringArray)) {
        foundOptionSet.emplace("<remain>");
        setFlag(argEntryAttr, ArgEntryAttr::REMAIN);
      }
    }
  }
  entry.setAttr(argEntryAttr);
  if (entry.getArgName().empty()) {
    entry.setArgName(toArgName(declNode.getVarName()).c_str());
  }

  entries.push_back(std::move(entry));
}

template <typename Func>
static constexpr bool func_requirement_v =
    std::is_same_v<void, std::invoke_result_t<Func, const AttributeNode &, const VarDeclNode &>>;

template <typename Func, enable_when<func_requirement_v<Func>> = nullptr>
static void iterateFieldAttribute(const FunctionNode &node, Func func) {
  for (auto &e : node.getBlockNode().getNodes()) {
    if (!isa<VarDeclNode>(*e) || !cast<VarDeclNode>(*e).getHandle()) {
      continue;
    }
    auto &declNode = cast<VarDeclNode>(*e);
    for (auto &attrNode : declNode.getAttrNodes()) {
      if (attrNode->getAttrKind() == AttributeKind::NONE || !attrNode->isValidType()) {
        continue;
      }
      func(*attrNode, declNode);
    }
  }
}

std::vector<ArgEntry> TypeChecker::resolveArgEntries(const FunctionNode &node,
                                                     const unsigned int offset) {
  std::vector<ArgEntry> entries;

  // check Flag/Option
  std::unordered_set<std::string> foundOptionSet = {"h", "help"};
  iterateFieldAttribute(node, [&](const AttributeNode &attrNode, const VarDeclNode &declNode) {
    if (auto kind = attrNode.getAttrKind();
        kind == AttributeKind::FLAG || kind == AttributeKind::OPTION) {
      this->resolveArgEntry(foundOptionSet, offset, attrNode, declNode, entries);
    }
  });
  entries.push_back(ArgEntry::newHelp(static_cast<ArgEntryIndex>(0)));

  // check Arg
  iterateFieldAttribute(node, [&](const AttributeNode &attrNode, const VarDeclNode &declNode) {
    if (attrNode.getAttrKind() == AttributeKind::ARG) {
      this->resolveArgEntry(foundOptionSet, offset, attrNode, declNode, entries);
    }
  });
  return entries;
}

} // namespace ydsh