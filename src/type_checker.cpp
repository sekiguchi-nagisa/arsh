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

#include "complete.h"
#include "constant.h"
#include "misc/glob.hpp"
#include "paths.h"
#include "type_checker.h"

namespace ydsh {

// #########################
// ##     TypeChecker     ##
// #########################

TypeOrError TypeChecker::toType(const TypeNode &node) {
  switch (node.typeKind) {
  case TypeNode::Base: {
    auto &typeNode = cast<BaseTypeNode>(node);

    // fist lookup type alias
    auto handle = this->curScope->lookup(toTypeAliasFullName(typeNode.getTokenText()));
    if (handle) {
      return Ok(&this->typePool.get(handle->getTypeId()));
    }
    return this->typePool.getType(typeNode.getTokenText());
  }
  case TypeNode::Qualified: {
    auto &qualifiedNode = cast<QualifiedTypeNode>(node);
    auto &recvType = this->checkTypeExactly(qualifiedNode.getRecvTypeNode());
    std::string typeName = toTypeAliasFullName(qualifiedNode.getNameTypeNode().getTokenText());
    auto ret = this->curScope->lookupField(this->typePool, recvType, typeName);
    if (ret) {
      auto &resolved = this->typePool.get(ret.asOk()->getTypeId());
      return Ok(&resolved);
    } else {
      auto &nameNode = qualifiedNode.getNameTypeNode();
      switch (ret.asErr()) {
      case NameLookupError::NOT_FOUND:
        this->reportError<UndefinedField>(nameNode, nameNode.getTokenText().c_str());
        break;
      case NameLookupError::MOD_PRIVATE:
        this->reportError<PrivateField>(nameNode, nameNode.getTokenText().c_str());
        break;
      }
      return Err(std::unique_ptr<TypeLookupError>());
    }
  }
  case TypeNode::Reified: {
    auto &typeNode = cast<ReifiedTypeNode>(node);
    unsigned int size = typeNode.getElementTypeNodes().size();
    auto tempOrError = this->typePool.getTypeTemplate(typeNode.getTemplate()->getTokenText());
    if (!tempOrError) {
      return Err(std::move(tempOrError).takeError());
    }
    auto typeTemplate = std::move(tempOrError).take();
    std::vector<const DSType *> elementTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      elementTypes[i] = &this->checkTypeExactly(*typeNode.getElementTypeNodes()[i]);
    }
    return this->typePool.createReifiedType(*typeTemplate, std::move(elementTypes));
  }
  case TypeNode::Func: {
    auto &typeNode = cast<FuncTypeNode>(node);
    auto &returnType = this->checkTypeExactly(typeNode.getReturnTypeNode());
    unsigned int size = typeNode.getParamTypeNodes().size();
    std::vector<const DSType *> paramTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      paramTypes[i] = &this->checkTypeExactly(*typeNode.getParamTypeNodes()[i]);
    }
    return this->typePool.createFuncType(returnType, std::move(paramTypes));
  }
  case TypeNode::Return: {
    auto &typeNode = cast<ReturnTypeNode>(node);
    unsigned int size = typeNode.getTypeNodes().size();
    if (size == 1) {
      return Ok(&this->checkTypeExactly(*typeNode.getTypeNodes()[0]));
    }

    std::vector<const DSType *> types(size);
    for (unsigned int i = 0; i < size; i++) {
      types[i] = &this->checkTypeExactly(*typeNode.getTypeNodes()[i]);
    }
    return this->typePool.createTupleType(std::move(types));
  }
  case TypeNode::TypeOf:
    auto &typeNode = cast<TypeOfNode>(node);
    auto &type = this->checkTypeAsSomeExpr(typeNode.getExprNode());
    return Ok(&type);
  }
  return Ok(static_cast<DSType *>(nullptr)); // for suppressing gcc warning (normally unreachable).
}

const DSType &TypeChecker::checkType(const DSType *requiredType, Node &targetNode,
                                     const DSType *unacceptableType, CoercionKind &kind) {
  /**
   * if target node is expr node and type is null,
   * try type check.
   */
  if (targetNode.isUntyped()) {
    this->visitingDepth++;
    targetNode.setType(this->typePool.getUnresolvedType());
    targetNode.accept(*this);
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
      this->reportError<Unacceptable>(targetNode, type.getName());
      targetNode.setType(this->typePool.getUnresolvedType());
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
  if (kind == CoercionKind::INVALID_COERCION && this->checkCoercion(*requiredType, type)) {
    kind = CoercionKind::PERFORM_COERCION;
    return type;
  }

  this->reportError<Required>(targetNode, requiredType->getName(), type.getName());
  targetNode.setType(this->typePool.getUnresolvedType());
  return targetNode.getType();
}

const DSType &TypeChecker::checkTypeAsSomeExpr(Node &targetNode) {
  auto &type = this->checkTypeAsExpr(targetNode);
  if (type.isNothingType()) {
    this->reportError<Unacceptable>(targetNode, type.getName());
    targetNode.setType(this->typePool.getUnresolvedType());
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
    auto *node = blockNode.getNodes()[index].get();
    if (isa<DeferNode>(node)) {
      auto *curDefer = cast<DeferNode>(node);
      unsigned int dropSize = curDefer->getDropLocalOffset() - lastDefer->getDropLocalOffset();
      lastDefer->setDropLocalSize(dropSize);
      lastDefer = curDefer;
    }
  }
  unsigned int localLimit = blockNode.getBaseIndex() + blockNode.getVarSize();
  unsigned int dropSize = localLimit - lastDefer->getDropLocalOffset();
  lastDefer->setDropLocalSize(dropSize);
}

void TypeChecker::checkTypeWithCurrentScope(const DSType *requiredType, BlockNode &blockNode) {
  auto *blockType = &this->typePool.get(TYPE::Void);
  FuncContext::IntoTry intoTry;
  for (auto iter = blockNode.refNodes().begin(); iter != blockNode.refNodes().end(); ++iter) {
    auto &targetNode = *iter;
    if (blockType->isNothingType()) {
      this->reportError<Unreachable>(*targetNode);
    }

    // type check
    if (iter == blockNode.refNodes().end() - 1) {
      if (requiredType != nullptr) {
        this->checkTypeWithCoercion(*requiredType, targetNode);
      } else {
        this->checkTypeExactly(*targetNode);
      }
    } else {
      this->checkTypeWithCoercion(this->typePool.get(TYPE::Void), targetNode);
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

void TypeChecker::checkTypeWithCoercion(const DSType &requiredType,
                                        std::unique_ptr<Node> &targetNode) {
  CoercionKind kind = CoercionKind::INVALID_COERCION;
  this->checkType(&requiredType, *targetNode, nullptr, kind);
  if (kind != CoercionKind::INVALID_COERCION && kind != CoercionKind::NOP) {
    this->resolveCoercion(requiredType, targetNode);
  }
}

bool TypeChecker::checkCoercion(const DSType &requiredType, const DSType &targetType) {
  if (requiredType.isVoidType()) { // pop stack top
    return true;
  }

  if (requiredType.is(TYPE::Boolean)) {
    if (targetType.isOptionType()) {
      return true;
    }
    auto *handle = this->typePool.lookupMethod(targetType, OP_BOOL);
    if (handle != nullptr) {
      return true;
    }
  }
  return false;
}

const DSType &TypeChecker::resolveCoercionOfJumpValue(const FlexBuffer<JumpNode *> &jumpNodes,
                                                      bool optional) {
  if (jumpNodes.empty()) {
    return this->typePool.get(TYPE::Void);
  }

  std::vector<const DSType *> types(jumpNodes.size());
  for (unsigned int i = 0; i < jumpNodes.size(); i++) {
    types[i] = &jumpNodes[i]->getExprNode().getType();
  }
  auto &retType =
      this->resolveCommonSuperType(jumpNodes[0]->getExprNode(), std::move(types), nullptr);
  for (auto &jumpNode : jumpNodes) {
    this->checkTypeWithCoercion(retType, jumpNode->refExprNode());
  }

  if (optional) {
    if (auto ret = this->typePool.createOptionType(retType); ret) {
      return *std::move(ret).take();
    } else {
      return this->typePool.getUnresolvedType();
    }
  } else {
    return retType;
  }
}

HandlePtr TypeChecker::addEntry(Token token, const std::string &symbolName, const DSType &type,
                                HandleAttr attribute) {
  auto ret = this->curScope->defineHandle(std::string(symbolName), type, attribute);
  if (!ret) {
    switch (ret.asErr()) {
    case NameRegisterError::DEFINED:
      this->reportError<DefinedSymbol>(token, symbolName.c_str());
      break;
    case NameRegisterError::LIMIT:
      this->reportError<LocalLimit>(token);
      break;
    case NameRegisterError::INVALID_TYPE:
      break; // normally unreachable
    }
    return nullptr;
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

const Handle *TypeChecker::addUdcEntry(const UserDefinedCmdNode &node) {
  static auto deniedList = initDeniedNameList();
  if (deniedList.find(node.getCmdName()) != deniedList.end()) {
    this->reportError<DefinedCmd>(node, node.getCmdName().c_str()); // FIXME: better error message
    return nullptr;
  }

  const DSType *returnType = nullptr;
  if (!node.getReturnTypeNode()) {
    returnType = &this->typePool.get(TYPE::Int);
  } else if (node.getReturnTypeNode()->getType().isNothingType()) {
    returnType = &this->typePool.get(TYPE::Nothing);
  }
  auto *type = &this->typePool.getUnresolvedType();
  if (returnType) {
    auto ret = this->typePool.createFuncType(*returnType, {&this->typePool.get(TYPE::StringArray)});
    assert(ret);
    type = ret.asOk();
  }

  auto ret =
      this->curScope->defineHandle(toCmdFullName(node.getCmdName()), *type, HandleAttr::READ_ONLY);
  if (ret) {
    return ret.asOk().get();
  } else if (ret.asErr() == NameRegisterError::DEFINED) {
    this->reportError<DefinedCmd>(node, node.getCmdName().c_str());
  }
  return nullptr;
}

void TypeChecker::reportErrorImpl(Token token, const char *kind, const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    abort();
  }
  va_end(arg);

  this->errors.emplace_back(token, kind, CStrPtr(str));
}

// for ApplyNode type checking
/**
 * lookup resolve function/method like the following step
 * 1. if node.isMethodCall()
 * 1-1. check type receiver, and lookup method. if found, return MethodHandle
 * 1-2. if method is not found, lookup field.
 * 1-2-1. if field is found and is callable, return function type
 * 1-2-2. if field is not callable, report NotCallable error
 * 1-3. otherwise report UndefinedMethod error
 *
 * 2. if ! node.isMethodCall()
 * 2-1. check type expression and check if expr is callable, return function type
 * 2-2. otherwise, report NotCallable error
 */
CallableTypes TypeChecker::resolveCallee(ApplyNode &node) {
  auto &exprNode = node.getExprNode();
  if (node.isMethodCall()) {
    assert(isa<AccessNode>(exprNode));
    auto &accessNode = cast<AccessNode>(exprNode);

    // first lookup method
    auto &recvType = this->checkTypeAsExpr(accessNode.getRecvNode());
    if (auto *handle =
            this->curScope->lookupMethod(this->typePool, recvType, accessNode.getFieldName())) {
      accessNode.setType(this->typePool.get(TYPE::Any));
      node.setKind(ApplyNode::METHOD_CALL);
      node.setHandle(handle);
      return handle->toCallableTypes();
    }

    // if method is not found, resolve field
    if (!this->checkAccessNode(accessNode)) {
      node.setType(this->typePool.getUnresolvedType());
      this->reportError<UndefinedMethod>(accessNode.getNameNode(),
                                         accessNode.getFieldName().c_str());
    }
  }

  // otherwose, resolve function type
  CallableTypes callableTypes(this->typePool.getUnresolvedType());
  auto &type = this->checkType(this->typePool.get(TYPE::Func), exprNode);
  if (type.isFuncType()) {
    node.setKind(ApplyNode::FUNC_CALL);
    callableTypes = cast<FunctionType>(type).toCallableTypes();
  } else {
    this->reportError<NotCallable>(exprNode);
  }
  return callableTypes;
}

bool TypeChecker::checkAccessNode(AccessNode &node) {
  auto &recvType = this->checkTypeAsExpr(node.getRecvNode());
  auto ret = this->curScope->lookupField(this->typePool, recvType, node.getFieldName());
  if (ret) {
    auto handle = ret.asOk();
    node.setHandle(handle);
    node.setType(this->typePool.get(handle->getTypeId()));
    return true;
  } else {
    switch (ret.asErr()) {
    case NameLookupError::NOT_FOUND:
      break;
    case NameLookupError::MOD_PRIVATE:
      this->reportError<PrivateField>(node.getNameNode(), node.getFieldName().c_str());
      break;
    }
    return false;
  }
}

void TypeChecker::checkArgsNode(const CallableTypes &types, ArgsNode &node) {
  unsigned int argSize = node.getNodes().size();
  unsigned int paramSize = types.paramSize;
  if (argSize != paramSize) {
    this->reportError<UnmatchParam>(node, paramSize, argSize);
  }
  unsigned int maxSize = std::max(argSize, paramSize);
  for (unsigned int i = 0; i < maxSize; i++) {
    if (i < argSize && i < paramSize) {
      this->checkTypeWithCoercion(*types.paramTypes[i], node.refNodes()[i]);
    } else if (i < argSize) {
      this->checkTypeAsExpr(*node.getNodes()[i]);
    }
  }
  this->checkTypeExactly(node);
}

void TypeChecker::resolveCastOp(TypeOpNode &node) {
  auto &exprType = node.getExprNode().getType();
  auto &targetType = node.getType();

  if (node.getType().isVoidType()) {
    node.setOpKind(TypeOpNode::TO_VOID);
    return;
  }

  /**
   * nop
   */
  if (targetType.isSameOrBaseTypeOf(exprType)) {
    return;
  }

  /**
   * number cast
   */
  int beforeIndex = exprType.getNumTypeIndex();
  int afterIndex = targetType.getNumTypeIndex();
  if (beforeIndex > -1 && afterIndex > -1) {
    assert(beforeIndex < 8 && afterIndex < 8);
    node.setOpKind(TypeOpNode::NUM_CAST);
    return;
  }

  if (exprType.isOptionType()) {
    if (targetType.is(TYPE::Boolean)) {
      node.setOpKind(TypeOpNode::CHECK_UNWRAP);
      return;
    }
  } else {
    if (targetType.is(TYPE::String)) {
      node.setOpKind(TypeOpNode::TO_STRING);
      return;
    }
    if (targetType.is(TYPE::Boolean) && this->typePool.lookupMethod(exprType, OP_BOOL) != nullptr) {
      node.setOpKind(TypeOpNode::TO_BOOL);
      return;
    }
    if (!targetType.isNothingType() && exprType.isSameOrBaseTypeOf(targetType)) {
      node.setOpKind(TypeOpNode::CHECK_CAST);
      return;
    }
  }
  this->reportError<CastOp>(node, exprType.getName(), targetType.getName());
}

std::unique_ptr<Node> TypeChecker::newPrintOpNode(std::unique_ptr<Node> &&node) {
  if (!node->getType().isVoidType() && !node->getType().isNothingType()) {
    auto castNode = newTypedCastNode(std::move(node), this->typePool.get(TYPE::Void));
    castNode->setOpKind(TypeOpNode::PRINT);
    node = std::move(castNode);
  }
  return std::move(node);
}

// visitor api
void TypeChecker::visitTypeNode(TypeNode &node) {
  auto ret = this->toType(node);
  if (ret) {
    node.setType(*std::move(ret).take());
  } else {
    if (ret.asErr()) {
      this->reportError(node.getToken(), std::move(*ret.asErr()));
    }
  }
}

void TypeChecker::visitNumberNode(NumberNode &node) {
  switch (node.kind) {
  case NumberNode::Int:
    node.setType(this->typePool.get(TYPE::Int));
    break;
  case NumberNode::Float:
    node.setType(this->typePool.get(TYPE::Float));
    break;
  case NumberNode::Signal:
    node.setType(this->typePool.get(TYPE::Signal));
    break;
  }
}

void TypeChecker::visitStringNode(StringNode &node) {
  node.setType(this->typePool.get(TYPE::String));
}

void TypeChecker::visitStringExprNode(StringExprNode &node) {
  for (auto &exprNode : node.getExprNodes()) {
    this->checkTypeAsExpr(*exprNode);
  }
  node.setType(this->typePool.get(TYPE::String));
}

void TypeChecker::visitRegexNode(RegexNode &node) {
  std::string e;
  if (!node.buildRegex(e)) {
    this->reportError<RegexSyntax>(node, e.c_str());
  }
  node.setType(this->typePool.get(TYPE::Regex));
}

void TypeChecker::visitArrayNode(ArrayNode &node) {
  unsigned int size = node.getExprNodes().size();
  assert(size != 0);
  auto &firstElementNode = node.getExprNodes()[0];
  auto &elementType = this->checkTypeAsSomeExpr(*firstElementNode);

  for (unsigned int i = 1; i < size; i++) {
    this->checkTypeWithCoercion(elementType, node.refExprNodes()[i]);
  }

  if (auto typeOrError = this->typePool.createArrayType(elementType)) {
    node.setType(*std::move(typeOrError).take());
  }
}

void TypeChecker::visitMapNode(MapNode &node) {
  unsigned int size = node.getValueNodes().size();
  assert(size != 0);
  auto &firstKeyNode = node.getKeyNodes()[0];
  this->checkTypeAsSomeExpr(*firstKeyNode);
  auto &keyType = this->checkType(this->typePool.get(TYPE::_Value), *firstKeyNode);
  auto &firstValueNode = node.getValueNodes()[0];
  auto &valueType = this->checkTypeAsSomeExpr(*firstValueNode);

  for (unsigned int i = 1; i < size; i++) {
    this->checkTypeWithCoercion(keyType, node.refKeyNodes()[i]);
    this->checkTypeWithCoercion(valueType, node.refValueNodes()[i]);
  }

  if (auto typeOrError = this->typePool.createMapType(keyType, valueType)) {
    node.setType(*std::move(typeOrError).take());
  }
}

void TypeChecker::visitTupleNode(TupleNode &node) {
  unsigned int size = node.getNodes().size();
  std::vector<const DSType *> types(size);
  for (unsigned int i = 0; i < size; i++) {
    types[i] = &this->checkTypeAsSomeExpr(*node.getNodes()[i]);
  }
  auto typeOrError = this->typePool.createTupleType(std::move(types));
  if (typeOrError) {
    node.setType(*std::move(typeOrError).take());
  } else {
    this->reportError(node.getToken(), std::move(*typeOrError.asErr()));
  }
}

void TypeChecker::visitVarNode(VarNode &node) {
  if (auto handle = this->curScope->lookup(node.getVarName()); handle) {
    node.setHandle(handle);
    node.setType(this->typePool.get(handle->getTypeId()));
  } else {
    this->reportError<UndefinedSymbol>(node, node.getVarName().c_str());
  }
}

void TypeChecker::visitAccessNode(AccessNode &node) {
  if (!this->checkAccessNode(node)) {
    this->reportError<UndefinedField>(node.getNameNode(), node.getFieldName().c_str());
  }
}

void TypeChecker::visitTypeOpNode(TypeOpNode &node) {
  auto &exprType = this->checkTypeAsExpr(node.getExprNode());
  auto &targetType = this->checkTypeExactly(*node.getTargetTypeNode());

  if (node.isCastOp()) {
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
    node.setType(this->typePool.get(TYPE::Boolean));
  }
}

void TypeChecker::visitUnaryOpNode(UnaryOpNode &node) {
  auto &exprType = this->checkTypeAsExpr(*node.getExprNode());
  if (node.isUnwrapOp()) {
    if (exprType.isOptionType()) {
      node.setType(cast<OptionType>(exprType).getElementType());
    } else {
      this->reportError<Required>(*node.getExprNode(), "Option type", exprType.getName());
    }
  } else {
    if (exprType.isOptionType()) {
      this->resolveCoercion(this->typePool.get(TYPE::Boolean), node.refExprNode());
    }
    auto &applyNode = node.createApplyNode();
    node.setType(this->checkTypeAsExpr(applyNode));
  }
}

void TypeChecker::visitBinaryOpNode(BinaryOpNode &node) {
  if (node.getOp() == TokenKind::COND_AND || node.getOp() == TokenKind::COND_OR) {
    auto &booleanType = this->typePool.get(TYPE::Boolean);
    this->checkTypeWithCoercion(booleanType, node.refLeftNode());
    if (node.getLeftNode()->getType().isNothingType()) {
      this->reportError<Unreachable>(*node.getRightNode());
    }

    this->checkTypeWithCoercion(booleanType, node.refRightNode());
    node.setType(booleanType);
    return;
  }

  if (node.getOp() == TokenKind::STR_CHECK) {
    this->checkType(this->typePool.get(TYPE::String), *node.getLeftNode());
    this->checkType(this->typePool.get(TYPE::String), *node.getRightNode());
    node.setType(this->typePool.get(TYPE::String));
    return;
  }

  if (node.getOp() == TokenKind::NULL_COALE) {
    auto &leftType = this->checkTypeAsExpr(*node.getLeftNode());
    if (leftType.isOptionType()) {
      auto &elementType = cast<OptionType>(leftType).getElementType();
      this->checkTypeWithCoercion(elementType, node.refRightNode());
      node.setType(elementType);
    } else {
      this->reportError<Required>(*node.getLeftNode(), "Option type", leftType.getName());
    }
    return;
  }

  auto &leftType = this->checkTypeAsExpr(*node.getLeftNode());
  auto &rightType = this->checkTypeAsExpr(*node.getRightNode());

  // check referential equality of func object
  if (leftType.isFuncType() && leftType == rightType &&
      (node.getOp() == TokenKind::EQ || node.getOp() == TokenKind::NE)) {
    node.setType(this->typePool.get(TYPE::Boolean));
    return;
  }

  // string concatenation
  if (node.getOp() == TokenKind::ADD && (leftType.is(TYPE::String) || rightType.is(TYPE::String))) {
    if (!leftType.is(TYPE::String)) {
      this->resolveCoercion(this->typePool.get(TYPE::String), node.refLeftNode());
    }
    if (!rightType.is(TYPE::String)) {
      this->resolveCoercion(this->typePool.get(TYPE::String), node.refRightNode());
    }
    node.setType(this->typePool.get(TYPE::String));
    return;
  }

  node.createApplyNode();
  node.setType(this->checkTypeAsExpr(*node.getOptNode()));
}

void TypeChecker::visitArgsNode(ArgsNode &node) { node.setType(this->typePool.get(TYPE::Void)); }

void TypeChecker::visitApplyNode(ApplyNode &node) {
  CallableTypes callableTypes = this->resolveCallee(node);
  this->checkArgsNode(callableTypes, node.getArgsNode());
  node.setType(*callableTypes.returnType);
  if (node.getType().isNothingType() &&
      this->funcCtx->finallyLevel() > this->funcCtx->childLevel()) {
    this->reportError<InsideFinally>(node);
  }
}

void TypeChecker::visitNewNode(NewNode &node) {
  auto &type = this->checkTypeAsExpr(node.getTargetTypeNode());
  CallableTypes callableTypes(this->typePool.getUnresolvedType());
  if (!type.isOptionType() && !type.isArrayType() && !type.isMapType()) {
    if (auto *handle = this->curScope->lookupConstructor(this->typePool, type)) {
      callableTypes = handle->toCallableTypes();
      node.setHandle(handle);
    } else {
      this->reportError<UndefinedInit>(node.getTargetTypeNode(), type.getName());
    }
  }
  this->checkArgsNode(callableTypes, node.getArgsNode());
  node.setType(type);
}

void TypeChecker::visitEmbedNode(EmbedNode &node) {
  auto &exprType = this->checkTypeAsExpr(node.getExprNode());
  if (exprType.isOptionType()) {
    this->reportError<Unacceptable>(node.getExprNode(), exprType.getName());
    node.setType(this->typePool.getUnresolvedType());
    return;
  }

  node.setType(exprType);
  if (node.getKind() == EmbedNode::STR_EXPR) {
    auto &type = this->typePool.get(TYPE::String);
    if (!type.isSameOrBaseTypeOf(exprType)) { // call __INTERP__()
      std::string methodName(OP_INTERP);
      auto *handle =
          exprType.isOptionType() ? nullptr : this->typePool.lookupMethod(exprType, methodName);
      if (handle) {
        assert(handle->getReturnType() == type);
        node.setHandle(handle);
      } else { // if exprType is optional
        this->reportError<UndefinedMethod>(node.getExprNode(), methodName.c_str());
        node.setType(this->typePool.getUnresolvedType());
      }
    }
  } else {
    if (exprType.is(TYPE::Any)) {
      this->reportError<Unacceptable>(node.getExprNode(), exprType.getName());
      node.setType(this->typePool.getUnresolvedType());
    } else if (!this->typePool.get(TYPE::String).isSameOrBaseTypeOf(exprType) &&
               !this->typePool.get(TYPE::StringArray).isSameOrBaseTypeOf(exprType) &&
               !this->typePool.get(TYPE::UnixFD)
                    .isSameOrBaseTypeOf(exprType)) { // call __STR__ or __CMD__ARG
      if (exprType.isArrayType() || exprType.isMapType() || exprType.isTupleType() ||
          exprType.isRecordType()) {
        node.setType(this->typePool.get(TYPE::StringArray));
      } else if (auto *handle = this->typePool.lookupMethod(exprType, OP_STR)) {
        node.setHandle(handle);
        node.setType(handle->getReturnType());
      } else {
        this->reportError<UndefinedMethod>(node.getExprNode(), OP_STR);
        node.setType(this->typePool.getUnresolvedType());
      }
    }
  }
}

void TypeChecker::visitCmdNode(CmdNode &node) {
  this->checkType(this->typePool.get(TYPE::String), node.getNameNode());
  for (auto &argNode : node.getArgNodes()) {
    this->checkTypeAsExpr(*argNode);
  }
  if (node.getNameNode().getValue() == "exit" || node.getNameNode().getValue() == "_exit") {
    node.setType(this->typePool.get(TYPE::Nothing));
  } else {
    node.setType(this->typePool.get(TYPE::Boolean));
    std::string cmdName = toCmdFullName(node.getNameNode().getValue());
    if (auto handle = this->curScope->lookup(cmdName)) {
      node.setUdcIndex(handle->getIndex());
      auto &type = this->typePool.get(handle->getTypeId());
      if (type.isFuncType()) { // resolved command may be module object
        auto &returnType = cast<FunctionType>(type).getReturnType();
        assert(returnType.is(TYPE::Int) || returnType.isNothingType());
        if (returnType.isNothingType()) {
          node.setType(returnType);
        }
      }
    }
  }
  if (node.getType().isNothingType() &&
      this->funcCtx->finallyLevel() > this->funcCtx->childLevel()) {
    this->reportError<InsideFinally>(node);
  }
}

void TypeChecker::visitCmdArgNode(CmdArgNode &node) {
  for (auto &exprNode : node.getSegmentNodes()) {
    this->checkTypeAsExpr(*exprNode);
    assert(exprNode->getType().is(TYPE::String) || exprNode->getType().is(TYPE::StringArray) ||
           exprNode->getType().is(TYPE::UnixFD) || exprNode->getType().isNothingType() ||
           exprNode->getType().isUnresolved());
  }

  if (node.getGlobPathSize() > SYS_LIMIT_GLOB_FRAG_NUM) {
    this->reportError<GlobLimit>(node);
  }

  // not allow String Array and UnixFD type
  if (node.getSegmentNodes().size() > 1) {
    for (auto &exprNode : node.getSegmentNodes()) {
      this->checkType(nullptr, *exprNode, &this->typePool.get(TYPE::StringArray));
      this->checkType(nullptr, *exprNode, &this->typePool.get(TYPE::UnixFD));
    }
  }
  assert(!node.getSegmentNodes().empty());
  node.setType(node.getGlobPathSize() > 0 ? this->typePool.get(TYPE::StringArray)
                                          : node.getSegmentNodes()[0]->getType());
}

void TypeChecker::visitArgArrayNode(ArgArrayNode &node) {
  for (auto &argNode : node.getCmdArgNodes()) {
    this->checkTypeAsExpr(*argNode);
  }
  node.setType(this->typePool.get((TYPE::StringArray)));
}

void TypeChecker::visitRedirNode(RedirNode &node) {
  auto &argNode = node.getTargetNode();
  this->checkTypeAsExpr(argNode);

  // not allow String Array type
  this->checkType(nullptr, argNode, &this->typePool.get(TYPE::StringArray));

  // not UnixFD type, if IOHere
  if (node.isHereStr()) {
    this->checkType(nullptr, argNode, &this->typePool.get(TYPE::UnixFD));
  }
  node.setType(this->typePool.get(TYPE::Any)); // FIXME:
}

void TypeChecker::visitWildCardNode(WildCardNode &node) {
  node.setType(this->typePool.get(TYPE::String));
}

void TypeChecker::visitPipelineNode(PipelineNode &node) {
  unsigned int size = node.getNodes().size();
  if (size + (node.isLastPipe() ? 0 : 1) > SYS_LIMIT_PIPE_LEN) {
    this->reportError<PipeLimit>(node);
  }

  {
    auto child = this->funcCtx->intoChild();
    for (unsigned int i = 0; i < size - 1; i++) {
      this->checkTypeExactly(*node.getNodes()[i]);
    }
  }

  if (node.isLastPipe()) {
    auto scope = this->intoBlock();
    this->addEntry(node, "%%pipe", this->typePool.get(TYPE::Any), HandleAttr::READ_ONLY);
    node.setBaseIndex(this->curScope->getBaseIndex());
    auto &type = this->checkTypeExactly(*node.getNodes()[size - 1]);
    node.setType(type);
  } else {
    auto child = this->funcCtx->intoChild();
    this->checkTypeExactly(*node.getNodes()[size - 1]);
    node.setType(this->typePool.get(TYPE::Boolean));
  }
}

void TypeChecker::visitWithNode(WithNode &node) {
  auto scope = this->intoBlock();

  // register redir config
  this->addEntry(node, "%%redir", this->typePool.get(TYPE::Any), HandleAttr::READ_ONLY);

  auto &type = this->checkTypeExactly(node.getExprNode());
  for (auto &e : node.getRedirNodes()) {
    this->checkTypeAsExpr(*e);
  }

  node.setBaseIndex(this->curScope->getBaseIndex());
  node.setType(type);
}

void TypeChecker::visitForkNode(ForkNode &node) {
  auto child = this->funcCtx->intoChild();
  this->checkType(nullptr, node.getExprNode(),
                  node.isJob() ? &this->typePool.get(TYPE::Job) : nullptr);

  const DSType *type = nullptr;
  switch (node.getOpKind()) {
  case ForkKind::STR:
    type = &this->typePool.get(TYPE::String);
    break;
  case ForkKind::ARRAY:
    type = &this->typePool.get(TYPE::StringArray);
    break;
  case ForkKind::IN_PIPE:
  case ForkKind::OUT_PIPE:
    type = &this->typePool.get(TYPE::UnixFD);
    break;
  case ForkKind::JOB:
  case ForkKind::COPROC:
  case ForkKind::DISOWN:
  case ForkKind::NONE:
    type = &this->typePool.get(TYPE::Job);
    break;
  }
  node.setType(*type);
}

void TypeChecker::visitAssertNode(AssertNode &node) {
  this->checkTypeWithCoercion(this->typePool.get(TYPE::Boolean), node.refCondNode());
  this->checkType(this->typePool.get(TYPE::String), node.getMessageNode());
  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitBlockNode(BlockNode &node) {
  if (this->isTopLevel() && node.getNodes().empty()) {
    this->reportError<UselessBlock>(node);
  }
  auto scope = this->intoBlock();
  this->checkTypeWithCurrentScope(nullptr, node);
}

void TypeChecker::visitTypeDefNode(TypeDefNode &node) {
  switch (node.getDefKind()) {
  case TypeDefNode::ALIAS: {
    TypeNode &typeToken = node.getTargetTypeNode();
    auto &type = this->checkTypeExactly(typeToken);
    auto ret = this->curScope->defineTypeAlias(this->typePool, node.getName(), type);
    if (!ret) {
      this->reportError<DefinedTypeAlias>(node.getNameInfo().getToken(), node.getName().c_str());
    }
    break;
  }
  case TypeDefNode::ERROR_DEF: {
    if (!this->isTopLevel()) { // only available toplevel scope
      this->reportError<OutsideToplevel>(node);
      break;
    }
    auto &errorType = this->checkType(this->typePool.get(TYPE::Error), node.getTargetTypeNode());
    auto typeOrError =
        this->typePool.createErrorType(node.getName(), errorType, this->curScope->modId);
    if (typeOrError) {
      auto ret =
          this->curScope->defineTypeAlias(this->typePool, node.getName(), *typeOrError.asOk());
      if (!ret) {
        this->reportError<DefinedTypeAlias>(node.getNameInfo().getToken(), node.getName().c_str());
      }
    } else {
      this->reportError(node.getNameInfo().getToken(), std::move(*typeOrError.asErr()));
    }
    break;
  }
  }
  node.setType(this->typePool.get(TYPE::Void));
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
  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitLoopNode(LoopNode &node) {
  {
    auto scope = this->intoBlock();
    this->checkTypeWithCoercion(this->typePool.get(TYPE::Void), node.refInitNode());

    if (node.getCondNode() != nullptr) {
      this->checkTypeWithCoercion(this->typePool.get(TYPE::Boolean), node.refCondNode());
    }
    this->checkTypeWithCoercion(this->typePool.get(TYPE::Void), node.refIterNode());

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
    unsigned int baseIndex = blockNode.getBaseIndex();
    unsigned int varSize = blockNode.getVarSize();
    blockNode.setBaseIndex(baseIndex + 1);
    blockNode.setVarSize(varSize - 1);
  }

  if (!node.getBlockNode().getType().isNothingType()) { // insert continue to block end
    auto jumpNode = JumpNode::newContinue({0, 0});
    jumpNode->setType(this->typePool.get(TYPE::Nothing));
    jumpNode->getExprNode().setType(this->typePool.get(TYPE::Void));
    node.getBlockNode().setType(jumpNode->getType());
    node.getBlockNode().addNode(std::move(jumpNode));
  }
}

void TypeChecker::visitIfNode(IfNode &node) {
  this->checkTypeWithCoercion(this->typePool.get(TYPE::Boolean), node.refCondNode());
  auto &thenType = this->checkTypeExactly(node.getThenNode());
  auto &elseType = this->checkTypeExactly(node.getElseNode());

  if (thenType.isNothingType() && elseType.isNothingType()) {
    node.setType(thenType);
  } else if (thenType.isSameOrBaseTypeOf(elseType)) {
    node.setType(thenType);
  } else if (elseType.isSameOrBaseTypeOf(thenType)) {
    node.setType(elseType);
  } else if (this->checkCoercion(thenType, elseType)) {
    this->checkTypeWithCoercion(thenType, node.refElseNode());
    node.setType(thenType);
  } else if (this->checkCoercion(elseType, thenType)) {
    this->checkTypeWithCoercion(elseType, node.refThenNode());
    node.setType(elseType);
  } else {
    this->checkTypeWithCoercion(this->typePool.get(TYPE::Void), node.refThenNode());
    this->checkTypeWithCoercion(this->typePool.get(TYPE::Void), node.refElseNode());
    node.setType(this->typePool.get(TYPE::Void));
  }
}

bool TypeChecker::IntPatternMap::collect(const Node &constNode) {
  if (constNode.getNodeKind() != NodeKind::Number) {
    return false;
  }
  int64_t value = cast<const NumberNode>(constNode).getIntValue();
  auto pair = this->set.insert(value);
  return pair.second;
}

bool TypeChecker::StrPatternMap::collect(const Node &constNode) {
  if (constNode.getNodeKind() != NodeKind::String) {
    return false;
  }
  const char *str = cast<const StringNode>(constNode).getValue().c_str();
  auto pair = this->set.insert(str);
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
  unsigned int size = node.getArmNodes().size();
  std::vector<const DSType *> types(size);
  for (unsigned int i = 0; i < size; i++) {
    types[i] = &this->checkTypeExactly(*node.getArmNodes()[i]);
  }
  auto &type =
      this->resolveCommonSuperType(node, std::move(types), &this->typePool.get(TYPE::Void));

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
      auto elseNode = std::make_unique<StringNode>(token, "else");
      this->reportError<DupPattern>(*elseNode);
    }
    collector.setElsePattern(true);
  }

  // check pattern type
  for (auto &e : node.getPatternNodes()) {
    auto *type = &this->checkTypeAsExpr(*e);
    if (type->is(TYPE::Regex)) {
      collector.setKind(CaseNode::IF_ELSE);
      type = &this->typePool.get(TYPE::String);
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

const DSType &TypeChecker::resolveCommonSuperType(const Node &node,
                                                  std::vector<const DSType *> &&types,
                                                  const DSType *fallbackType) {
  std::sort(types.begin(), types.end(), [](const DSType *x, const DSType *y) {
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
    return *types[0];
  }

  if (fallbackType) {
    return *fallbackType;
  } else {
    std::string value;
    for (auto &t : types) {
      if (!value.empty()) {
        value += ", ";
      }
      value += t->getNameRef();
    }
    this->reportError<NoCommonSuper>(node, value.c_str());
    return this->typePool.getUnresolvedType();
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
  switch (node.getNodeKind()) {
  case NodeKind::Number:
    return cast<NumberNode>(node).clone();
  case NodeKind::String: {
    auto &strNode = cast<StringNode>(node);
    auto constNode = std::make_unique<StringNode>(
        strNode.getToken(), std::string(strNode.getValue()), strNode.getKind());
    constNode->setType(strNode.getType());
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
  case NodeKind::WildCard: {
    auto &wildCardNode = cast<WildCardNode>(node);
    auto constNode = std::make_unique<WildCardNode>(wildCardNode.getToken(), wildCardNode.meta);
    constNode->setType(wildCardNode.getType());
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
      auto constNode = TRY(evalConstant(recvNode));
      TRY(isa<NumberNode>(*constNode));
      int64_t value = cast<NumberNode>(*constNode).getIntValue();
      if (op == TokenKind::MINUS) {
        value = -value;
      } else if (op == TokenKind::NOT) {
        uint64_t v = ~static_cast<uint64_t>(value);
        value = static_cast<int64_t>(v);
      }
      constNode = NumberNode::newInt(token, value);
      constNode->setType(this->typePool.get(TYPE::Int));
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
    constNode->setType(this->typePool.get(TYPE::String));
    return constNode;
  }
  case NodeKind::Embed: {
    auto &embedNode = cast<EmbedNode>(node);
    if (!embedNode.getHandle()) {
      return this->evalConstant(embedNode.getExprNode());
    }
    break;
  }
  case NodeKind::Var: {
    assert(this->lexer);
    auto &varNode = cast<VarNode>(node);
    Token token = varNode.getToken();
    std::string value;
    if (!varNode.getHandle()) {
      break;
    }
    if (hasFlag(varNode.attr(), HandleAttr::MOD_CONST)) {
      if (varNode.getVarName() == CVAR_SCRIPT_NAME) {
        value = this->lexer->getSourceName();
      } else if (varNode.getVarName() == CVAR_SCRIPT_DIR) {
        value = this->lexer->getScriptDir();
      } else {
        break;
      }
    } else if (hasFlag(varNode.attr(), HandleAttr::GLOBAL)) {
      auto *ptr = this->config.lookup(varNode.getVarName());
      if (!ptr) {
        break;
      }
      value = *ptr;
    } else {
      break;
    }
    TRY(varNode.getType().is(TYPE::String));
    auto constNode = std::make_unique<StringNode>(token, std::move(value));
    constNode->setType(this->typePool.get(TYPE::String));
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
    this->reportError<InsideLoop>(node);
    return;
  }

  if (this->funcCtx->finallyLevel() > this->funcCtx->loopLevel()) {
    this->reportError<InsideFinally>(node);
  }

  if (this->funcCtx->childLevel() > this->funcCtx->loopLevel()) {
    this->reportError<InsideChild>(node);
  }

  if (this->funcCtx->tryCatchLevel() > this->funcCtx->loopLevel()) {
    node.setTryDepth(this->funcCtx->tryCatchLevel() - this->funcCtx->loopLevel());
  }

  if (node.getExprNode().is(NodeKind::Empty)) {
    this->checkType(this->typePool.get(TYPE::Void), node.getExprNode());
  } else if (node.getOpKind() == JumpNode::BREAK) {
    this->checkTypeAsSomeExpr(node.getExprNode());
    this->funcCtx->addJumpNode(&node);
  }
  assert(!node.getExprNode().isUntyped());
}

void TypeChecker::checkTypeAsReturn(JumpNode &node) {
  if (this->funcCtx->finallyLevel() > 0) {
    this->reportError<InsideFinally>(node);
  }

  if (this->funcCtx->childLevel() > 0) {
    this->reportError<InsideChild>(node);
  }

  if (!this->funcCtx->withinFunc()) {
    this->reportError<InsideFunc>(node);
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
    this->checkTypeAsBreakContinue(node);
    break;
  case JumpNode::THROW:
    if (this->funcCtx->finallyLevel() > this->funcCtx->childLevel()) {
      this->reportError<InsideFinally>(node);
    }
    this->checkType(this->typePool.get(TYPE::Any), node.getExprNode());
    break;
  case JumpNode::RETURN:
  case JumpNode::RETURN_INIT: // normally unreachable
    this->checkTypeAsReturn(node);
    break;
  }
  node.setType(this->typePool.get(TYPE::Nothing));
}

void TypeChecker::visitCatchNode(CatchNode &node) {
  auto &exceptionType = this->checkTypeAsSomeExpr(node.getTypeNode());
  /**
   * not allow Void, Nothing and Option type.
   */
  if (exceptionType.isOptionType()) {
    this->reportError<Unacceptable>(node.getTypeNode(), exceptionType.getName());
  }

  {
    auto scope = this->intoBlock();
    /**
     * check type catch block
     */
    auto handle = this->addEntry(node.getNameInfo(), exceptionType, HandleAttr::READ_ONLY);
    if (handle) {
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
  const DSType *exprType = nullptr;
  {
    auto try1 = this->funcCtx->intoTry();
    exprType = &this->checkTypeExactly(node.getExprNode());
  }

  // check type catch block
  for (auto &c : node.getCatchNodes()) {
    auto try1 = this->funcCtx->intoTry();
    auto &catchType = this->checkTypeExactly(*c);
    if (!exprType->isSameOrBaseTypeOf(catchType) && !this->checkCoercion(*exprType, catchType)) {
      exprType = &this->typePool.get(TYPE::Void);
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

void TypeChecker::visitVarDeclNode(VarDeclNode &node) {
  const DSType *exprType = nullptr;
  HandleAttr attr{};
  switch (node.getKind()) {
  case VarDeclNode::LET:
  case VarDeclNode::VAR:
    if (node.getKind() == VarDeclNode::LET) {
      setFlag(attr, HandleAttr::READ_ONLY);
    }
    exprType = &this->checkTypeAsSomeExpr(*node.getExprNode());
    break;
  case VarDeclNode::IMPORT_ENV:
  case VarDeclNode::EXPORT_ENV:
    setFlag(attr, HandleAttr::ENV);
    exprType = &this->typePool.get(TYPE::String);
    if (node.getExprNode() != nullptr) {
      this->checkType(*exprType, *node.getExprNode());
    }
    break;
  }

  auto handle = this->addEntry(node.getNameInfo(), *exprType, attr);
  if (handle) {
    node.setAttribute(*handle);
  }
  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitAssignNode(AssignNode &node) {
  auto &leftNode = node.getLeftNode();
  auto &leftType = this->checkTypeAsExpr(leftNode);
  if (isa<AssignableNode>(leftNode)) {
    auto &assignable = cast<AssignableNode>(leftNode);
    if (assignable.getHandle() && hasFlag(assignable.attr(), HandleAttr::READ_ONLY)) {
      if (isa<VarNode>(leftNode)) {
        this->reportError<ReadOnlySymbol>(leftNode, cast<VarNode>(leftNode).getVarName().c_str());
      } else {
        assert(isa<AccessNode>(leftNode));
        auto &nameNode = cast<AccessNode>(leftNode).getNameNode();
        this->reportError<ReadOnlyField>(nameNode, nameNode.getVarName().c_str());
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
    auto &opNode = cast<BinaryOpNode>(node.getRightNode());
    opNode.getLeftNode()->setType(leftType);
    if (isa<AccessNode>(leftNode)) {
      cast<AccessNode>(leftNode).setAdditionalOp(AccessNode::DUP_RECV);
    }
    auto &rightType = this->checkTypeAsExpr(node.getRightNode());
    if (leftType != rightType) { // convert right hand-side type to left type
      this->resolveCoercion(leftType, node.refRightNode());
    }
  } else {
    this->checkTypeWithCoercion(leftType, node.refRightNode());
  }

  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
  auto &recvType = this->checkTypeAsExpr(node.getRecvNode());
  auto &indexType = this->checkTypeAsExpr(node.getIndexNode());

  node.setRecvType(recvType);
  node.setIndexType(indexType);

  auto &elementType = this->checkTypeAsExpr(node.getGetterNode());
  cast<BinaryOpNode>(node.getRightNode()).getLeftNode()->setType(elementType);

  // convert right hand-side type to element type
  auto &rightType = this->checkTypeAsExpr(node.getRightNode());
  if (elementType != rightType) {
    this->resolveCoercion(elementType, node.refRightNode());
  }

  node.getSetterNode().getArgsNode().getNodes()[1]->setType(elementType);
  this->checkType(this->typePool.get(TYPE::Void), node.getSetterNode());

  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitPrefixAssignNode(PrefixAssignNode &node) {
  if (node.getExprNode()) { // AAA=1243 BBB='fre' expr
    auto scope = this->intoBlock();

    // register envctx
    this->addEntry(node, node.toEnvCtxName(), this->typePool.get(TYPE::Any), HandleAttr::READ_ONLY);

    for (auto &e : node.getAssignNodes()) {
      auto &rightType = this->checkType(this->typePool.get(TYPE::String), e->getRightNode());
      assert(isa<VarNode>(e->getLeftNode()));
      auto &leftNode = cast<VarNode>(e->getLeftNode());
      if (auto handle =
              this->addEntry(leftNode, leftNode.getVarName(), rightType, HandleAttr::ENV)) {
        leftNode.setHandle(handle);
        leftNode.setType(rightType);
      }
    }

    auto &exprType = this->checkTypeExactly(*node.getExprNode());
    node.setBaseIndex(this->curScope->getBaseIndex());
    node.setType(exprType);
  } else { // AAA=1234 BBB='fer'
    for (auto &e : node.getAssignNodes()) {
      this->checkType(this->typePool.get(TYPE::String), e->getLeftNode());
      this->checkTypeExactly(*e);
    }
    node.setType(this->typePool.get(TYPE::Void));
  }
}

void TypeChecker::registerRecordType(FunctionNode &node) {
  assert(node.isConstructor());
  auto typeOrError = this->typePool.createRecordType(node.getFuncName(), this->curScope->modId);
  if (typeOrError) {
    auto &recordType = cast<RecordType>(*typeOrError.asOk());
    if (this->curScope->defineTypeAlias(this->typePool, node.getFuncName(), recordType)) {
      node.setResolvedType(recordType);
    } else {
      this->reportError<DefinedTypeAlias>(node.getNameInfo().getToken(),
                                          node.getFuncName().c_str());
    }
  } else {
    this->reportError(node.getNameInfo().getToken(), std::move(*typeOrError.asErr()));
  }
}

void TypeChecker::registerFuncHandle(FunctionNode &node,
                                     const std::vector<const DSType *> &paramTypes) {
  if (node.isMethod()) {
    auto &recvType = node.getRecvTypeNode()->getType();
    auto ret = this->curScope->defineMethod(this->typePool, recvType, node.getFuncName(),
                                            node.getReturnTypeNode()->getType(), paramTypes);
    if (ret) {
      assert(ret.asOk()->isMethod());
      node.setVarIndex(ret.asOk()->getIndex());
    } else {
      this->reportError<DefinedMethod>(node.getNameInfo().getToken(), node.getFuncName().c_str(),
                                       recvType.getName());
    }
  } else if (node.getReturnTypeNode()) { // for named function
    assert(!node.isConstructor());
    auto typeOrError = this->typePool.createFuncType(node.getReturnTypeNode()->getType(),
                                                     std::vector<const DSType *>(paramTypes));
    if (typeOrError) {
      auto &funcType = cast<FunctionType>(*std::move(typeOrError).take());
      node.setResolvedType(funcType);
      if (HandlePtr handle;
          !node.isAnonymousFunc() &&
          (handle = this->addEntry(node.getNameInfo(), funcType, HandleAttr::READ_ONLY))) {
        node.setVarIndex(handle->getIndex());
      }
    } else {
      this->reportError(node.getToken(), std::move(*typeOrError.asErr()));
    }
  } else if (node.isConstructor()) {
    if (auto *type = node.getResolvedType(); type && type->isRecordType()) {
      auto ret =
          this->curScope->defineConstructor(this->typePool, cast<RecordType>(*type), paramTypes);
      assert(ret && ret.asOk()->isMethod());
      node.setVarIndex(ret.asOk()->getIndex());
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

void TypeChecker::postprocessFunction(FunctionNode &node, const DSType *returnType,
                                      std::vector<const DSType *> &&paramTypes) {
  assert(!node.isConstructor());

  // insert terminal node if not found
  BlockNode &blockNode = node.getBlockNode();
  if (!blockNode.getType().isNothingType()) {
    if (returnType && returnType->isVoidType()) {
      auto emptyNode = std::make_unique<EmptyNode>();
      emptyNode->setType(this->typePool.get(TYPE::Void));
      addReturnNodeToLast(blockNode, this->typePool, std::move(emptyNode));
    } else if (node.isAnonymousFunc()) {
      std::unique_ptr<Node> lastNode;
      if (blockNode.getNodes().empty() || blockNode.getNodes().back()->getType().isVoidType() ||
          this->funcCtx->getVoidReturnCount() > 0) {
        lastNode = std::make_unique<EmptyNode>();
        lastNode->setType(this->typePool.get(TYPE::Void));
      } else {
        lastNode = std::move(blockNode.refNodes().back());
        blockNode.refNodes().pop_back();
      }
      addReturnNodeToLast(blockNode, this->typePool, std::move(lastNode));
      assert(isa<JumpNode>(*blockNode.getNodes().back()));
      this->checkTypeAsReturn(cast<JumpNode>(*blockNode.getNodes().back()));
    }
  }
  if (!blockNode.getType().isNothingType()) {
    this->reportError<UnfoundReturn>(blockNode);
  }

  // resolve common return type
  const FunctionType *funcType = nullptr;
  if (node.getResolvedType() && isa<FunctionType>(node.getResolvedType())) {
    funcType = cast<FunctionType>(node.getResolvedType());
  }

  if (!returnType) {
    assert(!funcType);
    auto &type = this->resolveCoercionOfJumpValue(this->funcCtx->getReturnNodes(), false);
    auto typeOrError = this->typePool.createFuncType(type, std::move(paramTypes));
    if (typeOrError) {
      funcType = cast<FunctionType>(std::move(typeOrError).take());
      node.setResolvedType(*funcType);
    } else {
      this->reportError(node.getToken(), std::move(*typeOrError.asErr()));
    }
  }
  if (node.isSingleExpr()) {
    auto retType = this->typePool.createOptionType(this->typePool.get(TYPE::Any));
    assert(retType);
    auto typeOrError = this->typePool.createFuncType(*retType.asOk(), {});
    assert(typeOrError);
    node.setType(*typeOrError.asOk()); // always `() -> Any!' type
  } else if (node.isAnonymousFunc() && funcType) {
    node.setType(*funcType);
  } else if (node.isMethod()) {
    node.setResolvedType(this->typePool.get(TYPE::Any));
  }
}

void TypeChecker::postprocessConstructor(FunctionNode &node, NameScopePtr &&constructorScope,
                                         unsigned int paramSize) {
  assert(node.isConstructor() && !node.getFuncName().empty());
  assert(constructorScope && constructorScope->parent &&
         constructorScope->kind == NameScope::BLOCK &&
         constructorScope->parent->kind == NameScope::FUNC);

  if (!node.getResolvedType()) {
    return;
  }

  // finalize record type
  const unsigned int offset = paramSize;
  std::unordered_map<std::string, HandlePtr> handles;
  for (auto &e : constructorScope->getHandles()) {
    auto handle = e.second.first;
    if (!handle->has(HandleAttr::TYPE_ALIAS) && !handle->isMethod()) {
      if (handle->getIndex() < offset) {
        continue;
      }

      // field
      auto &fieldType = this->typePool.get(handle->getTypeId());
      handle = HandlePtr::create(fieldType, handle->getIndex() - offset, handle->attr(),
                                 handle->getModId());
    }
    handles.emplace(e.first, std::move(handle));
  }
  assert(node.getResolvedType()->isRecordType());
  auto typeOrError = this->typePool.finalizeRecordType(cast<RecordType>(*node.getResolvedType()),
                                                       std::move(handles));
  if (!typeOrError) {
    this->reportError(node.getNameInfo().getToken(), std::move(*typeOrError.asErr()));
  }

  unsigned int fieldSize = cast<RecordType>(node.getResolvedType())->getFieldSize();
  auto returnNode = JumpNode::newReturnInit(*node.getResolvedType(), offset, fieldSize);
  returnNode->setType(this->typePool.get(TYPE::Nothing));
  node.getBlockNode().addNode(std::move(returnNode));
}

void TypeChecker::visitFunctionNode(FunctionNode &node) {
  node.setType(this->typePool.get(TYPE::Void));
  if (!this->curScope->isGlobal()) { // only available toplevel scope
    this->reportError<OutsideToplevel>(node);
    return;
  }

  if (node.isConstructor()) {
    this->registerRecordType(node);
  }

  // resolve param type, return type
  const unsigned int paramSize = node.getParamTypeNodes().size();
  std::vector<const DSType *> paramTypes(paramSize);
  for (unsigned int i = 0; i < paramSize; i++) {
    auto &type = this->checkTypeAsSomeExpr(*node.getParamTypeNodes()[i]);
    paramTypes[i] = &type;
  }
  auto *returnType =
      node.getReturnTypeNode() ? &this->checkTypeExactly(*node.getReturnTypeNode()) : nullptr;

  if (node.isMethod()) {
    this->checkTypeAsSomeExpr(*node.getRecvTypeNode());
  }

  // register function/constructor handle
  this->registerFuncHandle(node, paramTypes);

  // func body
  NameScopePtr scope;
  {
    assert(!node.isConstructor() || (node.isConstructor() && !returnType));
    auto func = this->intoFunc(returnType,
                               node.isConstructor() ? FuncContext::CONSTRUCTOR : FuncContext::FUNC);
    // register parameter
    if (node.isMethod()) {
      NameInfo nameInfo(node.getRecvTypeNode()->getToken(), "this");
      this->addEntry(nameInfo, node.getRecvTypeNode()->getType(), HandleAttr());
    }
    for (unsigned int i = 0; i < paramSize; i++) {
      this->addEntry(node.getParams()[i], *paramTypes[i], HandleAttr());
    }
    // check type func body
    this->checkTypeWithCurrentScope(
        node.isAnonymousFunc() ? nullptr : &this->typePool.get(TYPE::Void), node.getBlockNode());
    node.setMaxVarNum(this->curScope->getMaxLocalVarIndex());
    if (node.isConstructor()) {
      scope = this->curScope; // save current scope
    } else {
      this->postprocessFunction(node, returnType, std::move(paramTypes));
    }
  }

  if (node.isConstructor()) {
    this->postprocessConstructor(node, std::move(scope), paramSize);
  }
}

void TypeChecker::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
  node.setType(this->typePool.get(TYPE::Void));

  if (!this->isTopLevel()) { // only available toplevel scope
    this->reportError<OutsideToplevel>(node);
    return;
  }

  if (node.getReturnTypeNode()) {
    this->checkType(this->typePool.get(TYPE::Nothing), *node.getReturnTypeNode());
  }

  // register command name
  auto *returnType = &this->typePool.get(TYPE::Int);
  if (auto *handle = this->addUdcEntry(node)) {
    node.setUdcIndex(handle->getIndex());
    returnType = &cast<FunctionType>(this->typePool.get(handle->getTypeId())).getReturnType();
  }

  // check type udc body
  {
    auto func = this->intoFunc(returnType); // pseudo return type
    // register dummy parameter (for propagating command attr)
    this->addEntry(node, "%%attr", this->typePool.get(TYPE::Any), HandleAttr::READ_ONLY);

    // register dummy parameter (for closing file descriptor)
    this->addEntry(node, "%%redir", this->typePool.get(TYPE::Any), HandleAttr::READ_ONLY);

    // register special characters (@, #, 0, 1, ... 9)
    this->addEntry(node, "@", this->typePool.get(TYPE::StringArray), HandleAttr::READ_ONLY);
    this->addEntry(node, "#", this->typePool.get(TYPE::Int), HandleAttr::READ_ONLY);
    for (unsigned int i = 0; i < 10; i++) {
      this->addEntry(node, std::to_string(i), this->typePool.get(TYPE::String),
                     HandleAttr::READ_ONLY);
    }

    // check type command body
    this->checkTypeWithCurrentScope(node.getBlockNode());
    node.setMaxVarNum(this->curScope->getMaxLocalVarIndex());
  }

  // insert return node if not found
  if (node.getBlockNode().getNodes().empty() ||
      !node.getBlockNode().getNodes().back()->getType().isNothingType()) {
    if (returnType->isNothingType()) {
      this->reportError<UnfoundReturn>(node.getBlockNode());
    } else {
      unsigned int lastPos = node.getBlockNode().getToken().endPos();
      auto varNode = std::make_unique<VarNode>(Token{lastPos, 0}, "?");
      this->checkTypeAsExpr(*varNode);
      addReturnNodeToLast(node.getBlockNode(), this->typePool, std::move(varNode));
    }
  }
}

void TypeChecker::visitInterfaceNode(InterfaceNode &node) {
  //    if(!this->isTopLevel()) {   // only available toplevel scope
  //        RAISE_TC_ERROR(OutsideToplevel, node);
  //    }
  node.setType(this->typePool.get(TYPE::Void));
  this->reportError<OutsideToplevel>(node);
}

void TypeChecker::visitSourceNode(SourceNode &node) {
  assert(this->isTopLevel());

  // import module
  ImportedModKind importedKind{};
  if (!node.getNameInfo()) {
    setFlag(importedKind, ImportedModKind::GLOBAL);
  }
  if (node.isInlined()) {
    setFlag(importedKind, ImportedModKind::INLINED);
  }
  auto ret = this->curScope->importForeignHandles(this->typePool, node.getModType(), importedKind);
  if (!ret.empty()) {
    this->reportError<ConflictSymbol>(node, ret.c_str(), node.getPathName().c_str());
  }
  if (node.getNameInfo()) { // scoped import
    auto &nameInfo = *node.getNameInfo();
    auto handle = node.getModType().toAliasHandle(this->curScope->modId);

    // register actual module handle
    if (!this->curScope->defineAlias(std::string(nameInfo.getName()), handle)) {
      this->reportError<DefinedSymbol>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
    std::string cmdName = toCmdFullName(nameInfo.getName());
    if (!this->curScope->defineAlias(std::move(cmdName), handle)) { // for module subcommand
      this->reportError<DefinedCmd>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
    if (!this->curScope->defineTypeAlias(this->typePool, nameInfo.getName(), node.getModType())) {
      this->reportError<DefinedTypeAlias>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
  }
  node.setType(this->typePool.get(node.isNothing() ? TYPE::Nothing : TYPE::Void));
}

class SourceGlobIter {
private:
  using iterator = SourceListNode::path_iterator;

  iterator cur;
  const char *ptr{nullptr};

public:
  explicit SourceGlobIter(iterator begin) : cur(begin) {
    if (isa<StringNode>(**this->cur)) {
      this->ptr = cast<StringNode>(**this->cur).getValue().c_str();
    }
  }

  char operator*() const { return this->ptr == nullptr ? '\0' : *this->ptr; }

  bool operator==(const SourceGlobIter &other) const {
    return this->cur == other.cur && this->ptr == other.ptr;
  }

  bool operator!=(const SourceGlobIter &other) const { return !(*this == other); }

  SourceGlobIter &operator++() {
    if (this->ptr) {
      this->ptr++;
      if (*this->ptr == '\0') { // if reaches null, increment iterator
        this->ptr = nullptr;
      }
    }
    if (!this->ptr) {
      ++this->cur;
      if (isa<StringNode>(**this->cur)) {
        this->ptr = cast<StringNode>(**this->cur).getValue().c_str();
      }
    }
    return *this;
  }

  iterator getIter() const { return this->cur; }
};

struct SourceGlobMeta {
  static bool isAny(SourceGlobIter iter) {
    auto &node = **iter.getIter();
    return isa<WildCardNode>(node) && cast<WildCardNode>(node).meta == GlobMeta::ANY;
  }

  static bool isZeroOrMore(SourceGlobIter iter) {
    auto &node = **iter.getIter();
    return isa<WildCardNode>(node) && cast<WildCardNode>(node).meta == GlobMeta::ZERO_OR_MORE;
  }

  static void preExpand(std::string &path) { expandTilde(path, true); }
};

static std::string concat(const CmdArgNode &node, const unsigned int endOffset) {
  std::string path;
  for (unsigned int i = 0; i < endOffset; i++) {
    auto &e = node.getSegmentNodes()[i];
    assert(isa<StringNode>(*e) || isa<WildCardNode>(*e));
    if (isa<StringNode>(*e)) {
      path += cast<StringNode>(*e).getValue();
    } else {
      path += toString(cast<WildCardNode>(*e).meta);
    }
  }
  return path;
}

static bool isDirPattern(const CmdArgNode &node) {
  auto &nodes = node.getSegmentNodes();
  assert(!nodes.empty());
  for (auto iter = nodes.rbegin(); iter != nodes.crend(); ++iter) {
    if (isa<StringNode>(**iter)) {
      StringRef ref = cast<StringNode>(**iter).getValue();
      if (ref.empty()) {
        continue;
      }
      return ref.back() == '/' || ref.endsWith("/.") || ref.endsWith("/..");
    }
    break;
  }
  return false;
}

void TypeChecker::resolvePathList(SourceListNode &node) {
  if (!node.getConstPathNode()) {
    return;
  }
  auto &pathNode = *node.getConstPathNode();
  std::vector<std::shared_ptr<const std::string>> ret;
  if (pathNode.getGlobPathSize() == 0) {
    std::string path = concat(pathNode, pathNode.getSegmentNodes().size());
    if (pathNode.isTilde()) {
      expandTilde(path, true);
    }
    ret.push_back(std::make_shared<const std::string>(std::move(path)));
  } else {
    if (isDirPattern(pathNode)) {
      std::string path = concat(pathNode, pathNode.getSegmentNodes().size());
      this->reportError<NoGlobDir>(pathNode, path.c_str());
      return;
    }
    pathNode.addSegmentNode(std::make_unique<EmptyNode>()); // sentinel
    auto begin = SourceGlobIter(pathNode.getSegmentNodes().cbegin());
    auto end = SourceGlobIter(pathNode.getSegmentNodes().cend() - 1);
    auto appender = [&](std::string &&path) {
      if (ret.size() == 4096) {
        return false;
      }
      ret.push_back(std::make_shared<const std::string>(std::move(path)));
      return true;
    };
    auto option = GlobMatchOption::IGNORE_SYS_DIR | GlobMatchOption::FASTGLOB;
    if (pathNode.isTilde()) {
      setFlag(option, GlobMatchOption::TILDE);
    }
    auto matcher =
        createGlobMatcher<SourceGlobMeta>(this->lexer->getScriptDir(), begin, end, option);
    auto globRet = matcher(appender);
    if (globRet == GlobMatchResult::MATCH || node.isOptional()) {
      std::sort(ret.begin(), ret.end(),
                [](const std::shared_ptr<const std::string> &x,
                   const std::shared_ptr<const std::string> &y) { return *x < *y; });
    } else {
      std::string path = concat(pathNode, pathNode.getSegmentNodes().size() - 1); // skip sentinel
      if (globRet == GlobMatchResult::NOMATCH) {
        this->reportError<NoGlobMatch>(pathNode, path.c_str());
      } else {
        this->reportError<GlobRetLimit>(pathNode, path.c_str());
      }
    }
  }
  node.setPathList(std::move(ret));
}

void TypeChecker::visitSourceListNode(SourceListNode &node) {
  node.setType(this->typePool.get(TYPE::Void));
  if (!this->isTopLevel()) { // only available toplevel scope
    this->reportError<OutsideToplevel>(node);
    return;
  }
  bool isGlob = node.getPathNode().getGlobPathSize() > 0 && !node.getNameInfoPtr();
  auto &exprType = this->typePool.get(isGlob ? TYPE::StringArray : TYPE::String);
  this->checkType(exprType, node.getPathNode());

  std::unique_ptr<CmdArgNode> constPathNode;
  for (auto &e : node.getPathNode().getSegmentNodes()) {
    auto constNode = this->evalConstant(*e);
    if (!constNode) {
      return;
    }
    assert(isa<StringNode>(*constNode) || isa<WildCardNode>(*constNode));
    if (isa<StringNode>(*constNode)) {
      auto ref = StringRef(cast<StringNode>(*constNode).getValue());
      if (ref.hasNullChar()) {
        this->reportError<NullInPath>(node.getPathNode());
        return;
      }
    }
    if (!constPathNode) {
      constPathNode = std::make_unique<CmdArgNode>(std::move(constNode));
    } else {
      constPathNode->addSegmentNode(std::move(constNode));
    }
  }
  node.setConstPathNode(std::move(constPathNode));
  this->resolvePathList(node);
}

void TypeChecker::visitCodeCompNode(CodeCompNode &node) {
  assert(this->ccHandler);
  this->reachComp = true;
  node.setType(this->typePool.get(TYPE::Void));
  switch (node.getKind()) {
  case CodeCompNode::VAR:
    this->ccHandler->addVarNameRequest(this->lexer->toName(node.getTypingToken()), this->curScope);
    break;
  case CodeCompNode::MEMBER: {
    assert(node.getExprNode());
    auto &recvType = this->checkTypeAsExpr(*node.getExprNode());
    if (recvType.isRecordType() && !cast<RecordType>(recvType).isFinalized()) {
      break; // ignore non-finalized record type
    }
    this->ccHandler->addMemberRequest(recvType, this->lexer->toTokenText(node.getTypingToken()));
    break;
  }
  case CodeCompNode::TYPE: {
    const DSType *recvType = nullptr;
    if (node.getExprNode()) {
      recvType = &this->checkTypeExactly(*node.getExprNode());
    }
    this->ccHandler->addTypeNameRequest(this->lexer->toName(node.getTypingToken()), recvType,
                                        this->curScope);
    break;
  }
  }
  this->reportError<Unreachable>(node);
}

void TypeChecker::visitErrorNode(ErrorNode &node) { node.setType(this->typePool.get(TYPE::Void)); }

void TypeChecker::visitEmptyNode(EmptyNode &node) { node.setType(this->typePool.get(TYPE::Void)); }

static bool mayBeCmd(const Node &node) {
  if (isa<CmdNode>(node)) {
    return true;
  }
  if (isa<PipelineNode>(node)) {
    if (cast<PipelineNode>(node).getNodes().back()->is(NodeKind::Cmd)) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<Node> TypeChecker::operator()(const DSType *prevType, std::unique_ptr<Node> &&node,
                                              NameScopePtr global) {
  // reset state
  this->visitingDepth = 0;
  this->funcCtx->clear();
  this->errors.clear();

  // set scope
  this->curScope = std::move(global);

  if (prevType != nullptr && prevType->isNothingType()) {
    this->reportError<Unreachable>(*node);
  }
  if (this->toplevelPrinting && this->curScope->inRootModule() && !mayBeCmd(*node)) {
    this->checkTypeExactly(*node);
    node = this->newPrintOpNode(std::move(node));
  } else {
    this->checkTypeWithCoercion(this->typePool.get(TYPE::Void), node); // pop stack top
  }
  if (this->hasError()) {
    node = std::make_unique<ErrorNode>(std::move(node));
    node->setType(this->typePool.get(TYPE::Void));
  }
  return std::move(node);
}

} // namespace ydsh
