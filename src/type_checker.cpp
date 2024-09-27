/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include <cstdarg>
#include <vector>

#include "arg_parser_base.h"
#include "complete.h"
#include "constant.h"
#include "misc/num_util.hpp"
#include "type_checker.h"

namespace arsh {

// #########################
// ##     TypeChecker     ##
// #########################

const Type *TypeChecker::toType(TypeNode &node) {
  switch (node.typeKind) {
  case TypeNode::Base: {
    auto &typeNode = cast<BaseTypeNode>(node);

    // fist lookup type alias
    auto &typeName = typeNode.getTokenText();
    if (auto ret = this->curScope->lookup(toTypeAliasFullName(typeName))) {
      auto handle = std::move(ret).take();
      typeNode.setHandle(handle);
      return &this->typePool().get(handle->getTypeId());
    }
    auto *type = this->typePool().getType(typeName);
    if (!type) {
      if (this->typePool().getTypeTemplate(typeName)) { // generic base type
        this->reportError<NeedTypeParam>(typeNode, typeName.c_str());
        break;
      }
      std::string suffix;
      auto suggestion = suggestSimilarType(typeName, this->typePool(), *this->curScope, nullptr);
      if (!suggestion.empty()) {
        addSuggestionSuffix(suffix, suggestion);
      }
      this->reportError<UndefinedType>(typeNode, typeName.c_str(), suffix.c_str());
      break;
    }
    return type;
  }
  case TypeNode::Qualified: {
    auto &qualifiedNode = cast<QualifiedTypeNode>(node);
    auto &recvType = this->checkTypeExactly(qualifiedNode.getRecvTypeNode());
    std::string typeName = toTypeAliasFullName(qualifiedNode.getNameTypeNode().getTokenText());
    if (auto ret = this->curScope->lookupField(this->typePool(), recvType, typeName)) {
      qualifiedNode.getNameTypeNode().setHandle(ret.asOk());
      return &this->typePool().get(ret.asOk()->getTypeId());
    } else {
      auto &nameNode = qualifiedNode.getNameTypeNode();
      switch (ret.asErr()) {
      case NameLookupError::NOT_FOUND: {
        auto &name = nameNode.getTokenText();
        std::string suffix;
        auto suggestion = suggestSimilarType(name, this->typePool(), *this->curScope, &recvType);
        if (!suggestion.empty()) {
          addSuggestionSuffix(suffix, suggestion);
        }
        this->reportError<UndefinedField>(nameNode, name.c_str(), recvType.getName(),
                                          suffix.c_str());
        break;
      }
      case NameLookupError::MOD_PRIVATE:
        this->reportError<PrivateField>(nameNode, nameNode.getTokenText().c_str());
        break;
      case NameLookupError::UPVAR_LIMIT:
      case NameLookupError::UNCAPTURE_ENV:
      case NameLookupError::UNCAPTURE_FIELD:
        break; // unreachable
      }
      break;
    }
  }
  case TypeNode::Reified: {
    auto &typeNode = cast<ReifiedTypeNode>(node);
    unsigned int size = typeNode.getElementTypeNodes().size();
    auto &tempName = typeNode.getTemplate()->getTokenText();
    auto *typeTemplate = this->typePool().getTypeTemplate(tempName);
    if (!typeTemplate) {
      this->reportError<UndefinedGeneric>(typeNode.getTemplate()->getToken(), tempName.c_str());
      break;
    }
    std::vector<const Type *> elementTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      elementTypes[i] = &this->checkTypeExactly(*typeNode.getElementTypeNodes()[i]);
    }
    auto typeOrError = this->typePool().createReifiedType(*typeTemplate, std::move(elementTypes));
    if (!typeOrError) {
      Token token = node.getToken();
      if (int index = typeOrError.asErr()->getElementIndex(); index > -1) {
        token = typeNode.getElementTypeNodes()[index]->getToken();
      }
      this->reportError(token, std::move(*typeOrError.asErr()));
      break;
    }
    return typeOrError.asOk();
  }
  case TypeNode::Func: {
    auto &typeNode = cast<FuncTypeNode>(node);
    auto &returnType = this->checkTypeExactly(typeNode.getReturnTypeNode());
    unsigned int size = typeNode.getParamTypeNodes().size();
    std::vector<const Type *> paramTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      paramTypes[i] = &this->checkTypeExactly(*typeNode.getParamTypeNodes()[i]);
    }
    auto typeOrError = this->typePool().createFuncType(returnType, std::move(paramTypes));
    if (!typeOrError) {
      Token token = node.getToken();
      if (int index = typeOrError.asErr()->getElementIndex(); index > -1) {
        token = typeNode.getParamTypeNodes()[index]->getToken();
      }
      this->reportError(token, std::move(*typeOrError.asErr()));
      break;
    }
    return typeOrError.asOk();
  }
  case TypeNode::TypeOf:
    auto &typeNode = cast<TypeOfNode>(node);
    return &this->checkTypeAsSomeExpr(typeNode.getExprNode());
  }
  return nullptr;
}

static bool checkCoercion(const TypePool &pool, const Type &requiredType, const Type &targetType) {
  if (requiredType.isVoidType()) { // pop stack top
    return true;
  }

  if (requiredType.is(TYPE::Bool)) {
    if (targetType.isOptionType()) {
      return true;
    }
    return pool.hasMethod(targetType, OP_BOOL);
  }
  return false;
}

const Type &TypeChecker::checkType(const Type *requiredType, Node &targetNode,
                                   const Type *unacceptableType, CoercionKind &kind) {
  /**
   * if target node is expr node and type is null,
   * try type check.
   */
  if (targetNode.isUntyped()) {
    this->visitingDepth++;
    this->requiredTypes.push_back(requiredType);
    targetNode.setType(this->typePool().getUnresolvedType());
    targetNode.accept(*this);
    this->requiredTypes.pop_back();
    this->visitingDepth--;
  }

  /**
   * after type checking, Node is not untyped.
   */
  assert(!targetNode.isUntyped());
  auto &type = targetNode.getType();

  /**
   * do not try type matching.
   */
  if (requiredType == nullptr) {
    if (!type.isNothingType() && unacceptableType != nullptr &&
        unacceptableType->isSameOrBaseTypeOf(type)) {
      if (isa<TypeNode>(targetNode)) {
        this->reportError<UnacceptableType>(targetNode, type.getName());
      } else {
        this->reportError<Unacceptable>(targetNode, type.getName());
      }
      targetNode.setType(this->typePool().getUnresolvedType());
      return targetNode.getType();
    }
    return type;
  }

  /**
   * try type matching.
   */
  if (requiredType->isSameOrBaseTypeOf(type)) {
    return type;
  }

  /**
   * check coercion
   */
  if (kind == CoercionKind::INVALID_COERCION &&
      checkCoercion(this->typePool(), *requiredType, type)) {
    kind = CoercionKind::PERFORM_COERCION;
    return type;
  }

  this->reportError<Required>(targetNode, requiredType->getName(), type.getName());
  targetNode.setType(this->typePool().getUnresolvedType());
  return targetNode.getType();
}

const Type &TypeChecker::checkTypeAsSomeExpr(Node &targetNode) {
  auto &type = this->checkTypeAsExpr(targetNode);
  if (type.isNothingType()) {
    if (isa<TypeNode>(targetNode)) {
      this->reportError<UnacceptableType>(targetNode, type.getName());
    } else {
      this->reportError<Unacceptable>(targetNode, type.getName());
    }
    targetNode.setType(this->typePool().getUnresolvedType());
    return targetNode.getType();
  }
  return type;
}

static void adjustDeferDropSize(BlockNode &blockNode) {
  const unsigned int size = blockNode.getNodes().size();
  unsigned int index = 0;

  // find first defer
  DeferNode *lastDefer = nullptr;
  for (; index < size; index++) {
    auto *node = blockNode.getNodes()[index].get();
    if (isa<DeferNode>(node)) {
      lastDefer = cast<DeferNode>(node);
      index++;
      break;
    }
  }
  assert(lastDefer);
  blockNode.setFirstDeferOffset(lastDefer->getDropLocalOffset());

  for (; index < size; index++) {
    if (auto *node = blockNode.getNodes()[index].get(); isa<DeferNode>(node)) {
      auto *curDefer = cast<DeferNode>(node);
      const unsigned int dropSize =
          curDefer->getDropLocalOffset() - lastDefer->getDropLocalOffset();
      lastDefer->setDropLocalSize(dropSize);
      lastDefer = curDefer;
    }
  }
  const unsigned int localLimit = blockNode.getBaseIndex() + blockNode.getVarSize();
  const unsigned int dropSize = localLimit - lastDefer->getDropLocalOffset();
  lastDefer->setDropLocalSize(dropSize);
}

void TypeChecker::checkTypeWithCurrentScope(const Type *requiredType, BlockNode &blockNode) {
  auto *blockType = &this->typePool().get(TYPE::Void);
  FuncContext::IntoTry intoTry;
  for (auto iter = blockNode.refNodes().begin(); iter != blockNode.refNodes().end(); ++iter) {
    auto &targetNode = *iter;
    if (blockType->isNothingType()) {
      if (!isa<JumpNode>(*targetNode) ||
          cast<JumpNode>(*targetNode).getOpKind() != JumpNode::IMPLICIT_CONTINUE) {
        this->reportError<Unreachable>(*targetNode);
      }
    }

    // type check
    if (iter == blockNode.refNodes().end() - 1) {
      if (requiredType != nullptr) {
        this->checkTypeWithCoercion(*requiredType, targetNode);
      } else {
        this->checkTypeExactly(*targetNode);
      }
    } else {
      this->checkTypeWithCoercion(this->typePool().get(TYPE::Void), targetNode);
    }
    blockType = &targetNode->getType();

    // check empty block
    if (isa<BlockNode>(*targetNode) && cast<BlockNode>(*targetNode).getNodes().empty()) {
      this->reportError<UselessBlock>(*targetNode);
    }

    // check defer
    if (isa<DeferNode>(*targetNode) && !intoTry) {
      intoTry = this->funcCtx->intoTry();
    }
  }

  // set base index of current scope
  blockNode.setBaseIndex(this->curScope->getBaseIndex());
  blockNode.setVarSize(this->curScope->getLocalSize());
  blockNode.setMaxVarSize(this->curScope->getMaxLocalVarIndex() - blockNode.getBaseIndex());

  assert(blockType != nullptr);
  blockNode.setType(*blockType);

  // adjust defer drop size
  if (intoTry) {
    adjustDeferDropSize(blockNode);
  }
}

void TypeChecker::checkTypeWithCoercion(const Type &requiredType,
                                        std::unique_ptr<Node> &targetNode) {
  CoercionKind kind = CoercionKind::INVALID_COERCION;
  this->checkType(&requiredType, *targetNode, nullptr, kind);
  if (kind != CoercionKind::INVALID_COERCION && kind != CoercionKind::NOP) {
    targetNode = TypeOpNode::newTypedCastNode(std::move(targetNode), requiredType);
    this->resolveCastOp(cast<TypeOpNode>(*targetNode));
  }
}

const Type &TypeChecker::resolveCoercionOfJumpValue(const FlexBuffer<JumpNode *> &jumpNodes,
                                                    bool optional) {
  if (jumpNodes.empty()) {
    return this->typePool().get(TYPE::Void);
  }

  std::vector<const Type *> types(jumpNodes.size());
  for (unsigned int i = 0; i < jumpNodes.size(); i++) {
    types[i] = &jumpNodes[i]->getExprNode().getType();
  }
  auto &retType =
      this->resolveCommonSuperType(jumpNodes[0]->getExprNode(), std::move(types), nullptr);
  for (auto &jumpNode : jumpNodes) {
    this->checkTypeWithCoercion(retType, jumpNode->refExprNode());
  }

  if (optional) {
    if (auto ret = this->typePool().createOptionType(retType); ret) {
      return *std::move(ret).take();
    } else {
      return this->typePool().getUnresolvedType();
    }
  } else {
    return retType;
  }
}

HandlePtr TypeChecker::addEntry(Token token, const std::string &symbolName, const Type &type,
                                HandleKind kind, HandleAttr attribute) {
  bool shadowing = false;
  if (this->allowWarning && !this->curScope->isGlobal() &&
      !hasFlag(attribute, HandleAttr::UNCAPTURED) && symbolName != "_") {
    if (this->curScope->lookup(symbolName)) {
      shadowing = true;
    }
  }
  auto ret = this->curScope->defineHandle(std::string(symbolName), type, kind, attribute);
  if (!ret) {
    this->reportNameRegisterError(token, ErrorSymbolKind::VAR, ret.asErr(), symbolName);
    return nullptr;
  }
  if (shadowing) {
    this->reportError<VarShadowing>(token, symbolName.c_str());
  }
  return std::move(ret).take();
}

static auto initDeniedNameList() {
  std::unordered_set<std::string> set;
  for (auto &e : DENIED_REDEFINED_CMD_LIST) {
    set.insert(e);
  }
  return set;
}

HandlePtr TypeChecker::addUdcEntry(const UserDefinedCmdNode &node) {
  static auto deniedList = initDeniedNameList();
  if (deniedList.find(node.getCmdName()) != deniedList.end()) {
    this->reportError<ReservedCmd>(node, node.getCmdName().c_str());
    return nullptr;
  }
  if (auto &name = node.getCmdName(); StringRef(name).contains('/')) {
    this->reportError<InvalidUDCName>(node.getNameInfo().getToken());
    return nullptr;
  }

  const Type *returnType = nullptr;
  if (!node.getReturnTypeNode()) {
    returnType = &this->typePool().get(TYPE::Int);
  } else if (node.getReturnTypeNode()->getType().isNothingType()) {
    returnType = &this->typePool().get(TYPE::Nothing);
  }
  auto *type = &this->typePool().getUnresolvedType();
  if (returnType) {
    auto *paramType = &this->typePool().get(TYPE::StringArray);
    if (const auto *typeNode = node.getParamTypeNode();
        typeNode && !typeNode->isUntyped() && !typeNode->getType().isUnresolved()) {
      paramType = &typeNode->getType();
    }
    auto ret = this->typePool().createFuncType(*returnType, {paramType});
    assert(ret);
    type = ret.asOk();
  }

  if (auto ret = this->curScope->defineHandle(toCmdFullName(node.getCmdName()), *type,
                                              HandleKind::UDC, HandleAttr::READ_ONLY)) {
    return std::move(ret).take();
  } else {
    this->reportNameRegisterError(node.getToken(), ErrorSymbolKind::UDC, ret.asErr(),
                                  node.getCmdName());
  }
  return nullptr;
}

void TypeChecker::reportErrorImpl(TypeCheckError::Type errorType, Token token, const char *kind,
                                  const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    abort();
  }
  va_end(arg);

  this->errors.emplace_back(errorType, token, kind, CStrPtr(str));
}

void TypeChecker::reportNameRegisterError(Token token, ErrorSymbolKind kind,
                                          NameRegisterError error, const std::string &symbolName,
                                          const char *extraArg) {
  switch (error) {
  case NameRegisterError::DEFINED: {
    switch (kind) {
    case ErrorSymbolKind::VAR:
      this->reportError<DefinedSymbol>(token, symbolName.c_str());
      break;
    case ErrorSymbolKind::METHOD:
      assert(extraArg);
      this->reportError<DefinedMethod>(token, symbolName.c_str(), extraArg);
      break;
    case ErrorSymbolKind::TYPE_ALIAS:
      this->reportError<DefinedTypeAlias>(token, symbolName.c_str());
      break;
    case ErrorSymbolKind::UDC:
      this->reportError<DefinedCmd>(token, symbolName.c_str());
      break;
    }
    break;
  }
  case NameRegisterError::LOCAL_LIMIT:
    this->reportError<LocalLimit>(token);
    break;
  case NameRegisterError::GLOBAL_LIMIT:
    this->reportError<GlobalLimit>(token);
    break;
  case NameRegisterError::INVALID_TYPE:
    break; // normally unreachable
  }
}

void TypeChecker::reportMethodLookupError(ApplyNode::Attr attr, const AccessNode &node) {
  const char *methodName = node.getFieldName().c_str();
  switch (attr) {
  case ApplyNode::DEFAULT:
  case ApplyNode::INDEX: {
    if (attr == ApplyNode::INDEX) {
      methodName += strlen(INDEX_OP_NAME_PREFIX);
    }
    std::string suffix;
    if (attr == ApplyNode::DEFAULT) {
      const auto suggestion =
          suggestSimilarMember(methodName, this->typePool(), *this->curScope,
                               node.getRecvNode().getType(), SuggestMemberType::METHOD);
      if (!suggestion.empty()) {
        addSuggestionSuffix(suffix, suggestion);
      }
    }
    this->reportError<UndefinedMethod>(node.getNameToken(), methodName,
                                       node.getRecvNode().getType().getName(), suffix.c_str());
    break;
  }
  case ApplyNode::UNARY:
    methodName += strlen(UNARY_OP_NAME_PREFIX);
    this->reportError<UndefinedUnary>(node.getNameToken(), methodName,
                                      node.getRecvNode().getType().getName());
    break;
  case ApplyNode::BINARY:
    methodName += strlen(BINARY_OP_NAME_PREFIX);
    this->reportError<UndefinedBinary>(node.getNameToken(), methodName,
                                       node.getRecvNode().getType().getName());
    break;
  case ApplyNode::NEW_ITER:
    this->reportError<NotIterable>(node.getRecvNode(), node.getRecvNode().getType().getName());
    break;
  case ApplyNode::ITER_NEXT:
  case ApplyNode::MAP_ITER_NEXT_KEY:
  case ApplyNode::MAP_ITER_NEXT_VALUE:
    break; // normally unreachable
  }
}

// for ApplyNode type checking
/**
 * resolve function/method like the following step
 * 1. if node.isMethodCall()
 * 1-1. check type receiver, and lookup method. if found, return MethodHandle
 * 1-1-1. if receiver type is Optional and NEW_ITER op, lookup from element type of receiver
 * 1-2. if method is not found, lookup field.
 * 1-2-1. if field is found and is callable, return function type
 * 1-2-2. if field is not callable, report NotCallable error
 * 1-3. otherwise report UndefinedMethod error
 *
 * 2. if ! node.isMethodCall()
 * 2-1. check type expression and check if expr is callable, return function type
 * 2-2. otherwise, report NotCallable error
 */
CallSignature TypeChecker::resolveCallSignature(ApplyNode &node) {
  auto &exprNode = node.getExprNode();
  if (node.isMethodCall()) {
    assert(isa<AccessNode>(exprNode));
    auto &accessNode = cast<AccessNode>(exprNode);
    auto &methodName = accessNode.getFieldName();

    // first lookup method
    auto &recvType = this->checkTypeAsExpr(accessNode.getRecvNode());
    const auto handleOrError = this->curScope->lookupMethod(
        this->typePool(),
        node.getAttr() == ApplyNode::NEW_ITER && recvType.isOptionType()
            ? cast<OptionType>(recvType).getElementType()
            : recvType,
        methodName);
    if (handleOrError) {
      auto *handle = handleOrError.asOk();
      accessNode.setType(this->typePool().get(TYPE::Any));
      node.setKind(ApplyNode::METHOD_CALL);
      node.setHandle(handle);
      return handle->toCallSignature(methodName.c_str());
    }
    if (handleOrError.asErr() == NameLookupError::MOD_PRIVATE) {
      this->reportError<PrivateMethod>(accessNode.getNameToken(), methodName.c_str(),
                                       node.getRecvNode().getType().getName());
    } else if (!this->checkAccessNode(accessNode)) { // if method is not found, resolve field
      assert(handleOrError.asErr() == NameLookupError::NOT_FOUND);
      node.setType(this->typePool().getUnresolvedType());
      this->reportMethodLookupError(node.getAttr(), accessNode);
      return CallSignature(this->typePool().getUnresolvedType());
    }
  }

  // otherwise, resolve function type
  CallSignature callableTypes(this->typePool().getUnresolvedType());
  auto &type = this->checkTypeExactly(exprNode);
  if (type.isFuncType()) {
    node.setKind(ApplyNode::FUNC_CALL);
    const Handle *ptr = nullptr;
    const char *name = nullptr;
    if (isa<VarNode>(exprNode)) {
      const auto &varNode = cast<VarNode>(exprNode);
      ptr = varNode.getHandle().get();
      name = varNode.getVarName().c_str();
    } else if (isa<AccessNode>(exprNode)) {
      const auto &accessNode = cast<AccessNode>(exprNode);
      ptr = accessNode.getHandle().get();
      name = accessNode.getFieldName().c_str();
    }
    callableTypes = cast<FunctionType>(type).toCallSignature(name, ptr);
  } else {
    this->reportError<NotCallable>(exprNode, type.getName());
  }
  return callableTypes;
}

bool TypeChecker::checkAccessNode(AccessNode &node) {
  auto &recvType = this->checkTypeAsExpr(node.getRecvNode());
  if (auto ret = this->curScope->lookupField(this->typePool(), recvType, node.getFieldName())) {
    const auto &handle = ret.asOk();
    node.setHandle(handle);
    node.setType(this->typePool().get(handle->getTypeId()));
    return true;
  } else {
    switch (ret.asErr()) {
    case NameLookupError::NOT_FOUND:
    case NameLookupError::UPVAR_LIMIT:
    case NameLookupError::UNCAPTURE_ENV:
    case NameLookupError::UNCAPTURE_FIELD:
      break;
    case NameLookupError::MOD_PRIVATE:
      this->reportError<PrivateField>(node.getNameToken(), node.getFieldName().c_str());
      break;
    }
    return false;
  }
}

static unsigned int getMinParamSize(const CallSignature &types) {
  unsigned int paramSize = types.paramSize;
  for (; paramSize > 0 && types.paramTypes[paramSize - 1]->isOptionType(); --paramSize)
    ;
  return paramSize;
}

static bool hasCompInLastArg(const ArgsNode &node, CodeCompNode::Kind kind) {
  if (!node.getNodes().empty()) {
    if (isa<CodeCompNode>(*node.getNodes().back())) {
      return cast<CodeCompNode>(*node.getNodes().back()).getKind() == kind;
    }
  }
  return false;
}

void TypeChecker::checkArgsNode(const CallSignature &callSignature, ArgsNode &node) {
  const unsigned int argSize = node.getNodes().size();
  if (node.hasNamedArgs()) {
    this->checkNamedArgs(callSignature, node);
  } else {
    const unsigned int paramSize = callSignature.paramSize;
    const unsigned int minParamSize = getMinParamSize(callSignature);

    if (!callSignature.returnType->isUnresolved()) {
      if (argSize < minParamSize || argSize > paramSize) {
        this->reportError<UnmatchParam>(node, paramSize, argSize);
      }
    }
    const unsigned int maxSize = std::max(argSize, paramSize);
    for (unsigned int i = 0; i < maxSize; i++) {
      if (i < argSize && i < paramSize) {
        this->checkType(*callSignature.paramTypes[i], *node.getNodes()[i]);
      } else if (i < argSize) {
        this->checkTypeAsExpr(*node.getNodes()[i]);
      }
    }

    // add optional args
    if (argSize >= minParamSize && argSize < paramSize) {
      node.setNoneCount(paramSize - argSize);
    }
    this->checkTypeExactly(node);
  }

  if (this->signatureHandler && hasCompInLastArg(node, CodeCompNode::Kind::CALL_SIGNATURE)) {
    this->signatureHandler(callSignature, argSize - 1);
  }
}

static std::vector<std::string>
getUnfoundParamNames(const FlexBuffer<StringRef> &paramNames,
                     const StrRefMap<std::pair<unsigned int, bool>> &paramMap, bool suffixColon) {
  std::vector<std::string> values;
  for (auto &e : paramNames) {
    auto iter = paramMap.find(e);
    assert(iter != paramMap.end());
    if (!iter->second.second) {
      auto v = e.toString();
      if (suffixColon) {
        v += ':';
      }
      values.push_back(std::move(v));
    }
  }
  return values;
}

void TypeChecker::checkNamedArgs(const CallSignature &callSignature, ArgsNode &node) {
  if (!callSignature.returnType->isUnresolved()) {
    const unsigned int argSize = node.getNodes().size();
    const unsigned int paramSize = callSignature.paramSize;
    if (argSize > paramSize) {
      this->reportError<UnmatchParam>(node, paramSize, argSize);
    }
  }

  auto *hd = callSignature.handle;
  StringRef packedParamNames;
  if (hd && hd->isFuncHandle()) {
    packedParamNames = cast<FuncHandle>(hd)->getPackedParamNames();
  } else if (hd && hd->isMethodHandle()) {
    packedParamNames = cast<MethodHandle>(hd)->getPackedParamNames();
  } else {
    this->reportError<NotNamedCallable>(node);
  }
  FlexBuffer<StringRef> paramNames;
  if (!packedParamNames.empty()) {
    splitByDelim(packedParamNames, ';', [&paramNames](const StringRef sub, bool) {
      paramNames.push_back(sub);
      return true;
    });
  }
  StrRefMap<std::pair<unsigned int, bool>> paramMap;
  for (unsigned int i = 0; i < paramNames.size(); i++) {
    paramMap[paramNames[i]] = {i, false};
  }

  const unsigned int argSize = node.getNodes().size();
  unsigned int index = 0;

  // check type unnamed arguments
  for (; index < argSize; index++) {
    if (node.findNamedEntry(index) != node.getNamedEntries().size()) {
      break;
    }
    if (index < paramNames.size()) {
      auto iter = paramMap.find(paramNames[index]);
      assert(iter != paramMap.end());
      iter->second.second = true;
      auto &paramType = *callSignature.paramTypes[iter->second.first];
      this->checkType(paramType, *node.getNodes()[index]);
    } else { // failed case
      this->checkTypeAsExpr(*node.getNodes()[index]);
    }
  }

  // check type named arguments
  for (; index < argSize; index++) {
    const auto namedEntryIndex = node.findNamedEntry(index);
    int paramIndex = -1;
    if (namedEntryIndex == node.getNamedEntries().size()) { //  not found named arg
      this->reportError<InvalidUnnamedArg>(*node.getNodes()[index]);
    } else {
      auto &nameInfo = node.getNamedEntries()[namedEntryIndex].getNameInfo();
      if (auto iter = paramMap.find(nameInfo.getName()); iter == paramMap.end()) {
        auto unfoundNames = getUnfoundParamNames(paramNames, paramMap, false);
        const StringRef suggestion = suggestSimilarParamName(nameInfo.getName(), unfoundNames, 5);
        std::string suffix;
        if (!suggestion.empty()) {
          addSuggestionSuffix(suffix, suggestion);
        }
        this->reportError<UndefinedNamedArg>(nameInfo.getToken(), nameInfo.getName().c_str(),
                                             suffix.c_str());
      } else if (iter->second.second) {
        this->reportError<RepeatedNamedArg>(nameInfo.getToken(), nameInfo.getName().c_str());
      } else {
        iter->second.second = true;
        paramIndex = static_cast<int>(iter->second.first);
      }
    }

    if (paramIndex > -1) {
      auto &paramType = *callSignature.paramTypes[paramIndex];
      this->checkType(paramType, *node.getNodes()[index]);
      node.refNamedEntries()[namedEntryIndex].setParamIndex(static_cast<unsigned char>(paramIndex));
    } else { // failed case
      this->checkTypeAsExpr(*node.getNodes()[index]);
    }
  }

  // check missing named arguments
  for (unsigned int i = 0; i < paramNames.size(); i++) {
    const auto iter = paramMap.find(paramNames[i]);
    assert(iter != paramMap.end());
    if (!iter->second.second && !callSignature.paramTypes[i]->isOptionType()) {
      this->reportError<MissingNamedArg>(node, iter->first.toString().c_str());
    }
  }
  if (const unsigned int namedArgStartOffset = node.getNamedEntries()[0].getOffset();
      namedArgStartOffset < callSignature.paramSize) {
    node.setNoneCount(callSignature.paramSize - namedArgStartOffset);
  }
  this->checkTypeExactly(node);

  if (!paramNames.empty() && hasCompInLastArg(node, CodeCompNode::Kind::VAR_OR_PARAM)) {
    auto unfoundNames = getUnfoundParamNames(paramNames, paramMap, true);
    this->compCtx->appendParamRequest(std::move(unfoundNames));
  }
}

void TypeChecker::resolveCastOp(TypeOpNode &node, bool forceToString) {
  auto &exprType = node.getExprNode().getType();
  auto &targetType = node.getType();

  if (exprType.isNothingType()) {
    this->reportError<NothingCast>(node);
    return;
  }

  if (node.getType().isVoidType()) {
    node.setOpKind(TypeOpNode::TO_VOID);
    return;
  }

  /**
   * nop
   */
  if (targetType.isSameOrBaseTypeOf(exprType)) {
    if (targetType == exprType && this->allowWarning) {
      this->reportError<MeaninglessCast>(node);
    }
    return;
  }

  /**
   * number cast
   */
  const int beforeIndex = exprType.getNumTypeIndex();
  const int afterIndex = targetType.getNumTypeIndex();
  if (beforeIndex > -1 && afterIndex > -1) {
    assert(beforeIndex < 8 && afterIndex < 8);
    node.setOpKind(TypeOpNode::NUM_CAST);
    return;
  }

  if (exprType.isOptionType()) {
    if (targetType.is(TYPE::Bool)) {
      node.setOpKind(TypeOpNode::CHECK_UNWRAP);
      return;
    }
  } else {
    if (targetType.is(TYPE::Bool) && this->typePool().hasMethod(exprType, OP_BOOL)) {
      node.setOpKind(TypeOpNode::TO_BOOL);
      return;
    }
    if (forceToString && targetType.is(TYPE::String)) {
      node.setOpKind(TypeOpNode::TO_STRING);
      return;
    }
    if (!targetType.isNothingType() && exprType.isSameOrBaseTypeOf(targetType)) {
      node.setOpKind(TypeOpNode::CHECK_CAST);
      return;
    }
    if (targetType.is(TYPE::String)) {
      node.setOpKind(TypeOpNode::TO_STRING);
      return;
    }
  }
  this->reportError<CastOp>(node, exprType.getName(), targetType.getName());
}

// visitor api
void TypeChecker::visitTypeNode(TypeNode &node) {
  if (const auto ret = this->toType(node)) {
    node.setType(*ret);
  }
}

void TypeChecker::visitNumberNode(NumberNode &node) {
  switch (node.kind) {
  case NumberNode::Int:
    if (!node.isInit()) {
      auto [value, status] = this->lexer.get().toInt64(node.getActualToken());
      if (status) {
        node.setIntValue(value);
      } else {
        this->reportError<OutOfRangeInt>(node.getActualToken());
      }
    }
    node.setType(this->typePool().get(TYPE::Int));
    break;
  case NumberNode::Float:
    if (!node.isInit()) {
      auto [value, status] = this->lexer.get().toDouble(node.getActualToken());
      if (status) {
        node.setFloatValue(value);
      } else {
        this->reportError<OutOfRangeFloat>(node.getActualToken());
      }
    }
    node.setType(this->typePool().get(TYPE::Float));
    break;
  case NumberNode::Signal:
    node.setType(this->typePool().get(TYPE::Signal)); // for constant expression
    break;
  case NumberNode::Bool:
    node.setType(this->typePool().get(TYPE::Bool)); // for constant expression
    break;
  case NumberNode::None:
    node.setType(this->typePool().get(TYPE::OptNothing)); // for constant expression
    break;
  }
}

void TypeChecker::visitStringNode(StringNode &node) {
  switch (node.getKind()) {
  case StringNode::STRING:
    if (!node.isInit()) {
      std::string value;
      if (this->lexer.get().singleToString(node.getActualToken(), value)) {
        node.setValue(std::move(value));
      } else {
        this->reportError<IllegalStrEscape>(node.getActualToken(), value.c_str());
      }
    }
    break;
  case StringNode::TILDE:
  case StringNode::CMD_ARG:
    break;
  case StringNode::BACKQUOTE:
    this->reportError<NoBackquote>(node);
    return; // not set String type
  }
  node.setType(this->typePool().get(TYPE::String));
}

void TypeChecker::visitStringExprNode(StringExprNode &node) {
  for (auto &exprNode : node.getExprNodes()) {
    this->checkTypeAsExpr(*exprNode);
  }
  node.setType(this->typePool().get(TYPE::String));
}

void TypeChecker::visitRegexNode(RegexNode &node) {
  std::string e;
  if (!node.buildRegex(e)) {
    this->reportError<RegexSyntax>(node.getActualToken(), e.c_str());
  }
  node.setType(this->typePool().get(TYPE::Regex));
}

void TypeChecker::visitArrayNode(ArrayNode &node) {
  const unsigned int size = node.getExprNodes().size();
  assert(size != 0);
  auto &firstElementNode = node.getExprNodes()[0];
  auto &elementType = this->checkTypeAsSomeExpr(*firstElementNode);

  for (unsigned int i = 1; i < size; i++) {
    this->checkType(elementType, *node.getExprNodes()[i]);
  }

  if (auto typeOrError = this->typePool().createArrayType(elementType)) {
    node.setType(*std::move(typeOrError).take());
  }
}

void TypeChecker::visitMapNode(MapNode &node) {
  const unsigned int size = node.getValueNodes().size();
  assert(size != 0);
  auto &firstKeyNode = node.getKeyNodes()[0];
  this->checkTypeAsSomeExpr(*firstKeyNode);
  auto &keyType = this->checkType(this->typePool().get(TYPE::Value_), *firstKeyNode);
  auto &firstValueNode = node.getValueNodes()[0];
  auto &valueType = this->checkTypeAsSomeExpr(*firstValueNode);

  for (unsigned int i = 1; i < size; i++) {
    this->checkType(keyType, *node.getKeyNodes()[i]);
    this->checkType(valueType, *node.getValueNodes()[i]);
  }

  if (auto typeOrError = this->typePool().createMapType(keyType, valueType)) {
    node.setType(*std::move(typeOrError).take());
  }
}

void TypeChecker::visitTupleNode(TupleNode &node) {
  const unsigned int size = node.getNodes().size();
  std::vector<const Type *> types(size);
  for (unsigned int i = 0; i < size; i++) {
    types[i] = &this->checkTypeAsSomeExpr(*node.getNodes()[i]);
  }
  if (auto typeOrError = this->typePool().createTupleType(std::move(types))) {
    node.setType(*std::move(typeOrError).take());
  } else {
    this->reportError(node.getToken(), std::move(*typeOrError.asErr()));
  }
}

void TypeChecker::visitVarNode(VarNode &node) {
  switch (node.getExtraOp()) {
  case VarNode::NONE: {
    if (auto ret = this->curScope->lookupAndCaptureUpVar(node.getVarName())) {
      const auto handle = std::move(ret).take();
      node.setHandle(handle);
      node.setType(this->typePool().get(handle->getTypeId()));
    } else {
      switch (ret.asErr()) {
      case NameLookupError::NOT_FOUND: {
        const auto suggestion = suggestSimilarVarName(node.getVarName(), *this->curScope);
        std::string suffix;
        if (!suggestion.empty()) {
          addSuggestionSuffix(suffix, suggestion);
        }
        this->reportError<UndefinedSymbol>(node, node.getVarName().c_str(), suffix.c_str());
        break;
      }
      case NameLookupError::MOD_PRIVATE:
        break; // unreachable
      case NameLookupError::UPVAR_LIMIT:
        this->reportError<UpvarLimit>(node);
        break;
      case NameLookupError::UNCAPTURE_ENV:
        this->reportError<UncaptureEnv>(node, node.getVarName().c_str());
        break;
      case NameLookupError::UNCAPTURE_FIELD:
        this->reportError<UncaptureField>(node, node.getVarName().c_str());
        break;
      }
    }
    break;
  }
  case VarNode::ARGS_LEN: { // $#
    auto ret = this->curScope->lookupAndCaptureUpVar("@");
    assert(ret);
    node.setHandle(ret.asOk());
    node.setType(this->typePool().get(TYPE::Int));
    break;
  }
  case VarNode::POSITIONAL_ARG: { // $0, $1 ...
    const StringRef ref = node.getVarName();
    if (const auto pair = convertToNum10<uint32_t>(ref.begin(), ref.end());
        pair && pair.value <= SYS_LIMIT_ARRAY_MAX) {
      if (pair.value == 0) { // $0
        auto ret = this->curScope->lookupAndCaptureUpVar("0");
        assert(ret);
        const auto &handle = ret.asOk();
        node.setHandle(handle);
        node.setType(this->typePool().get(handle->getTypeId()));
      } else {
        auto ret = this->curScope->lookupAndCaptureUpVar("@");
        assert(ret);
        node.setHandle(ret.asOk());
        node.setExtraValue(pair.value);
        node.setType(this->typePool().get(TYPE::String));
      }
    } else {
      this->reportError<PosArgRange>(node.getActualToken(), node.getVarName().c_str());
    }
    break;
  }
  case VarNode::CUR_ARG0:
    node.setType(this->typePool().get(TYPE::String));
    break;
  }
}

void TypeChecker::visitAccessNode(AccessNode &node) {
  if (!this->checkAccessNode(node)) {
    std::string suffix;
    const auto suggestion =
        suggestSimilarMember(node.getFieldName(), this->typePool(), *this->curScope,
                             node.getRecvNode().getType(), SuggestMemberType::FIELD);
    if (!suggestion.empty()) {
      addSuggestionSuffix(suffix, suggestion);
    }
    this->reportError<UndefinedField>(node.getNameToken(), node.getFieldName().c_str(),
                                      node.getRecvNode().getType().getName(), suffix.c_str());
  }
}

void TypeChecker::visitTypeOpNode(TypeOpNode &node) {
  auto &exprType = this->checkTypeAsExpr(node.getExprNode());
  auto &targetType = this->checkTypeExactly(*node.getTargetTypeNode());

  if (node.getOpKind() == TypeOpNode::CHECK_CAST_OPT) {
    node.setOpKind(TypeOpNode::NO_CAST);
    if (!targetType.isNothingType() && exprType.isSameOrBaseTypeOf(targetType)) {
      if (auto ret = this->typePool().createOptionType(targetType); ret) {
        node.setOpKind(TypeOpNode::CHECK_CAST_OPT);
        node.setType(*ret.asOk());
      }
    } else {
      this->reportError<InvalidOptCast>(node, exprType.getName());
    }
  } else if (node.isCastOp()) {
    node.setType(targetType);
    this->resolveCastOp(node);
  } else {
    if (targetType.isSameOrBaseTypeOf(exprType)) {
      node.setOpKind(TypeOpNode::ALWAYS_TRUE);
    } else if (exprType.isSameOrBaseTypeOf(targetType)) {
      node.setOpKind(TypeOpNode::INSTANCEOF);
    } else {
      node.setOpKind(TypeOpNode::ALWAYS_FALSE);
    }
    node.setType(this->typePool().get(TYPE::Bool));
  }
}

void TypeChecker::visitUnaryOpNode(UnaryOpNode &node) {
  auto &exprType = this->checkTypeAsExpr(*node.getExprNode());
  if (exprType.isUnresolved()) {
    return;
  }
  if (node.isUnwrapOp()) {
    if (exprType.isOptionType()) {
      node.setType(cast<OptionType>(exprType).getElementType());
    } else {
      this->reportError<UnwrapType>(*node.getExprNode(), exprType.getName());
    }
  } else {
    if (exprType.isOptionType()) {
      this->checkTypeWithCoercion(this->typePool().get(TYPE::Bool), node.refExprNode());
    }
    auto &applyNode = node.createApplyNode();
    node.setType(this->checkTypeAsExpr(applyNode));
  }
}

void TypeChecker::visitBinaryOpNode(BinaryOpNode &node) {
  if (node.getOp() == TokenKind::COND_AND || node.getOp() == TokenKind::COND_OR) {
    auto &booleanType = this->typePool().get(TYPE::Bool);
    this->checkTypeWithCoercion(booleanType, node.refLeftNode());
    if (node.getLeftNode()->getType().isNothingType()) {
      this->reportError<Unreachable>(*node.getRightNode());
    }
    this->checkTypeWithCoercion(booleanType, node.refRightNode());
    node.setType(booleanType);
    return;
  }

  if (node.getOp() == TokenKind::NULL_COALE) {
    auto &leftType = this->checkTypeAsExpr(*node.getLeftNode());
    if (leftType.isOptionType()) {
      auto &elementType = cast<OptionType>(leftType).getElementType();
      this->checkType(elementType, *node.getRightNode());
      node.setType(elementType);
    } else {
      this->checkTypeAsExpr(*node.getRightNode());
      this->reportError<Required>(*node.getLeftNode(), TYPE_OPTION, leftType.getName());
    }
    return;
  }

  auto &leftType = this->checkTypeAsExpr(*node.getLeftNode());
  auto &rightType = this->checkTypeAsExpr(*node.getRightNode());

  if (leftType.isUnresolved() || rightType.isUnresolved()) {
    return;
  }

  // check referential equality of func object
  if ((leftType.isFuncType() || leftType.is(TYPE::Command)) &&
      (node.getOp() == TokenKind::EQ || node.getOp() == TokenKind::NE)) {
    this->checkType(leftType, *node.getRightNode());
    node.setType(this->typePool().get(TYPE::Bool));
    return;
  }

  // string concatenation
  if (node.getOp() == TokenKind::ADD && (leftType.is(TYPE::String) || rightType.is(TYPE::String))) {
    if (!leftType.is(TYPE::String)) {
      this->resolveToStringCoercion(node.refLeftNode());
    }
    if (!rightType.is(TYPE::String)) {
      this->resolveToStringCoercion(node.refRightNode());
    }
    node.setType(this->typePool().get(TYPE::String));
    return;
  }

  node.createApplyNode();
  node.setType(this->checkTypeAsExpr(*node.getOptNode()));
}

void TypeChecker::visitArgsNode(ArgsNode &node) { node.setType(this->typePool().get(TYPE::Void)); }

void TypeChecker::visitApplyNode(ApplyNode &node) {
  switch (node.getAttr()) {
  case ApplyNode::MAP_ITER_NEXT_KEY:
  case ApplyNode::MAP_ITER_NEXT_VALUE: {
    assert(isa<AccessNode>(node.getExprNode()));
    auto &accessNode = cast<AccessNode>(node.getExprNode());
    accessNode.setType(this->typePool().get(TYPE::Any));
    auto &recvType = this->checkTypeAsExpr(accessNode.getRecvNode());
    if (recvType.isMapType()) {
      auto &mapType = cast<MapType>(recvType);
      node.setType(node.getAttr() == ApplyNode::MAP_ITER_NEXT_KEY ? mapType.getKeyType()
                                                                  : mapType.getValueType());
    } else {
      this->reportError<Required>(accessNode.getRecvNode(), TYPE_MAP, recvType.getName());
    }
    break;
  }
  default:
    const CallSignature callSignature = this->resolveCallSignature(node);
    this->checkArgsNode(callSignature, node.getArgsNode());
    node.setType(*callSignature.returnType);
    if (node.getType().isNothingType() &&
        this->funcCtx->finallyLevel() > this->funcCtx->childLevel()) {
      this->reportError<InsideFinally>(node);
    }
    break;
  }
}

void TypeChecker::visitNewNode(NewNode &node) {
  auto &type = this->checkTypeAsExpr(*node.getTargetTypeNode());
  CallSignature callSignature(this->typePool().getUnresolvedType());
  if (type.isOptionType() || type.isArrayType() || type.isMapType()) {
    callSignature = CallSignature(type, OP_INIT);
  } else {
    if (auto *handle = this->curScope->lookupConstructor(this->typePool(), type)) {
      callSignature = handle->toCallSignature(OP_INIT);
      node.setHandle(handle);
    } else {
      this->reportError<UndefinedInit>(*node.getTargetTypeNode(), type.getName());
    }
  }
  this->checkArgsNode(callSignature, node.getArgsNode());
  node.setType(type);
}

void TypeChecker::visitEmbedNode(EmbedNode &node) {
  auto &exprType = this->checkTypeAsExpr(node.getExprNode());
  if (exprType.isOptionType()) {
    this->reportError<OptParamExpand>(node.getExprNode(), exprType.getName());
    node.setType(this->typePool().getUnresolvedType());
    return;
  }

  node.setType(exprType);
  if (node.getKind() == EmbedNode::STR_EXPR) {
    auto &type = this->typePool().get(TYPE::String);
    if (!type.isSameOrBaseTypeOf(exprType)) { // call __INTERP__()
      const std::string methodName(OP_INTERP);
      if (auto *handle = this->typePool().lookupMethod(exprType, methodName)) {
        assert(handle->getReturnType() == type);
        node.setHandle(handle);
      } else { // if exprType is Unresolved
        this->reportError<UndefinedMethod>(node.getExprNode(), methodName.c_str(),
                                           exprType.getName(), "");
        node.setType(this->typePool().getUnresolvedType());
      }
    }
  } else {
    if (!this->typePool().get(TYPE::String).isSameOrBaseTypeOf(exprType) &&
        !this->typePool().get(TYPE::StringArray).isSameOrBaseTypeOf(exprType) &&
        !this->typePool().get(TYPE::FD).isSameOrBaseTypeOf(
            exprType)) { // call __STR__ or __CMD__ARG
      if (exprType.isCollectionLike() || exprType.is(TYPE::Any)) {
        node.setType(this->typePool().get(TYPE::StringArray));
      } else if (auto *handle = this->typePool().lookupMethod(exprType, OP_STR)) {
        node.setHandle(handle);
        node.setType(handle->getReturnType());
      } else {
        this->reportError<UndefinedMethod>(node.getExprNode(), OP_STR, exprType.getName(), "");
        node.setType(this->typePool().getUnresolvedType());
      }
    }
  }
}

void TypeChecker::visitWithNode(WithNode &node) {
  auto scope = this->intoBlock();

  // register redir config
  this->addEntry(node, node.toCtxName(), this->typePool().get(TYPE::Any), HandleAttr::READ_ONLY);

  auto &type = this->checkTypeExactly(node.getExprNode());
  for (auto &e : node.getRedirNodes()) {
    this->checkTypeAsExpr(*e);
  }

  node.setBaseIndex(this->curScope->getBaseIndex());
  node.setType(type);
}

void TypeChecker::visitTimeNode(TimeNode &node) {
  auto scope = this->intoBlock();

  // register timer entry
  this->addEntry(node, node.toCtxName(), this->typePool().get(TYPE::Any), HandleAttr::READ_ONLY);

  auto &type = this->checkTypeExactly(node.getExprNode());
  node.setBaseIndex(this->curScope->getBaseIndex());
  node.setType(type);
}

void TypeChecker::visitForkNode(ForkNode &node) {
  auto child = this->funcCtx->intoChild();
  this->checkTypeExactly(node.getExprNode());

  const Type *type = nullptr;
  switch (node.getOpKind()) {
  case ForkKind::STR:
    type = &this->typePool().get(TYPE::String);
    break;
  case ForkKind::ARRAY:
    type = &this->typePool().get(TYPE::StringArray);
    break;
  case ForkKind::IN_PIPE:
  case ForkKind::OUT_PIPE:
    type = &this->typePool().get(TYPE::FD);
    break;
  case ForkKind::JOB:
  case ForkKind::COPROC:
  case ForkKind::DISOWN:
  case ForkKind::NONE:      // unreachable
  case ForkKind::PIPE_FAIL: // unreachable
    if (node.getExprNode().getType().is(TYPE::Job)) {
      this->reportError<NestedJob>(node.getExprNode());
    }
    type = &this->typePool().get(TYPE::Job);
    break;
  }
  node.setType(*type);
}

void TypeChecker::visitAssertNode(AssertNode &node) {
  this->checkTypeWithCoercion(this->typePool().get(TYPE::Bool), node.refCondNode());
  this->checkType(this->typePool().get(TYPE::String), node.getMessageNode());
  if (isa<BinaryOpNode>(node.getCondNode())) {
    auto &binaryNode = cast<BinaryOpNode>(node.getCondNode());
    if (isa<ApplyNode>(binaryNode.getOptNode())) {
      auto &applyNode = cast<ApplyNode>(*binaryNode.getOptNode());
      const auto opKind = binaryNode.getOp();
      const auto op = opKind == TokenKind::EQ      ? AssertOp::EQ
                      : opKind == TokenKind::MATCH ? AssertOp::MATCH
                                                   : AssertOp::DEFAULT;
      applyNode.setAssertOp(op);
    } else if (binaryNode.getOp() == TokenKind::EQ) {
      binaryNode.setAssertOp(AssertOp::EQ);
    }
  }
  node.setType(this->typePool().get(TYPE::Void));
}

void TypeChecker::visitBlockNode(BlockNode &node) {
  if (this->isTopLevel() && node.getNodes().empty()) {
    this->reportError<UselessBlock>(node);
  }
  auto scope = this->intoBlock();
  this->checkTypeWithCurrentScope(nullptr, node);
}

void TypeChecker::visitTypeDefNode(TypeDefNode &node) {
  for (auto &e : node.getAttrNodes()) {
    this->checkTypeExactly(*e);
  }
  switch (node.getDefKind()) {
  case TypeDefNode::ALIAS: {
    auto &nameInfo = node.getNameInfo();
    TypeNode &typeToken = node.getTargetTypeNode();
    auto &type = this->checkTypeExactly(typeToken);
    bool shadowing = false;
    if (this->allowWarning && !this->curScope->isGlobal() && !this->isConstructorTopLevel()) {
      if (this->curScope->lookup(toTypeAliasFullName(nameInfo.getName()))) {
        shadowing = true;
      }
    }
    if (auto ret = this->curScope->defineTypeAlias(this->typePool(), nameInfo.getName(), type)) {
      node.setHandle(ret.asOk());
      if (shadowing) {
        this->reportError<TypeAliasShadowing>(nameInfo.getToken(), nameInfo.getName().c_str());
      }
    } else {
      this->reportNameRegisterError(nameInfo.getToken(), ErrorSymbolKind::TYPE_ALIAS, ret.asErr(),
                                    nameInfo.getName());
    }
    break;
  }
  case TypeDefNode::ERROR_DEF: {
    if (!this->isTopLevel()) { // only available toplevel scope
      this->reportError<OutsideToplevel>(node, "error type definition");
      break;
    }
    auto &errorType = this->checkType(this->typePool().get(TYPE::Error), node.getTargetTypeNode());
    if (auto typeOrError =
            this->typePool().createErrorType(node.getName(), errorType, this->curScope->modId)) {
      if (auto ret = this->curScope->defineTypeAlias(this->typePool(), node.getName(),
                                                     *typeOrError.asOk())) {
        node.setHandle(ret.asOk());
      } else {
        this->reportNameRegisterError(node.getNameInfo().getToken(), ErrorSymbolKind::TYPE_ALIAS,
                                      ret.asErr(), node.getName());
      }
    } else {
      this->reportError(node.getNameInfo().getToken(), std::move(*typeOrError.asErr()));
    }
    break;
  }
  }
  node.setType(this->typePool().get(TYPE::Void));
}

void TypeChecker::visitDeferNode(DeferNode &node) {
  auto cleanup = this->funcCtx->intoFinally();
  auto block = this->intoBlock();
  this->checkTypeWithCurrentScope(node.getBlockNode());
  if (node.getBlockNode().getNodes().empty()) {
    this->reportError<UselessBlock>(node.getBlockNode());
  }
  if (node.getBlockNode().getType().isNothingType()) {
    this->reportError<InsideFinally>(node.getBlockNode());
  }
  node.setType(this->typePool().get(TYPE::Void));
}

void TypeChecker::visitLoopNode(LoopNode &node) {
  {
    auto scope = this->intoBlock();
    this->checkTypeWithCoercion(this->typePool().get(TYPE::Void), node.refInitNode());

    if (node.getCondNode() != nullptr) {
      this->checkTypeWithCoercion(this->typePool().get(TYPE::Bool), node.refCondNode());
    }
    this->checkTypeWithCoercion(this->typePool().get(TYPE::Void), node.refIterNode());

    {
      auto loop = this->funcCtx->intoLoop();
      this->checkTypeWithCurrentScope(node.getBlockNode());
      auto &type = this->resolveCoercionOfJumpValue(this->funcCtx->getJumpNodes());
      node.setType(type);
    }
  }

  // adjust local offset
  if (node.getInitNode().is(NodeKind::VarDecl)) {
    auto &blockNode = node.getBlockNode();
    const unsigned int baseIndex = blockNode.getBaseIndex();
    const unsigned int varSize = blockNode.getVarSize();
    blockNode.setBaseIndex(baseIndex + 1);
    blockNode.setVarSize(varSize - 1);
  }
}

void TypeChecker::resolveIfLet(IfNode &node) {
  assert(node.isIfLet());
  assert(isa<BlockNode>(node.getThenNode()));
  node.setIfLetKind(IfNode::IfLetKind::ERROR);
  auto &condNode = node.getCondNode();
  assert(isa<VarDeclNode>(condNode));
  auto &varDeclNode = cast<VarDeclNode>(condNode);
  varDeclNode.setType(this->typePool().get(TYPE::Void));
  const auto declKind = varDeclNode.getKind();
  assert(declKind == VarDeclNode::LET || declKind == VarDeclNode::VAR);
  auto &exprNode = *varDeclNode.getExprNode();
  auto &exprType = this->checkTypeAsExpr(exprNode);
  if (!exprType.isOptionType()) {
    this->reportError<IfLetOpt>(exprNode, exprType.getName());
    return;
  }
  node.setIfLetKind(IfNode::IfLetKind::UNWRAP);
  auto emptyNode = std::make_unique<EmptyNode>(varDeclNode.getNameInfo().getToken());
  emptyNode->setType(cast<OptionType>(exprType).getElementType());
  auto decl = std::make_unique<VarDeclNode>(
      varDeclNode.getPos(), NameInfo(varDeclNode.getNameInfo()), std::move(emptyNode), declKind);
  cast<BlockNode>(node.getThenNode()).insertNodeToFirst(std::move(decl));
}

void TypeChecker::visitIfNode(IfNode &node) {
  if (node.isIfLet()) {
    this->resolveIfLet(node);
  } else {
    this->checkTypeWithCoercion(this->typePool().get(TYPE::Bool), node.refCondNode());
  }
  this->checkTypeExactly(node.getThenNode());
  this->checkTypeExactly(node.getElseNode());

  if (node.isElif()) {
    /**
     * dummy. actual type is resolved from parent IfNode
     */
    node.setType(this->typePool().getUnresolvedType());
    return;
  }

  // resolve common type of if-elif-else chain
  std::vector<const Type *> types;
  types.reserve(4);
  types.push_back(&node.getThenNode().getType());

  for (const auto *elseNode = &node.refElseNode();;) {
    if (isa<IfNode>(**elseNode) && cast<IfNode>(**elseNode).isElif()) {
      auto &elifNode = cast<IfNode>(**elseNode);
      assert(elifNode.getType().isUnresolved());
      types.push_back(&elifNode.getThenNode().getType());
      elseNode = &elifNode.refElseNode();
    } else {
      types.push_back(&elseNode->get()->getType());
      break;
    }
  }
  auto &type =
      this->resolveCommonSuperType(node, std::move(types), &this->typePool().get(TYPE::Void));

  // apply coercion
  this->checkTypeWithCoercion(type, node.refThenNode());
  for (auto *elseNode = &node.refElseNode();;) {
    if (isa<IfNode>(**elseNode) && cast<IfNode>(**elseNode).isElif()) {
      auto &elifNode = cast<IfNode>(**elseNode);
      elifNode.setType(type);
      this->checkTypeWithCoercion(type, elifNode.refThenNode());
      elseNode = &elifNode.refElseNode();
    } else {
      this->checkTypeWithCoercion(type, *elseNode);
      break;
    }
  }
  node.setType(type);
}

bool TypeChecker::IntPatternMap::collect(const Node &constNode) {
  if (constNode.getNodeKind() != NodeKind::Number) {
    return false;
  }
  const int64_t value = cast<const NumberNode>(constNode).getIntValue();
  const auto pair = this->set.insert(value);
  return pair.second;
}

bool TypeChecker::StrPatternMap::collect(const Node &constNode) {
  if (constNode.getNodeKind() != NodeKind::String) {
    return false;
  }
  const char *str = cast<const StringNode>(constNode).getValue().c_str();
  const auto pair = this->set.insert(str);
  return pair.second;
}

bool TypeChecker::PatternCollector::collect(const Node &constNode) {
  if (!this->map) {
    switch (constNode.getNodeKind()) {
    case NodeKind::Number:
      this->map = std::make_unique<IntPatternMap>();
      break;
    case NodeKind::String:
      this->map = std::make_unique<StrPatternMap>();
      break;
    default:
      break;
    }
  }
  return this->map && this->map->collect(constNode);
}

void TypeChecker::visitCaseNode(CaseNode &node) {
  auto *exprType = &this->checkTypeAsExpr(node.getExprNode());

  // check pattern type
  PatternCollector collector;
  for (auto &e : node.getArmNodes()) {
    this->checkPatternType(*e, collector);
  }

  // check type expr
  node.setCaseKind(collector.getKind());
  if (auto *patternType = collector.getType(); patternType) {
    if (exprType->isOptionType()) {
      exprType = &cast<OptionType>(exprType)->getElementType();
    }
    if (!patternType->isSameOrBaseTypeOf(*exprType)) {
      this->reportError<Required>(node.getExprNode(), patternType->getName(), exprType->getName());
    }
  } else {
    this->reportError<NeedPattern>(node);
  }

  // resolve arm expr type
  const unsigned int size = node.getArmNodes().size();
  std::vector<const Type *> types(size);
  for (unsigned int i = 0; i < size; i++) {
    types[i] = &this->checkTypeExactly(*node.getArmNodes()[i]);
  }
  auto &type =
      this->resolveCommonSuperType(node, std::move(types), &this->typePool().get(TYPE::Void));

  // apply coercion
  for (auto &armNode : node.getArmNodes()) {
    this->checkTypeWithCoercion(type, armNode->refActionNode());
    armNode->setType(type);
  }

  if (!type.isVoidType() && !collector.hasElsePattern()) {
    this->reportError<NeedDefault>(node);
  }
  node.setType(type);
}

void TypeChecker::visitArmNode(ArmNode &node) {
  auto &type = this->checkTypeExactly(node.getActionNode());
  node.setType(type);
}

void TypeChecker::checkPatternType(ArmNode &node, PatternCollector &collector) {
  if (node.getPatternNodes().empty()) {
    if (collector.hasElsePattern()) {
      Token token{node.getPos(), 4};
      const auto elseNode = std::make_unique<StringNode>(token, "else");
      this->reportError<DupPattern>(*elseNode);
    }
    collector.setElsePattern(true);
  }

  // check pattern type
  for (auto &e : node.getPatternNodes()) {
    auto *type = &this->checkTypeAsExpr(*e);
    if (type->isOptionType()) {
      this->reportError<Unacceptable>(*e, type->getName());
    }
    if (type->is(TYPE::Regex)) {
      collector.setKind(CaseNode::IF_ELSE);
      type = &this->typePool().get(TYPE::String);
    }
    if (collector.getType() == nullptr) {
      collector.setType(type);
    }
    if (*collector.getType() != *type) {
      this->reportError<Required>(*e, collector.getType()->getName(), type->getName());
    }
  }

  // constant folding
  std::vector<std::unique_ptr<Node>> constNodes;
  for (auto &e : node.getPatternNodes()) {
    auto constNode = this->evalConstant(*e);
    if (!constNode) {
      return;
    }
    constNodes.push_back(std::move(constNode));
  }
  node.setConstPatternNodes(std::move(constNodes));

  // check duplicated patterns
  for (auto &e : node.getConstPatternNodes()) {
    if (e->is(NodeKind::Regex)) {
      continue;
    }
    if (!collector.collect(*e)) {
      this->reportError<DupPattern>(*e);
    }
  }
}

const Type &TypeChecker::resolveCommonSuperType(const Node &node, std::vector<const Type *> &&types,
                                                const Type *fallbackType) {
  // remove Nothing? type
  bool hasOptNothing = false;
  for (auto iter = types.begin(); iter != types.end();) {
    if ((*iter)->is(TYPE::OptNothing)) {
      iter = types.erase(iter);
      hasOptNothing = true;
    } else {
      ++iter;
    }
  }

  std::sort(types.begin(), types.end(), [](const Type *x, const Type *y) {
    /**
     *  require weak ordering (see. https://cpprefjp.github.io/reference/algorithm.html)
     */
    return x != y && x->isSameOrBaseTypeOf(*y);
  });

  unsigned int count = 1;
  for (; count < types.size(); count++) {
    if (!types[0]->isSameOrBaseTypeOf(*types[count])) {
      break;
    }
  }
  if (count == types.size()) {
    if (hasOptNothing) {
      /**
       * [T, Nothing?]
       *  -> if T is Void -> Void
       *  -> if T is U? -> U?
       *  -> otherwise -> T?
       */
      const auto *type = types[0];
      if (type->isVoidType() || type->isOptionType()) {
        return *type;
      } else if (auto ret = this->typePool().createOptionType(*type)) {
        return *ret.asOk();
      }
    } else {
      return *types[0];
    }
  } else if (hasOptNothing && types.empty()) {
    return this->typePool().get(TYPE::OptNothing);
  }

  if (fallbackType) {
    return *fallbackType;
  } else {
    std::string value;
    for (const auto &t : types) {
      if (!value.empty()) {
        value += ", ";
      }
      value += t->getNameRef();
    }
    this->reportError<NoCommonSuper>(node, value.c_str());
    return this->typePool().getUnresolvedType();
  }
}

#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto __v = E;                                                                                  \
    if (!__v) {                                                                                    \
      return nullptr;                                                                              \
    }                                                                                              \
    std::forward<decltype(__v)>(__v);                                                              \
  })

std::unique_ptr<Node> TypeChecker::evalConstant(const Node &node) {
  if (node.isUntyped() || node.getType().isUnresolved()) {
    return nullptr;
  }
  switch (node.getNodeKind()) {
  case NodeKind::Number:
    return cast<NumberNode>(node).clone();
  case NodeKind::String: {
    auto &strNode = cast<StringNode>(node);
    auto constNode = std::make_unique<StringNode>(
        strNode.getToken(), std::string(strNode.getValue()), strNode.getKind());
    constNode->setType(strNode.getType());
    constNode->setEscaped(strNode.isEscaped());
    return constNode;
  }
  case NodeKind::Regex: {
    auto &regexNode = cast<RegexNode>(node);
    auto constNode =
        std::make_unique<RegexNode>(regexNode.getToken(), std::string(regexNode.getReStr()),
                                    std::string(regexNode.getReFlag()));
    this->checkTypeAsExpr(*constNode);
    return constNode;
  }
  case NodeKind::Array: {
    auto &arrayNode = cast<ArrayNode>(node);
    const unsigned int size = arrayNode.getExprNodes().size();
    auto exprNode = TRY(this->evalConstant(*arrayNode.getExprNodes()[0]));
    auto constNode = std::make_unique<ArrayNode>(arrayNode.getPos(), std::move(exprNode));
    for (unsigned int i = 1; i < size; i++) {
      exprNode = TRY(this->evalConstant(*arrayNode.getExprNodes()[i]));
      constNode->addExprNode(std::move(exprNode));
    }
    constNode->updateToken({arrayNode.getToken().endPos() - 1, 1});
    constNode->setType(arrayNode.getType());
    return constNode;
  }
  case NodeKind::Tuple: {
    auto &tupleNode = cast<TupleNode>(node);
    Token endToken{tupleNode.getToken().endPos() - 1, 1};
    std::vector<std::unique_ptr<Node>> nodes;
    nodes.reserve(tupleNode.getNodes().size());
    for (auto &e : tupleNode.getNodes()) {
      nodes.push_back(TRY(this->evalConstant(*e)));
    }
    auto constNode = std::make_unique<TupleNode>(tupleNode.getPos(), std::move(nodes), endToken);
    constNode->setType(tupleNode.getType());
    return constNode;
  }
  case NodeKind::WildCard: {
    auto &wildCardNode = cast<WildCardNode>(node);
    if (wildCardNode.isExpand()) {
      auto constNode = std::make_unique<WildCardNode>(wildCardNode.getToken(), wildCardNode.meta);
      constNode->setExpand(wildCardNode.isExpand());
      constNode->setBraceId(wildCardNode.getBraceId());
      constNode->setType(wildCardNode.getType());
      return constNode;
    } else {
      auto constNode = std::make_unique<StringNode>(
          wildCardNode.getToken(), toString(wildCardNode.meta), StringNode::STRING);
      constNode->setType(this->typePool().get(TYPE::String));
      return constNode;
    }
  }
  case NodeKind::BraceSeq: {
    auto &seqNode = cast<BraceSeqNode>(node);
    auto constNode = std::make_unique<BraceSeqNode>(seqNode.getToken(), seqNode.getRange().kind);
    auto range = seqNode.getRange();
    constNode->setRange(range);
    constNode->setType(seqNode.getType());
    return constNode;
  }
  case NodeKind::UnaryOp: { // !, +, -, !
    auto &unaryNode = cast<UnaryOpNode>(node);
    Token token = node.getToken();
    const auto op = unaryNode.getOp();
    if (node.getType().is(TYPE::Int) &&
        (op == TokenKind::MINUS || op == TokenKind::PLUS || op == TokenKind::NOT)) {
      auto *applyNode = TRY(unaryNode.getApplyNode());
      assert(applyNode->getExprNode().is(NodeKind::Access));
      auto &accessNode = cast<AccessNode>(applyNode->getExprNode());
      auto &recvNode = accessNode.getRecvNode();
      auto constNode = TRY(this->evalConstant(recvNode));
      TRY(isa<NumberNode>(*constNode));
      int64_t value = cast<NumberNode>(*constNode).getIntValue();
      if (op == TokenKind::MINUS) {
        if (value == INT64_MIN) {
          this->reportError<NegativeIntMin>(node);
          return nullptr;
        }
        value = -value;
      } else if (op == TokenKind::NOT) {
        uint64_t v = ~static_cast<uint64_t>(value);
        value = static_cast<int64_t>(v);
      }
      constNode = NumberNode::newInt(token, value);
      constNode->setType(this->typePool().get(TYPE::Int));
      return constNode;
    }
    break;
  }
  case NodeKind::StringExpr: {
    auto &exprNode = cast<StringExprNode>(node);
    Token token = node.getToken();
    std::string value;
    for (auto &e : exprNode.getExprNodes()) {
      auto constNode = TRY(this->evalConstant(*e));
      TRY(isa<StringNode>(*constNode));
      value += cast<StringNode>(*constNode).getValue();
    }
    auto constNode = std::make_unique<StringNode>(token, std::move(value));
    constNode->setType(this->typePool().get(TYPE::String));
    return constNode;
  }
  case NodeKind::Embed: {
    auto &embedNode = cast<EmbedNode>(node);
    if (!embedNode.getHandle() && !embedNode.isUntyped() && embedNode.getType().is(TYPE::String)) {
      return this->evalConstant(embedNode.getExprNode());
    }
    break;
  }
  case NodeKind::Var: {
    auto &varNode = cast<VarNode>(node);
    Token token = varNode.getToken();
    std::string value;
    if (!varNode.getHandle()) {
      break;
    }
    if (varNode.getHandle()->is(HandleKind::MOD_CONST)) {
      if (varNode.getVarName() == CVAR_SCRIPT_NAME) {
        value = this->lexer.get().getSourceName();
      } else if (varNode.getVarName() == CVAR_SCRIPT_DIR) {
        value = this->lexer.get().getScriptDir();
      } else {
        break;
      }
    } else if (varNode.getHandle()->is(HandleKind::SYS_CONST)) {
      auto *ptr = this->config.lookup(varNode.getVarName());
      if (!ptr) {
        break;
      }
      value = *ptr;
    } else if (varNode.getHandle()->is(HandleKind::SMALL_CONST)) {
      ConstEntry entry(varNode.getIndex());
      std::unique_ptr<Node> constNode;
      auto v = static_cast<unsigned int>(entry.data.v);
      switch (entry.data.k) {
      case ConstEntry::Kind::INT:
        constNode = NumberNode::newInt(token, static_cast<int64_t>(v));
        break;
      case ConstEntry::Kind::BOOL:
        constNode = NumberNode::newBool(token, v != 0);
        break;
      case ConstEntry::Kind::SIG:
        constNode = NumberNode::newSignal(token, static_cast<int>(v));
        break;
      case ConstEntry::Kind::NONE:
        constNode = NumberNode::newNone(token);
        break;
      }
      this->checkTypeAsExpr(*constNode);
      return constNode;
    } else {
      break;
    }
    TRY(varNode.getType().is(TYPE::String));
    auto constNode = std::make_unique<StringNode>(token, std::move(value));
    constNode->setType(this->typePool().get(TYPE::String));
    return constNode;
  }
  case NodeKind::New: {
    auto &newNode = cast<NewNode>(node);
    if (newNode.getType().is(TYPE::Regex) && newNode.getArgsNode().getNodes().size() == 2) {
      auto strNode = TRY(this->evalConstant(*newNode.getArgsNode().getNodes()[0]));
      auto flagNode = TRY(this->evalConstant(*newNode.getArgsNode().getNodes()[1]));
      TRY(isa<StringNode>(*strNode));
      TRY(isa<StringNode>(*flagNode));
      Token token = newNode.getToken();
      auto reStr = cast<StringNode>(*strNode).getValue();
      auto reFlag = cast<StringNode>(*flagNode).getValue();
      auto constNode = std::make_unique<RegexNode>(token, std::move(reStr), std::move(reFlag));
      this->checkTypeAsExpr(*constNode);
      return constNode;
    }
    break;
  }
  default:
    break;
  }
  this->reportError<Constant>(node);
  return nullptr;
}

void TypeChecker::checkTypeAsBreakContinue(JumpNode &node) {
  if (this->funcCtx->loopLevel() == 0) {
    this->reportError<InsideLoop>(node.getActualToken());
    return;
  }

  if (this->funcCtx->finallyLevel() > this->funcCtx->loopLevel()) {
    this->reportError<InsideFinally>(node.getActualToken());
  }

  if (this->funcCtx->childLevel() > this->funcCtx->loopLevel()) {
    this->reportError<InsideChild>(node.getActualToken());
  }

  if (this->funcCtx->tryCatchLevel() > this->funcCtx->loopLevel()) {
    node.setTryDepth(this->funcCtx->tryCatchLevel() - this->funcCtx->loopLevel());
  }

  if (node.getExprNode().is(NodeKind::Empty)) {
    this->checkType(this->typePool().get(TYPE::Void), node.getExprNode());
  } else if (node.getOpKind() == JumpNode::BREAK) {
    this->checkTypeAsSomeExpr(node.getExprNode());
    this->funcCtx->addJumpNode(&node);
  }
  assert(!node.getExprNode().isUntyped());
}

void TypeChecker::checkTypeAsReturn(JumpNode &node) {
  if (this->funcCtx->finallyLevel() > 0) {
    this->reportError<InsideFinally>(node.getActualToken());
  }

  if (this->funcCtx->childLevel() > 0) {
    this->reportError<InsideChild>(node.getActualToken());
  }

  if (!this->funcCtx->withinFunc()) {
    this->reportError<InsideFunc>(node.getActualToken());
    return;
  }

  // check return expr
  auto *returnType = this->funcCtx->getReturnType();
  auto &exprType = returnType ? this->checkType(*returnType, node.getExprNode())
                              : this->checkTypeExactly(node.getExprNode());
  if (exprType.isVoidType() && !isa<EmptyNode>(node.getExprNode())) {
    this->reportError<NotNeedExpr>(node.getExprNode());
  }
  if (!returnType) {
    this->funcCtx->addReturnNode(&node);
  }
}

void TypeChecker::visitJumpNode(JumpNode &node) {
  switch (node.getOpKind()) {
  case JumpNode::BREAK:
  case JumpNode::CONTINUE:
  case JumpNode::IMPLICIT_CONTINUE:
    this->checkTypeAsBreakContinue(node);
    break;
  case JumpNode::THROW:
    if (this->funcCtx->finallyLevel() > this->funcCtx->childLevel()) {
      this->reportError<InsideFinally>(node.getActualToken());
    }
    this->checkType(this->typePool().get(TYPE::Error), node.getExprNode());
    break;
  case JumpNode::RETURN:
  case JumpNode::RETURN_INIT: // normally unreachable
    this->checkTypeAsReturn(node);
    break;
  }
  node.setType(this->typePool().get(TYPE::Nothing));
}

void TypeChecker::visitCatchNode(CatchNode &node) {
  auto &exceptionType = this->checkTypeExactly(node.getTypeNode());
  if (exceptionType.isNothingType() ||
      !this->typePool().get(TYPE::Error).isSameOrBaseTypeOf(exceptionType)) {
    this->reportError<InvalidCatchType>(node.getTypeNode(), exceptionType.getName());
  }

  {
    auto scope = this->intoBlock();
    /**
     * check type catch block
     */
    if (const auto handle =
            this->addEntry(node.getNameInfo(), exceptionType, HandleAttr::READ_ONLY)) {
      node.setAttribute(*handle);
    }
    this->checkTypeWithCurrentScope(nullptr, node.getBlockNode());
  }
  node.setType(node.getBlockNode().getType());
}

void TypeChecker::visitTryNode(TryNode &node) {
  assert(node.getExprNode().is(NodeKind::Block));
  if (cast<BlockNode>(node.getExprNode()).getNodes().empty()) {
    this->reportError<EmptyTry>(node.getExprNode());
  }

  // check type try block
  const Type *exprType;
  {
    auto try1 = this->funcCtx->intoTry();
    exprType = &this->checkTypeExactly(node.getExprNode());
  }

  // check type catch block
  for (auto &c : node.getCatchNodes()) {
    auto try1 = this->funcCtx->intoTry();
    this->checkTypeExactly(*c);
  }
  for (auto &c : node.getCatchNodes()) {
    if (!exprType->isSameOrBaseTypeOf(c->getType())) {
      exprType = &this->typePool().get(TYPE::Void);
      break;
    }
  }

  // perform coercion
  this->checkTypeWithCoercion(*exprType, node.refExprNode());
  for (auto &c : node.refCatchNodes()) {
    this->checkTypeWithCoercion(*exprType, c);
  }

  // check type finally block
  if (node.getFinallyNode() != nullptr) {
    this->checkTypeExactly(*node.getFinallyNode());
  }

  /**
   * verify catch block order
   */
  const size_t size = node.getCatchNodes().size();
  for (size_t i = 1; i < size; i++) {
    auto &curType =
        findInnerNode<CatchNode>(node.getCatchNodes()[i - 1].get())->getTypeNode().getType();
    auto &nextType =
        findInnerNode<CatchNode>(node.getCatchNodes()[i].get())->getTypeNode().getType();
    if (curType.isSameOrBaseTypeOf(nextType)) {
      auto &nextNode = node.getCatchNodes()[i];
      this->reportError<Unreachable>(*nextNode);
    }
  }
  node.setType(*exprType);
}

void TypeChecker::checkTypeVarDecl(VarDeclNode &node, bool willBeField) {
  this->checkAttributes(node.getAttrNodes(), willBeField);

  switch (node.getKind()) {
  case VarDeclNode::LET:
  case VarDeclNode::VAR: {
    HandleAttr attr{};
    if (node.getKind() == VarDeclNode::LET) {
      setFlag(attr, HandleAttr::READ_ONLY);
    }
    if (willBeField) {
      setFlag(attr, HandleAttr::UNCAPTURED);
    }
    auto &exprType = this->checkTypeAsSomeExpr(*node.getExprNode());
    if (const auto handle = this->addEntry(node.getNameInfo(), exprType, attr)) {
      if (willBeField && isa<CLIRecordType>(this->funcCtx->getReturnType())) {
        if (auto &cliType = this->typePool().get(TYPE::CLI);
            this->typePool().hasMethod(cliType, node.getVarName())) {
          this->reportError<SameNameCLIField>(node.getNameInfo().getToken(),
                                              node.getVarName().c_str());
        }
      }
      node.setHandle(handle);
    }

    // check attribute type
    if (!node.getAttrNodes().empty()) {
      this->postCheckFieldAttributes(node);
    }
    break;
  }
  case VarDeclNode::IMPORT_ENV:
  case VarDeclNode::EXPORT_ENV: {
    assert(node.getAttrNodes().empty());
    if (node.getExprNode() != nullptr) {
      this->checkType(this->typePool().get(TYPE::String), *node.getExprNode());
    }
    const bool allowCapture = !willBeField;
    if (const auto handle =
            this->addEnvEntry(node.getNameInfo().getToken(), node.getVarName(), allowCapture)) {
      node.setHandle(handle);
    }
    break;
  }
  }
  node.setType(this->typePool().get(TYPE::Void));
}

void TypeChecker::visitVarDeclNode(VarDeclNode &node) {
  const bool willBeField = this->isConstructorTopLevel();
  this->checkTypeVarDecl(node, willBeField);
}

void TypeChecker::visitAssignNode(AssignNode &node) {
  auto &leftNode = node.getLeftNode();
  auto &leftType = this->checkTypeAsExpr(leftNode);
  if (isa<AssignableNode>(leftNode)) {
    const auto &assignable = cast<AssignableNode>(leftNode);
    if (assignable.getHandle() && assignable.hasAttr(HandleAttr::READ_ONLY)) {
      if (isa<VarNode>(leftNode)) {
        this->reportError<ReadOnlySymbol>(leftNode, cast<VarNode>(leftNode).getVarName().c_str());
      } else {
        assert(isa<AccessNode>(leftNode));
        const auto &accessNode = cast<AccessNode>(leftNode);
        this->reportError<ReadOnlyField>(accessNode.getNameToken(),
                                         accessNode.getFieldName().c_str(),
                                         accessNode.getRecvNode().getType().getName());
      }
    }
  } else {
    this->reportError<Assignable>(leftNode);
  }

  if (leftNode.is(NodeKind::Access)) {
    node.setAttribute(AssignNode::FIELD_ASSIGN);
  }
  if (node.isSelfAssignment()) {
    assert(node.getRightNode().is(NodeKind::BinaryOp));
    const auto &opNode = cast<BinaryOpNode>(node.getRightNode());
    opNode.getLeftNode()->setType(leftType);
    if (isa<AccessNode>(leftNode)) {
      cast<AccessNode>(leftNode).setAdditionalOp(AccessNode::DUP_RECV);
    }
  }
  this->checkType(leftType, node.getRightNode());
  node.setType(this->typePool().get(TYPE::Void));
}

void TypeChecker::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
  auto &recvType = this->checkTypeAsExpr(node.getRecvNode());
  auto &indexType = this->checkTypeAsExpr(node.getIndexNode());

  node.setRecvType(recvType);
  node.setIndexType(indexType);

  auto &elementType = this->checkTypeAsExpr(node.getGetterNode());
  cast<BinaryOpNode>(node.getRightNode()).getLeftNode()->setType(elementType);
  this->checkType(elementType, node.getRightNode());

  node.getSetterNode().getArgsNode().getNodes()[1]->setType(elementType);
  this->checkType(this->typePool().get(TYPE::Void), node.getSetterNode());

  node.setType(this->typePool().get(TYPE::Void));
}

void TypeChecker::visitPrefixAssignNode(PrefixAssignNode &node) {
  if (node.getExprNode()) { // AAA=1243 BBB='fre' expr
    auto scope = this->intoBlock();

    // register envctx
    this->addEntry(node, node.toCtxName(), this->typePool().get(TYPE::Any), HandleAttr::READ_ONLY);

    for (auto &e : node.getAssignNodes()) {
      auto &rightType = this->checkType(this->typePool().get(TYPE::String), e->getRightNode());
      assert(isa<VarNode>(e->getLeftNode()));
      auto &leftNode = cast<VarNode>(e->getLeftNode());
      leftNode.setType(this->typePool().getUnresolvedType());
      if (const auto handle =
              this->addEnvEntry(leftNode.getToken(), leftNode.getVarName(), false)) {
        leftNode.setHandle(handle);
        leftNode.setType(rightType);
      }
    }

    auto &exprType = this->checkTypeExactly(*node.getExprNode());
    node.setBaseIndex(this->curScope->getBaseIndex());
    node.setType(exprType);
  } else { // AAA=1234 BBB='fer'
    for (auto &e : node.getAssignNodes()) {
      this->checkType(this->typePool().get(TYPE::String), e->getLeftNode());
      this->checkTypeExactly(*e);
    }
    node.setType(this->typePool().get(TYPE::Void));
  }
}

void TypeChecker::registerRecordType(FunctionNode &node) {
  assert(node.isConstructor());

  // check CLI attribute
  auto [cli, attr, desc] = this->postCheckConstructorAttribute(node);
  auto typeOrError =
      cli ? this->typePool().createCLIRecordType(node.getFuncName(), this->curScope->modId, attr,
                                                 desc.toString())
          : this->typePool().createRecordType(node.getFuncName(), this->curScope->modId);
  if (typeOrError) {
    auto &recordType = *typeOrError.asOk();
    if (auto ret =
            this->curScope->defineTypeAlias(this->typePool(), node.getFuncName(), recordType)) {
      node.setResolvedType(recordType);
    } else {
      this->reportNameRegisterError(node.getNameInfo().getToken(), ErrorSymbolKind::TYPE_ALIAS,
                                    ret.asErr(), node.getFuncName());
    }
  } else {
    this->reportError(node.getNameInfo().getToken(), std::move(*typeOrError.asErr()));
  }
}

static PackedParamNames createPackedParamNames(const FunctionNode &node) {
  const unsigned int size = node.getParamNodes().size();

  // compute buffer size (due to suppress excessive memory allocation)
  unsigned int reservingSize = 0;
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      reservingSize++;
    }
    reservingSize += static_cast<unsigned int>(node.getParamNodes()[i]->getVarName().size());
  }
  if (reservingSize) {
    reservingSize++; // for sentinel
  }

  PackedParamNamesBuilder builder(reservingSize);
  for (unsigned int i = 0; i < size; i++) {
    builder.addParamName(node.getParamNodes()[i]->getVarName());
  }
  return std::move(builder).build();
}

void TypeChecker::registerFuncHandle(FunctionNode &node) {
  std::vector<const Type *> paramTypes(node.getParamNodes().size());
  for (unsigned int i = 0; i < node.getParamNodes().size(); i++) {
    paramTypes[i] = &node.getParamNodes()[i]->getExprNode()->getType();
  }
  auto packed = createPackedParamNames(node);

  if (node.isMethod()) {
    auto &recvType = node.getRecvTypeNode()->getType();
    const auto recvModId = recvType.resolveBelongedModId();
    if (isBuiltinMod(recvModId)) {
      this->reportError<NeedUdType>(*node.getRecvTypeNode());
    } else if (recvModId != this->curScope->modId) {
      this->reportError<SameModOfRecv>(node, recvType.getName());
    } else if (this->curScope->lookupField(this->typePool(), recvType, node.getFuncName())) {
      this->reportError<SameNameField>(node.getNameInfo().getToken(), node.getFuncName().c_str(),
                                       recvType.getName());
    } else {
      auto ret = this->curScope->defineMethod(this->typePool(), recvType, node.getFuncName(),
                                              node.getReturnTypeNode()->getType(), paramTypes,
                                              std::move(packed));
      if (ret) {
        assert(ret.asOk()->isMethodHandle());
        node.setHandle(std::move(ret).take());
      } else {
        this->reportNameRegisterError(node.getNameInfo().getToken(), ErrorSymbolKind::METHOD,
                                      ret.asErr(), node.getFuncName(), recvType.getName());
      }
    }
  } else if (node.getReturnTypeNode()) { // for named function
    assert(!node.isConstructor());
    auto typeOrError =
        this->typePool().createFuncType(node.getReturnTypeNode()->getType(), std::move(paramTypes));
    if (typeOrError) {
      auto &funcType = cast<FunctionType>(*std::move(typeOrError).take());
      node.setResolvedType(funcType);
      if (!node.isAnonymousFunc()) {
        auto &nameInfo = node.getNameInfo();
        if (auto ret = this->curScope->defineNamedFunction(nameInfo.getName(), funcType,
                                                           std::move(packed))) {
          node.setHandle(std::move(ret).take());
        } else {
          this->reportNameRegisterError(nameInfo.getToken(), ErrorSymbolKind::VAR, ret.asErr(),
                                        nameInfo.getName());
        }
      }
    } else {
      this->reportError(node.getToken(), std::move(*typeOrError.asErr()));
    }
  } else if (node.isConstructor()) {
    if (auto *type = node.getResolvedType(); type && type->isRecordOrDerived()) {
      auto ret = this->curScope->defineConstructor(this->typePool(), cast<RecordType>(*type),
                                                   paramTypes, std::move(packed));
      assert(ret && ret.asOk()->isMethodHandle());
      node.setHandle(std::move(ret).take());
    }
  }
}

static void addReturnNodeToLast(BlockNode &blockNode, const TypePool &pool,
                                std::unique_ptr<Node> exprNode) {
  assert(!blockNode.isUntyped() && !blockNode.getType().isNothingType());
  assert(!exprNode->isUntyped());

  auto returnNode = JumpNode::newReturn(exprNode->getToken(), std::move(exprNode));
  returnNode->setType(pool.get(TYPE::Nothing));
  blockNode.setType(returnNode->getType());
  blockNode.addNode(std::move(returnNode));
}

void TypeChecker::postprocessFunction(FunctionNode &node) {
  assert(!node.isConstructor());
  assert(this->curScope->parent->kind == NameScope::FUNC);

  const Type *returnType = nullptr;
  if (node.getReturnTypeNode()) {
    returnType = &node.getReturnTypeNode()->getType();
  }

  // insert terminal node if not found
  BlockNode &blockNode = node.getBlockNode();
  if (!blockNode.getType().isNothingType()) {
    if (returnType && returnType->isVoidType()) {
      auto emptyNode = std::make_unique<EmptyNode>();
      emptyNode->setType(this->typePool().get(TYPE::Void));
      addReturnNodeToLast(blockNode, this->typePool(), std::move(emptyNode));
    } else if (node.isAnonymousFunc()) {
      std::unique_ptr<Node> lastNode;
      if (blockNode.getNodes().empty() || blockNode.getNodes().back()->getType().isVoidType() ||
          this->funcCtx->getVoidReturnCount() > 0) {
        lastNode = std::make_unique<EmptyNode>(blockNode.getToken());
        lastNode->setType(this->typePool().get(TYPE::Void));
      } else {
        lastNode = std::move(blockNode.refNodes().back());
        blockNode.refNodes().pop_back();
      }
      addReturnNodeToLast(blockNode, this->typePool(), std::move(lastNode));
      assert(isa<JumpNode>(*blockNode.getNodes().back()));
      this->checkTypeAsReturn(cast<JumpNode>(*blockNode.getNodes().back()));
    }
  }
  if (!blockNode.getType().isNothingType()) {
    this->reportError<UnfoundReturn>(node.getNameInfo().getToken(), node.getFuncName().c_str());
  }

  // resolve common return type
  const FunctionType *funcType = nullptr;
  if (node.getResolvedType() && isa<FunctionType>(node.getResolvedType())) {
    funcType = cast<FunctionType>(node.getResolvedType());
  }

  if (!returnType) {
    assert(!funcType);
    auto &type = blockNode.getType().isNothingType() && this->funcCtx->getReturnNodes().empty()
                     ? this->typePool().get(TYPE::Nothing)
                     : this->resolveCoercionOfJumpValue(this->funcCtx->getReturnNodes(), false);
    std::vector<const Type *> paramTypes(node.getParamNodes().size());
    for (unsigned int i = 0; i < node.getParamNodes().size(); i++) {
      paramTypes[i] = &node.getParamNodes()[i]->getExprNode()->getType();
    }
    if (auto typeOrError = this->typePool().createFuncType(type, std::move(paramTypes))) {
      funcType = cast<FunctionType>(std::move(typeOrError).take());
      node.setResolvedType(*funcType);
    } else {
      this->reportError(node.getToken(), std::move(*typeOrError.asErr()));
    }
  }
  if (node.isSingleExpr()) {
    auto retType = this->typePool().createOptionType(this->typePool().get(TYPE::Any));
    assert(retType);
    auto typeOrError = this->typePool().createFuncType(*retType.asOk(), {});
    assert(typeOrError);
    node.setType(*typeOrError.asOk()); // always `() -> Any!' type
  } else if (node.isAnonymousFunc() && funcType) {
    node.setType(*funcType);
  } else if (node.isMethod()) {
    node.setResolvedType(this->typePool().get(TYPE::Any));
  }

  // collect captured variables
  for (auto &e : this->curScope->parent->getCaptures()) {
    node.addCapture(e);
  }
}

void TypeChecker::postprocessConstructor(FunctionNode &node) {
  assert(node.isConstructor() && !node.getFuncName().empty());
  assert(this->curScope->parent->kind == NameScope::FUNC);

  if (!node.getResolvedType()) {
    return;
  }

  // finalize record type
  assert(node.getResolvedType()->isRecordOrDerived());
  auto &resolvedType = *node.getResolvedType();
  const unsigned int offset =
      node.kind == FunctionNode::EXPLICIT_CONSTRUCTOR ? node.getParamNodes().size() : 0;
  std::unordered_map<std::string, HandlePtr> handles;
  for (auto &e : this->curScope->getHandles()) {
    auto handle = e.second.first;
    if (!handle->is(HandleKind::TYPE_ALIAS) && !handle->isMethodHandle()) {
      if (handle->getIndex() < offset) {
        continue;
      }

      // field
      auto &fieldType = this->typePool().get(handle->getTypeId());
      handle = HandlePtr::create(fieldType, handle->getIndex() - offset, handle->getKind(),
                                 handle->attr(), handle->getModId());
    }
    handles.emplace(e.first, std::move(handle));
  }

  std::vector<ArgEntry> entries;
  if (isa<CLIRecordType>(resolvedType)) {
    entries = this->resolveArgEntries(node, offset);
  }

  // finalize record type
  auto typeOrError =
      isa<CLIRecordType>(resolvedType)
          ? this->typePool().finalizeCLIRecordType(cast<CLIRecordType>(resolvedType),
                                                   std::move(handles), std::move(entries))
          : this->typePool().finalizeRecordType(cast<RecordType>(resolvedType), std::move(handles));
  if (!typeOrError) {
    this->reportError(node.getNameInfo().getToken(), std::move(*typeOrError.asErr()));
  }

  const unsigned int fieldSize = cast<RecordType>(node.getResolvedType())->getFieldSize();
  auto returnNode = JumpNode::newReturnInit(*node.getResolvedType(), offset, fieldSize);
  returnNode->setType(this->typePool().get(TYPE::Nothing));
  node.getBlockNode().addNode(std::move(returnNode));
}

void TypeChecker::inferParamTypes(FunctionNode &node) {
  assert(node.isAnonymousFunc());

  // resolve required func type
  const FunctionType *funcType = nullptr;
  if (auto *type = this->getRequiredType(); type) {
    if (type->isOptionType()) {
      type = &cast<OptionType>(type)->getElementType();
    }
    if (type->isFuncType()) {
      funcType = cast<FunctionType>(type);
    }
  }

  // infer param type if type is missing
  const unsigned int paramSize = node.getParamNodes().size();
  const bool doInference = funcType && paramSize == funcType->getParamSize();
  for (unsigned int i = 0; i < paramSize; i++) {
    auto &paramNode = node.getParamNodes()[i];
    if (!paramNode->getExprNode()) {
      auto exprNode = std::make_unique<EmptyNode>();
      if (doInference) {
        exprNode->setType(funcType->getParamTypeAt(i));
      } else {
        exprNode->setType(this->typePool().getUnresolvedType());
        if (!funcType) {
          this->reportError<NotInferParamNoFunc>(*paramNode);
        } else {
          this->reportError<NotInferParamUnmatch>(*paramNode, funcType->getName(),
                                                  funcType->getParamSize(), paramSize);
        }
      }
      paramNode->setExprNode(std::move(exprNode));
    }
  }
}

void TypeChecker::checkTypeFunction(FunctionNode &node, const FuncCheckOp op) {
  if (hasFlag(op, FuncCheckOp::REGISTER_NAME)) {
    this->checkAttributes(node.getAttrNodes(), false);

    node.setType(this->typePool().get(TYPE::Void));
    if (!this->isTopLevel() && !node.isAnonymousFunc()) { // only available toplevel scope
      const char *message;
      if (node.isConstructor()) {
        message = "type definition";
      } else if (node.isMethod()) {
        message = "method definition";
      } else {
        message = "named function definition";
      }
      this->reportError<OutsideToplevel>(node.getActualToken(), message);
      return;
    }
    if (this->funcCtx->depth == SYS_LIMIT_FUNC_DEPTH) {
      this->reportError<FuncDepthLimit>(node.getActualToken());
      return;
    }

    if (node.isConstructor()) {
      this->registerRecordType(node);
    } else if (node.isAnonymousFunc()) {
      this->inferParamTypes(node);
    }

    // resolve param type, return type
    for (auto &paramNode : node.getParamNodes()) {
      this->checkTypeAsSomeExpr(*paramNode->getExprNode());
    }
    if (node.getReturnTypeNode()) {
      this->checkTypeExactly(*node.getReturnTypeNode());
    }

    if (node.isMethod()) {
      this->checkTypeAsSomeExpr(*node.getRecvTypeNode());
    }

    // register function/constructor handle
    this->registerFuncHandle(node);
  }

  if (hasFlag(op, FuncCheckOp::CHECK_BODY)) {
    // func body
    const auto *returnType = node.getReturnTypeNode() ? &node.getReturnTypeNode()->getType()
                             : node.isConstructor()   ? node.getResolvedType()
                                                      : nullptr;
    auto func = this->intoFunc(returnType,
                               node.isConstructor() ? FuncContext::CONSTRUCTOR : FuncContext::FUNC);
    // register parameter
    if (node.isMethod()) {
      const NameInfo nameInfo(node.getRecvTypeNode()->getToken(), VAR_THIS);
      this->addEntry(nameInfo, node.getRecvTypeNode()->getType(), HandleAttr::READ_ONLY);
    }
    for (auto &paramNode : node.getParamNodes()) {
      if (node.kind == FunctionNode::EXPLICIT_CONSTRUCTOR) {
        this->checkTypeVarDecl(*paramNode, false);
      } else {
        this->checkTypeExactly(*paramNode);
      }
    }
    // check type func body
    if (isa<CLIRecordType>(node.getResolvedType())) {
      Token dummy = node.getNameInfo().getToken();
      std::unique_ptr<Node> exprNode;
      if (const auto attr = cast<CLIRecordType>(node.getResolvedType())->getAttr();
          hasFlag(attr, CLIRecordType::Attr::TOPLEVEL)) {
        auto varNode = std::make_unique<VarNode>(Token{0, 0}, CVAR_ARG0);
        const auto handle = this->curScope->getGlobalScope()->lookup(CVAR_ARG0).take();
        varNode->setHandle(handle);
        varNode->setType(this->typePool().get(TYPE::String));
        exprNode = std::move(varNode);
      } else {
        exprNode = VarNode::createCurArg0(dummy);
      }
      auto nameDeclNode = std::make_unique<VarDeclNode>(dummy.pos, NameInfo(dummy, "%name"),
                                                        std::move(exprNode), VarDeclNode::VAR);
      node.getBlockNode().insertNodeToFirst(std::move(nameDeclNode));
    }
    this->checkTypeWithCurrentScope(
        node.isAnonymousFunc() ? nullptr : &this->typePool().get(TYPE::Void), node.getBlockNode());
    node.setMaxVarNum(this->curScope->getMaxLocalVarIndex());

    // post process
    if (node.isConstructor()) {
      this->postprocessConstructor(node);
    } else {
      this->postprocessFunction(node);
    }
  }
}

void TypeChecker::visitFunctionNode(FunctionNode &node) {
  this->checkTypeFunction(node, FuncCheckOp::REGISTER_NAME | FuncCheckOp::CHECK_BODY);
}

void TypeChecker::checkTypeUserDefinedCmd(UserDefinedCmdNode &node, const FuncCheckOp op) {
  if (hasFlag(op, FuncCheckOp::REGISTER_NAME)) {
    node.setType(this->typePool().get(TYPE::Void));

    if (!node.isAnonymousCmd() && !this->isTopLevel()) { // only available toplevel scope
      this->reportError<OutsideToplevel>(node.getActualToken(), "user-defined command definition");
      return;
    }

    if (node.getReturnTypeNode()) { // for Nothing type user-defined command
      this->checkType(this->typePool().get(TYPE::Nothing), *node.getReturnTypeNode());
    }

    if (node.getParamNode()) {
      if (auto &returnNode = node.getReturnTypeNode();
          returnNode && returnNode->getType().isNothingType()) {
        this->reportError<InvalidUDCParam>(*node.getParamNode());
      }
      auto &typeNode = *node.getParamTypeNode();
      this->checkTypeAsSomeExpr(typeNode);
      if (!typeNode.getType().isCLIRecordType()) {
        this->reportError<InvalidUDCParamType>(typeNode, typeNode.getType().getName());
      }
    }

    // register command name
    if (node.isAnonymousCmd()) {
      node.setType(this->typePool().get(TYPE::Command));
    } else if (auto handle = this->addUdcEntry(node)) {
      node.setHandle(std::move(handle));
    }
  }

  if (hasFlag(op, FuncCheckOp::CHECK_BODY)) {
    // check type udc body
    auto *returnType = &this->typePool().get(TYPE::Int);
    if (node.getHandle()) {
      returnType =
          &cast<FunctionType>(this->typePool().get(node.getHandle()->getTypeId())).getReturnType();
    }

    auto func = this->intoFunc(returnType); // pseudo return type
    {
      const auto old = this->allowWarning;
      this->allowWarning = false; // temporary disable variable shadowing check

      // register dummy parameter (for propagating command attr)
      this->addEntry(node, "%%attr", this->typePool().get(TYPE::Any), HandleAttr::READ_ONLY);

      // register dummy parameter (for closing file descriptor)
      this->addEntry(node, "%%redir", this->typePool().get(TYPE::Any), HandleAttr::READ_ONLY);

      // register special characters (@, 0)
      this->addEntry(node, "@", this->typePool().get(TYPE::StringArray), HandleAttr::READ_ONLY);
      this->addEntry(node, "0", this->typePool().get(TYPE::String), HandleAttr::READ_ONLY);
      this->allowWarning = old;
    }

    if (node.getParamNode()) {
      this->checkTypeExactly(*node.getParamNode());
    }

    // check type command body
    this->checkTypeWithCurrentScope(node.getBlockNode());
    node.setMaxVarNum(this->curScope->getMaxLocalVarIndex());

    // collect captured variables
    for (auto &e : this->curScope->parent->getCaptures()) {
      assert(node.isAnonymousCmd());
      node.addCapture(e);
    }

    // insert return node if not found
    if (node.getBlockNode().getNodes().empty() ||
        !node.getBlockNode().getNodes().back()->getType().isNothingType()) {
      if (returnType->isNothingType()) {
        this->reportError<UnfoundReturn>(node.getNameInfo().getToken(), node.getCmdName().c_str());
      } else {
        const unsigned int lastPos = node.getBlockNode().getToken().endPos();
        auto varNode = std::make_unique<VarNode>(Token{lastPos, 0}, "?");
        this->checkTypeAsExpr(*varNode);
        addReturnNodeToLast(node.getBlockNode(), this->typePool(), std::move(varNode));
      }
    }
  }
}

void TypeChecker::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
  this->checkTypeUserDefinedCmd(node, FuncCheckOp::REGISTER_NAME | FuncCheckOp::CHECK_BODY);
}

void TypeChecker::visitFuncListNode(FuncListNode &node) {
  // register names
  for (auto &e : node.getNodes()) {
    if (isa<FunctionNode>(*e)) {
      this->checkTypeFunction(cast<FunctionNode>(*e), FuncCheckOp::REGISTER_NAME);
    } else {
      assert(isa<UserDefinedCmdNode>(*e));
      this->checkTypeUserDefinedCmd(cast<UserDefinedCmdNode>(*e), FuncCheckOp::REGISTER_NAME);
    }
  }

  // check body
  for (auto &e : node.getNodes()) {
    if (isa<FunctionNode>(*e)) {
      this->checkTypeFunction(cast<FunctionNode>(*e), FuncCheckOp::CHECK_BODY);
    } else {
      assert(isa<UserDefinedCmdNode>(*e));
      this->checkTypeUserDefinedCmd(cast<UserDefinedCmdNode>(*e), FuncCheckOp::CHECK_BODY);
    }
  }
  node.setType(this->typePool().get(TYPE::Void));
}

void TypeChecker::visitCodeCompNode(CodeCompNode &node) {
  assert(this->compCtx);
  this->reachComp = true;
  node.setType(this->typePool().get(TYPE::Void));
  switch (node.getKind()) {
  case CodeCompNode::VAR:
  case CodeCompNode::VAR_IN_CMD_ARG:
  case CodeCompNode::VAR_OR_PARAM:
    this->compCtx->addVarNameRequest(this->lexer.get().toName(node.getTypingToken()),
                                     node.getKind() == CodeCompNode::VAR_IN_CMD_ARG,
                                     this->curScope);
    break;
  case CodeCompNode::MEMBER: {
    assert(node.getExprNode());
    auto &recvType = this->checkTypeAsExpr(*node.getExprNode());
    if (recvType.isRecordOrDerived() && !cast<RecordType>(recvType).isFinalized()) {
      break; // ignore non-finalized record type
    }
    this->compCtx->addMemberRequest(recvType, this->lexer.get().toTokenText(node.getTypingToken()));
    break;
  }
  case CodeCompNode::TYPE: {
    const Type *recvType = nullptr;
    if (node.getExprNode()) {
      recvType = &this->checkTypeExactly(*node.getExprNode());
    }
    this->compCtx->addTypeNameRequest(this->lexer.get().toName(node.getTypingToken()), recvType,
                                      this->curScope);
    break;
  }
  case CodeCompNode::CALL_SIGNATURE:
    break; // dummy
  case CodeCompNode::ATTR_PARAM: {
    assert(node.getExprNode());
    assert(isa<AttributeNode>(*node.getExprNode()));
    this->checkTypeExactly(*node.getExprNode());
    const auto &attrNode = cast<AttributeNode>(*node.getExprNode());

    AttributeParamSet targetParamSet;
    const AttributeParamSet resolved = attrNode.getResolvedParamSet();
    if (auto *attr = this->attributeMap.lookup(attrNode.getAttrName())) {
      for (auto &e : attr->getParams()) {
        if (resolved.has(e.second)) {
          continue; // skip already found attribute
        }
        targetParamSet.add(e.second);
      }
    }
    this->compCtx->addAttrParamRequest(this->lexer.get().toName(node.getTypingToken()),
                                       targetParamSet);
    break;
  }
  }
  this->reportError<Unreachable>(node);
}

void TypeChecker::visitErrorNode(ErrorNode &node) {
  node.setType(this->typePool().get(TYPE::Void));
}

void TypeChecker::visitEmptyNode(EmptyNode &node) {
  node.setType(this->typePool().get(TYPE::Void));
}

static bool isCmdLike(const Node &node) {
  switch (node.getNodeKind()) {
  case NodeKind::Cmd:
    return true;
  case NodeKind::Pipeline:
    return cast<PipelineNode>(node).getNodes().back()->is(NodeKind::Cmd);
  case NodeKind::PrefixAssign: {
    auto &prefixAssignNode = cast<PrefixAssignNode>(node);
    return prefixAssignNode.getExprNode() && isCmdLike(*prefixAssignNode.getExprNode());
  }
  case NodeKind::Time:
    return isCmdLike(cast<TimeNode>(node).getExprNode());
  default:
    return false;
  }
}

std::unique_ptr<Node> TypeChecker::operator()(const bool prevIsNothing,
                                              std::unique_ptr<Node> &&node, NameScopePtr global) {
  // reset state
  this->visitingDepth = 0;
  this->funcCtx->clear();
  this->errors.clear();
  this->reachComp = false;
  this->requiredTypes.clear();

  // set scope
  this->curScope = std::move(global);

  if (prevIsNothing) {
    this->reportError<Unreachable>(*node);
  }
  if (this->toplevelPrinting && this->curScope->inRootModule() && !isCmdLike(*node)) {
    this->checkTypeExactly(*node);
    node = TypeOpNode::newPrintOpNode(this->typePool(), std::move(node));
  } else {
    this->checkTypeWithCoercion(this->typePool().get(TYPE::Void), node); // pop stack top
  }
  return std::move(node);
}

} // namespace arsh
