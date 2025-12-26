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
#include "format_util.h"
#include "type_checker.h"

namespace arsh {

static const Type &createIntPairType(TypePool &pool) {
  auto ret = pool.createTupleType({&pool.get(TYPE::Int), &pool.get(TYPE::Int)});
  return *ret.asOk();
}

static const Type &createCompFuncType(TypePool &pool) {
  auto ret = pool.createOptionType(pool.get(TYPE::Candidates));
  ret = pool.createFuncType(*ret.asOk(), {&pool.get(TYPE::String), &pool.get(TYPE::String)});
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
  }
  if (attr->getLoc() == Attribute::Loc::FIELD) {
    if (!this->funcCtx->withinConstructor() ||                 // NOLINT
        !isa<CLIRecordType>(this->funcCtx->getReturnType())) { // NOLINT
      this->reportError<NeedCLIAttr>(node, node.getAttrName().c_str());
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
      continue;
    }
    if (paramSet.has(*p)) {
      this->reportError<DupAttrParam>(key.getToken(), key.getName().c_str());
      continue;
    }
    paramSet.add(*p);

    // check param type
    auto &paramType = *p == Attribute::Param::RANGE  ? createIntPairType(this->pool)
                      : *p == Attribute::Param::COMP ? createCompFuncType(this->pool)
                                                     : getRequiredParamType(this->pool, *p);

    auto &exprNode = node.getValueNodes()[i];
    if (this->checkType(paramType, *exprNode).isUnresolved()) {
      continue;
    }

    auto constNode = this->evalConstant(*exprNode);
    if (!constNode) {
      continue;
    }

    if (*p == Attribute::Param::DESC) { // check desc (due to cli attribute param)
      assert(isa<StringNode>(*constNode));
      if (StringRef(cast<StringNode>(*constNode).getValue()).hasNullChar()) {
        this->reportError<NullCharAttrParam>(*constNode, key.getName().c_str());
        continue;
      }
    }
    constNodes.push_back(std::move(constNode));
  }
  node.setResolvedParamSet(paramSet);
  if (constNodes.size() == paramSize) {
    node.setConstNodes(std::move(constNodes));
    node.setAttrKind(attr->getKind());
    node.setType(this->typePool().get(TYPE::Void));
    if (node.getAttrKind() == AttributeKind::CLI) {
      node.setValidType(true);
    }
  }
}

void TypeChecker::checkAttributes(const std::vector<std::unique_ptr<AttributeNode>> &attrNodes,
                                  bool field) {
  unsigned int cliAttrCount = 0;
  for (unsigned int i = 0; i < attrNodes.size(); i++) {
    auto &attrNode = *attrNodes[i];
    if (i == SYS_LIMIT_ATTR_NUM) {
      this->reportError<AttrLimit>(attrNode);
      break;
    }
    if (field) {
      attrNode.setLoc(Attribute::Loc::FIELD);
    }
    this->checkTypeExactly(attrNode);
    if (attrNode.getAttrKind() == AttributeKind::CLI) {
      if (cliAttrCount > 0) {
        this->reportError<DupAttr>(attrNode, toString(AttributeKind::CLI));
      }
      cliAttrCount++;
    }
  }
}

static std::string concatTypeNames(const TypePool &pool, const std::vector<TYPE> &typeIds) {
  std::string value;
  const unsigned int size = typeIds.size();
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      value += ", ";
    }
    value += '`';
    value += pool.get(typeIds[i]).getNameRef();
    value += '\'';
  }
  return value;
}

static void concatTypeNames(std::string &) {}

template <typename... T>
static void concatTypeNames(std::string &out, const Type &type, T &&...remain) {
  out += ", `";
  out += type.getNameRef();
  out += '\'';
  concatTypeNames(out, std::forward<T>(remain)...);
}

template <typename... T>
static std::string concatTypeNames(const Type &type, T &&...remain) {
  std::string ret = "`";
  ret += type.getNameRef();
  ret += '\'';
  concatTypeNames(ret, std::forward<T>(remain)...);
  return ret;
}

std::tuple<bool, CLIRecordType::Attr, StringRef>
TypeChecker::postCheckConstructorAttribute(const FunctionNode &node) {
  assert(node.isConstructor());
  bool hasCliAttr = false;
  CLIRecordType::Attr attr{};
  StringRef desc;
  for (auto &e : node.getAttrNodes()) {
    if (e->getAttrKind() != AttributeKind::CLI) {
      continue;
    }
    hasCliAttr = true;
    if (const auto index = e->findValidAttrParamIndex(Attribute::Param::VERBOSE); index != -1) {
      if (cast<NumberNode>(*e->getConstNodes()[index]).getAsBoolValue()) {
        setFlag(attr, CLIRecordType::Attr::VERBOSE);
      } else {
        unsetFlag(attr, CLIRecordType::Attr::VERBOSE);
      }
    }
    if (const auto index = e->findValidAttrParamIndex(Attribute::Param::TOPLEVEL); index != -1) {
      if (cast<NumberNode>(*e->getConstNodes()[index]).getAsBoolValue()) {
        setFlag(attr, CLIRecordType::Attr::TOPLEVEL);
      } else {
        unsetFlag(attr, CLIRecordType::Attr::TOPLEVEL);
      }
    }
    if (const auto index = e->findValidAttrParamIndex(Attribute::Param::DESC); index != -1) {
      auto &constNode = *e->getConstNodes()[index];
      assert(isa<StringNode>(constNode));
      desc = cast<StringNode>(constNode).getValue();
    }
  }
  if (hasCliAttr && !node.getParamNodes().empty()) {
    this->reportError<CLIInitParam>(node.getNameInfo().getToken());
  }
  return {hasCliAttr, attr, desc};
}

void TypeChecker::postCheckFieldAttributes(const VarDeclNode &varDeclNode) {
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

    bool subcmd = false;
    for (auto &t : attribute->getTypeIds()) {
      if (auto &attrType = this->typePool().get(t); attrType.is(TYPE::CLI)) {
        subcmd = true;
        auto &type =
            fieldType.isOptionType() ? cast<OptionType>(fieldType).getElementType() : fieldType;
        if (type.isCLIRecordType()) {
          e->setValidType(true);
          break;
        }
      } else if (fieldType.isSameOrBaseTypeOf(attrType)) {
        e->setValidType(true);
        break;
      }
    }
    if (!e->isValidType()) {
      if (subcmd) {
        this->reportError<SubCmdAttrType>(*e);
      } else {
        auto value = concatTypeNames(this->typePool(), attribute->getTypeIds());
        this->reportError<FieldAttrType>(*e, e->getAttrName().c_str(), value.c_str());
      }
    }
  }
}

static bool isValidLongOpt(const std::string &opt) {
  if (opt.size() < 2) {
    return false;
  }
  // [a-zA-Z] [a-zA-Z0-9-]+
  if (!isLetter(opt[0])) {
    return false;
  }
  for (size_t i = 1; i < opt.size(); i++) {
    if (const char ch = opt[i]; !isLetterOrDigit(ch) && ch != '-') {
      return false;
    }
  }
  return true;
}

static std::string toLongOpt(const std::string &name) {
  assert(!name.empty());
  assert(name[0] != '_');
  std::string opt;
  splitCamelCaseIdentifier(name, [&opt](StringRef ref) {
    if (!opt.empty()) {
      opt += '-';
    }
    for (char ch : ref) {
      if (ch >= 'A' && ch <= 'Z') {
        ch = static_cast<char>(ch - 'A' + 'a');
      }
      opt += ch;
    }
  });
  return opt;
}

static std::string toArgName(const std::string &name) {
  assert(!name.empty());
  std::string value;
  splitCamelCaseIdentifier(name, [&value](StringRef ref) {
    if (!value.empty()) {
      value += '_';
    }
    for (char ch : ref) {
      if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
      }
      value += ch;
    }
  });
  return value;
}

void TypeChecker::resolveArgEntry(ResolveArgEntryParam &resolveParam, const unsigned int offset,
                                  const AttributeNode &attrNode, const VarDeclNode &declNode,
                                  std::vector<ArgEntry> &entries) {
  auto &fieldType = declNode.getExprNode()->getType();
  ArgEntry entry(fieldType.typeId(), static_cast<ArgEntryIndex>(entries.size()),
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
    resolveParam.argCount++;
    break;
  case AttributeKind::SUBCMD:
    setFlag(argEntryAttr, ArgEntryAttr::SUBCMD);
    break;
  }
  if (isSameOrOptionTypeOf(fieldType, this->typePool().get(TYPE::Int)) ||
      isSameOrOptionTypeOf(fieldType, this->typePool().get(TYPE::IntArray))) {
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
    case Attribute::Param::TOPLEVEL:
    case Attribute::Param::VERBOSE:
    case Attribute::Param::DESC:
      continue; // unreachable
    case Attribute::Param::HELP: {
      StringRef ref = cast<StringNode>(constNode).getValue();
      if (ref.hasNullChar()) {
        this->reportError<NullCharAttrParam>(constNode, paramInfo.getName().c_str());
        return;
      }
      entry.setDetail(ref.data());
      continue;
    }
    case Attribute::Param::SHORT: {
      auto &optName = cast<StringNode>(constNode).getValue();
      if (optName.size() != 1 || !isLetter(optName[0])) {
        this->reportError<InvalidShortOpt>(constNode, toPrintable(optName).c_str());
        return;
      }
      if (resolveParam.foundOptionSet.emplace(optName).second) {
        entry.setShortName(optName[0]);
      } else { // already found
        this->reportError<DefinedOpt>(constNode, optName.c_str());
        return;
      }
      continue;
    }
    case Attribute::Param::LONG: {
      auto &optName = cast<StringNode>(constNode).getValue();
      if (!isValidLongOpt(optName)) {
        this->reportError<InvalidLongOpt>(constNode, toPrintable(optName).c_str());
        return;
      }
      if (resolveParam.foundOptionSet.emplace(optName).second) {
        entry.setLongName(optName.c_str());
      } else { // already found
        this->reportError<DefinedOpt>(constNode, optName.c_str());
        return;
      }
      continue;
    }
    case Attribute::Param::REQUIRED:
      if (cast<NumberNode>(constNode).getAsBoolValue()) {
        setFlag(argEntryAttr, ArgEntryAttr::REQUIRED);
      } else {
        unsetFlag(argEntryAttr, ArgEntryAttr::REQUIRED);
      }
      continue;
    case Attribute::Param::STORE:
      if (cast<NumberNode>(constNode).getAsBoolValue()) {
        unsetFlag(argEntryAttr, ArgEntryAttr::STORE_FALSE);
      } else {
        setFlag(argEntryAttr, ArgEntryAttr::STORE_FALSE);
      }
      continue;
    case Attribute::Param::OPT: {
      bool opt = cast<NumberNode>(constNode).getAsBoolValue();
      entry.setParseOp(opt ? OptParseOp::OPT_ARG : OptParseOp::HAS_ARG);
      continue;
    }
    case Attribute::Param::STOP:
      if (cast<NumberNode>(constNode).getAsBoolValue()) {
        setFlag(argEntryAttr, ArgEntryAttr::STOP_OPTION);
      } else {
        unsetFlag(argEntryAttr, ArgEntryAttr::STOP_OPTION);
      }
      continue;
    case Attribute::Param::DEFAULT:
      entry.setDefaultValue(std::string(cast<StringNode>(constNode).getValue()));
      continue;
    case Attribute::Param::PLACE_HOLDER:
      entry.setArgName(cast<StringNode>(constNode).getValue().c_str());
      continue;
    case Attribute::Param::RANGE: {
      auto &intType = this->typePool().get(TYPE::Int);
      auto &intArrayType = this->typePool().get(TYPE::IntArray);
      if (fieldType.isSameOrBaseTypeOf(intType) || fieldType.isSameOrBaseTypeOf(intArrayType)) {
        auto &tupleNode = cast<TupleNode>(constNode);
        assert(tupleNode.getNodes().size() == 2);
        int64_t min = cast<NumberNode>(*tupleNode.getNodes()[0]).getIntValue();
        int64_t max = cast<NumberNode>(*tupleNode.getNodes()[1]).getIntValue();
        if (min > max) {
          std::swap(min, max);
        }
        entry.setIntRange(min, max);
      } else {
        std::string dummy = concatTypeNames(intType, intArrayType);
        this->reportError<FieldAttrParamType>(paramInfo.getToken(), paramInfo.getName().c_str(),
                                              dummy.c_str());
        return;
      }
      continue;
    }
    case Attribute::Param::CHOICE: {
      auto &strType = this->typePool().get(TYPE::String);
      auto &strArrayType = this->typePool().get(TYPE::StringArray);
      auto &strMapType = this->typePool().get(TYPE::StringStringMap);
      if (fieldType.isSameOrBaseTypeOf(strType) || fieldType.isSameOrBaseTypeOf(strArrayType) ||
          fieldType.isSameOrBaseTypeOf(strMapType)) {
        auto &arrayNode = cast<ArrayNode>(constNode);
        if (arrayNode.getExprNodes().size() > SYS_LIMIT_ATTR_CHOICE_SIZE) {
          this->reportError<ChoiceLimit>(constNode);
          return;
        }
        StrRefSet choiceSet;
        for (auto &e : arrayNode.getExprNodes()) {
          StringRef ref = cast<StringNode>(*e).getValue();
          if (ref.hasNullChar()) {
            this->reportError<NullCharAttrParam>(*e, paramInfo.getName().c_str());
            return;
          }
          if (!choiceSet.emplace(ref).second) { // already found
            this->reportError<DupChoiceElement>(*e, ref.data());
            return;
          }
          entry.addChoice(strdup(ref.data()));
        }
      } else {
        std::string dummy = concatTypeNames(strType, strArrayType);
        this->reportError<FieldAttrParamType>(paramInfo.getToken(), paramInfo.getName().c_str(),
                                              dummy.c_str());
        return;
      }
      continue;
    }
    case Attribute::Param::XOR: {
      int64_t v = cast<NumberNode>(constNode).getIntValue();
      if (v < 0 || static_cast<uint64_t>(v) > SYS_LIMIT_XOR_ARG_GROUP_NUM) {
        this->reportError<XORGroupRange>(constNode);
        return;
      }
      entry.setXORGroupId(static_cast<unsigned char>(v));
      continue;
    }
    case Attribute::Param::NAME: {
      auto &cmdName = cast<StringNode>(constNode).getValue();
      if (cmdName.empty() || cmdName[0] == '-' || StringRef(cmdName).hasNullChar()) {
        this->reportError<InvalidSubCmd>(constNode, toPrintable(cmdName).c_str());
        return;
      }
      if (resolveParam.foundSubCmdSet.emplace(cmdName).second) {
        entry.setArgName(cmdName.c_str());
      } else { // already found
        this->reportError<DefinedSubCmd>(constNode, cmdName.c_str());
        return;
      }
      continue;
    }
    case Attribute::Param::COMP: {
      auto &funcHandle = cast<NumberNode>(constNode).getAsFunc();
      assert(funcHandle.has(HandleAttr::GLOBAL));
      entry.setCompHandle(funcHandle);
      continue; // NOLINT
    }
    }
  }

  // add default option/arg/subcmd name
  switch (attrNode.getAttrKind()) {
  case AttributeKind::NONE:
  case AttributeKind::CLI:
    break;
  case AttributeKind::FLAG:
  case AttributeKind::OPTION:
    if (entry.getShortName() == '\0' && entry.getLongName().empty()) {
      auto &varName = declNode.getVarName();
      auto optName = toLongOpt(varName);
      if (resolveParam.foundOptionSet.emplace(optName).second) {
        entry.setLongName(optName.c_str());
      } else { // already found
        this->reportError<DefinedAutoOpt>(attrNode.getAttrNameInfo().getToken(), optName.c_str(),
                                          varName.c_str());
        return;
      }
    }
    break;
  case AttributeKind::ARG: {
    auto &foundOptionSet = resolveParam.foundOptionSet;
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
    }
    if (isSameOrOptionTypeOf(fieldType, this->typePool().get(TYPE::StringArray))) {
      foundOptionSet.emplace("<remain>");
      setFlag(argEntryAttr, ArgEntryAttr::REMAIN);
    }
    break;
  }
  case AttributeKind::SUBCMD:
    if (entry.getArgName().empty()) {
      auto &varName = declNode.getVarName();
      if (resolveParam.foundSubCmdSet.emplace(varName).second) {
        entry.setArgName(varName.c_str());
      } else { // already found
        this->reportError<DefinedAutoSubCmd>(attrNode.getAttrNameInfo().getToken(), varName.c_str(),
                                             varName.c_str());
        return;
      }
    }
    break;
  }
  entry.setAttr(argEntryAttr);
  if (entry.getArgName().empty()) {
    entry.setArgName(toArgName(declNode.getVarName()).c_str());
  }
  if (entry.inXORGroup() && entry.isRequired()) {
    resolveParam.requiredXORGroupSet.add(entry.getXORGroupId());
  }
  if (resolveParam.argCount > 0 && !resolveParam.foundSubCmdSet.empty()) {
    if (attrNode.getAttrKind() == AttributeKind::ARG ||
        attrNode.getAttrKind() == AttributeKind::SUBCMD) {
      this->reportError<CombineArgSubCmd>(attrNode.getAttrNameInfo().getToken());
    }
  }
  if (entry.getCheckerKind() == ArgEntry::CheckerKind::CHOICE && entry.getCompHandle()) {
    this->reportError<CombineChoiceComp>(attrNode.getAttrNameInfo().getToken());
  }

  entries.push_back(std::move(entry));
  resolveParam.tokens.push_back(attrNode.getAttrNameInfo().getToken());
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
  ResolveArgEntryParam param;
  param.foundOptionSet = {"h", "help"};

  // check Flag/Option
  iterateFieldAttribute(node, [&](const AttributeNode &attrNode, const VarDeclNode &declNode) {
    if (const auto kind = attrNode.getAttrKind();
        kind == AttributeKind::FLAG || kind == AttributeKind::OPTION) {
      this->resolveArgEntry(param, offset, attrNode, declNode, entries);
    }
  });
  entries.push_back(ArgEntry::newHelp(static_cast<ArgEntryIndex>(entries.size())));
  param.tokens.push_back({0, 0}); // dummy

  // check Arg/SubCmd
  iterateFieldAttribute(node, [&](const AttributeNode &attrNode, const VarDeclNode &declNode) {
    if (const auto kind = attrNode.getAttrKind();
        kind == AttributeKind::ARG || kind == AttributeKind::SUBCMD) {
      this->resolveArgEntry(param, offset, attrNode, declNode, entries);
    }
  });

  // check xor group
  const unsigned int size = entries.size();
  for (unsigned int i = 0; i < size; i++) {
    if (const auto &e = entries[i];
        e.inXORGroup() && !e.isRequired() && param.requiredXORGroupSet.has(e.getXORGroupId())) {
      this->reportError<RequiredXORGroup>(param.tokens[i], e.getXORGroupId());
    }
  }
  return entries;
}

} // namespace arsh