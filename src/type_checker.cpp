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

#include <sys/utsname.h>

#include <vector>

#include "complete.h"
#include "constant.h"
#include "core.h"
#include "misc/glob.hpp"
#include "type_checker.h"

namespace ydsh {

// #########################
// ##     BreakGather     ##
// #########################

void BreakGather::clear() { delete this->entry; }

void BreakGather::enter() {
  auto *e = new Entry(this->entry);
  this->entry = e;
}

void BreakGather::leave() {
  auto *old = this->entry->next;
  this->entry->next = nullptr;
  this->clear();
  this->entry = old;
}

void BreakGather::addJumpNode(JumpNode *node) {
  assert(this->entry != nullptr);
  this->entry->jumpNodes.push_back(node);
}

// #########################
// ##     TypeChecker     ##
// #########################

#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto value = E;                                                                                \
    if (!value) {                                                                                  \
      return value;                                                                                \
    }                                                                                              \
    value.take();                                                                                  \
  })

TypeOrError TypeChecker::toTypeImpl(TypeNode &node) {
  switch (node.typeKind) {
  case TypeNode::Base: {
    auto &typeNode = cast<BaseTypeNode>(node);

    // fist lookup type alias
    auto handle = this->curScope->lookup(toTypeAliasFullName(typeNode.getTokenText()));
    if (handle) {
      return Ok(&this->typePool.get(handle->getTypeID()));
    }
    return this->typePool.getType(typeNode.getTokenText());
  }
  case TypeNode::Qualified: {
    auto &qualifiedNode = cast<QualifiedTypeNode>(node);
    auto &recvType = this->checkTypeExactly(qualifiedNode.getRecvTypeNode());
    if (!recvType.isModType()) {
      RAISE_TC_ERROR(Required, qualifiedNode.getRecvTypeNode(), "Module type", recvType.getName());
    }
    auto &modType = static_cast<const ModType &>(recvType);
    std::string typeName = toTypeAliasFullName(qualifiedNode.getNameTypeNode().getTokenText());
    auto *handle = this->curScope->lookupField(modType, typeName);
    if (!handle) {
      auto &nameNode = qualifiedNode.getNameTypeNode();
      RAISE_TC_ERROR(UndefinedField, nameNode, nameNode.getTokenText().c_str());
    }
    return Ok(&this->typePool.get(handle->getTypeID()));
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
    auto &returnType = this->checkTypeExactly(*typeNode.getReturnTypeNode());
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
      RAISE_TC_ERROR(Unacceptable, targetNode, type.getName());
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

  RAISE_TC_ERROR(Required, targetNode, requiredType->getName(), type.getName());
}

const DSType &TypeChecker::checkTypeAsSomeExpr(Node &targetNode) {
  auto &type = this->checkTypeAsExpr(targetNode);
  if (type.isNothingType()) {
    RAISE_TC_ERROR(Unacceptable, targetNode, type.getName());
  }
  return type;
}

void TypeChecker::checkTypeWithCurrentScope(const DSType *requiredType, BlockNode &blockNode) {
  auto *blockType = &this->typePool.get(TYPE::Void);
  for (auto iter = blockNode.refNodes().begin(); iter != blockNode.refNodes().end(); ++iter) {
    auto &targetNode = *iter;
    if (blockType->isNothingType()) {
      RAISE_TC_ERROR(Unreachable, *targetNode);
    }
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
      RAISE_TC_ERROR(UselessBlock, *targetNode);
    }
  }

  // set base index of current scope
  blockNode.setBaseIndex(this->curScope->getBaseIndex());
  blockNode.setVarSize(this->curScope->getLocalSize());
  blockNode.setMaxVarSize(this->curScope->getMaxLocalVarIndex() - blockNode.getBaseIndex());

  assert(blockType != nullptr);
  blockNode.setType(*blockType);
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

const DSType &TypeChecker::resolveCoercionOfJumpValue() {
  auto &jumpNodes = this->breakGather.getJumpNodes();
  if (jumpNodes.empty()) {
    return this->typePool.get(TYPE::Void);
  }

  auto &firstType = jumpNodes[0]->getExprNode().getType();
  assert(!firstType.isNothingType() && !firstType.isVoidType());

  for (auto &jumpNode : jumpNodes) {
    if (firstType != jumpNode->getExprNode().getType()) {
      this->checkTypeWithCoercion(firstType, jumpNode->refExprNode());
    }
  }
  auto ret = this->typePool.createOptionType(firstType);
  assert(ret);
  return *std::move(ret).take();
}

const FieldHandle *TypeChecker::addEntry(const Node &node, const std::string &symbolName,
                                         const DSType &type, FieldAttribute attribute) {
  auto ret = this->curScope->defineHandle(std::string(symbolName), type, attribute);
  if (!ret) {
    switch (ret.asErr()) {
    case NameLookupError::DEFINED:
      RAISE_TC_ERROR(DefinedSymbol, node, symbolName.c_str());
    case NameLookupError::LIMIT:
      RAISE_TC_ERROR(LocalLimit, node);
    }
  }
  return ret.asOk();
}

static auto initDeniedNameList() {
  std::unordered_set<std::string> set;
  for (auto &e : DENIED_REDEFINED_CMD_LIST) {
    set.insert(e);
  }
  return set;
}

const FieldHandle *TypeChecker::addUdcEntry(const UserDefinedCmdNode &node) {
  static auto deniedList = initDeniedNameList();
  if (deniedList.find(node.getCmdName()) != deniedList.end()) {
    RAISE_TC_ERROR(DefinedCmd, node, node.getCmdName().c_str()); // FIXME: better error message
  }

  std::string name = toCmdFullName(node.getCmdName());
  auto ret = this->curScope->defineHandle(std::move(name), this->typePool.get(TYPE::Any),
                                          FieldAttribute::READ_ONLY);
  if (!ret) {
    assert(ret.asErr() == NameLookupError::DEFINED);
    RAISE_TC_ERROR(DefinedCmd, node, node.getCmdName().c_str());
  }
  return ret.asOk();
}

// for ApplyNode type checking
HandleOrFuncType TypeChecker::resolveCallee(ApplyNode &node) {
  auto &exprNode = node.getExprNode();
  if (exprNode.is(NodeKind::Access) && !node.isFuncCall()) {
    auto &accessNode = cast<AccessNode>(exprNode);
    if (!this->checkAccessNode(accessNode)) {
      auto &recvType = accessNode.getRecvNode().getType();
      auto *handle = this->typePool.lookupMethod(recvType, accessNode.getFieldName());
      if (handle == nullptr) {
        const char *name = accessNode.getFieldName().c_str();
        RAISE_TC_ERROR(UndefinedMethod, accessNode.getNameNode(), name);
      }
      node.setKind(ApplyNode::METHOD_CALL);
      return handle;
    }
  }

  node.setKind(ApplyNode::FUNC_CALL);
  if (exprNode.is(NodeKind::Var)) {
    return this->resolveCallee(cast<VarNode>(exprNode));
  }

  auto &type = this->checkType(this->typePool.get(TYPE::Func), exprNode);
  if (!type.isFuncType()) {
    RAISE_TC_ERROR(NotCallable, exprNode);
  }
  return static_cast<const FunctionType *>(&type);
}

HandleOrFuncType TypeChecker::resolveCallee(VarNode &recvNode) {
  auto handle = this->curScope->lookup(recvNode.getVarName());
  if (handle == nullptr) {
    RAISE_TC_ERROR(UndefinedSymbol, recvNode, recvNode.getVarName().c_str());
  }
  recvNode.setAttribute(*handle);

  auto &type = this->typePool.get(handle->getTypeID());
  if (!type.isFuncType()) {
    if (type.is(TYPE::Func)) {
      RAISE_TC_ERROR(NotCallable, recvNode);
    } else {
      RAISE_TC_ERROR(Required, recvNode, this->typePool.get(TYPE::Func).getName(), type.getName());
    }
  }
  return static_cast<FunctionType *>(const_cast<DSType *>(&type));
}

bool TypeChecker::checkAccessNode(AccessNode &node) {
  auto &recvType = this->checkTypeAsExpr(node.getRecvNode());
  auto *handle = this->curScope->lookupField(recvType, node.getFieldName());
  if (handle == nullptr) {
    return false;
  }
  node.setAttribute(*handle);
  node.setType(this->typePool.get(handle->getTypeID()));
  return true;
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

  RAISE_TC_ERROR(CastOp, node, exprType.getName(), targetType.getName());
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
  auto ret = this->toTypeImpl(node);
  if (ret) {
    node.setType(*std::move(ret).take());
  } else {
    throw TypeCheckError(node.getToken(), *ret.asErr());
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
    RAISE_TC_ERROR(RegexSyntax, node, e.c_str());
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

  auto typeOrError = this->typePool.createArrayType(elementType);
  assert(typeOrError);
  node.setType(*std::move(typeOrError).take());
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

  auto typeOrError = this->typePool.createMapType(keyType, valueType);
  assert(typeOrError);
  node.setType(*std::move(typeOrError).take());
}

void TypeChecker::visitTupleNode(TupleNode &node) {
  unsigned int size = node.getNodes().size();
  std::vector<const DSType *> types(size);
  for (unsigned int i = 0; i < size; i++) {
    types[i] = &this->checkTypeAsSomeExpr(*node.getNodes()[i]);
  }
  auto typeOrError = this->typePool.createTupleType(std::move(types));
  assert(typeOrError);
  node.setType(*std::move(typeOrError).take());
}

void TypeChecker::visitVarNode(VarNode &node) {
  auto handle = this->curScope->lookup(node.getVarName());
  if (handle == nullptr) {
    RAISE_TC_ERROR(UndefinedSymbol, node, node.getVarName().c_str());
  }

  node.setAttribute(*handle);
  node.setType(this->typePool.get(handle->getTypeID()));
}

void TypeChecker::visitAccessNode(AccessNode &node) {
  if (!this->checkAccessNode(node)) {
    RAISE_TC_ERROR(UndefinedField, node.getNameNode(), node.getFieldName().c_str());
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
    } else if (!exprType.isOptionType() && exprType.isSameOrBaseTypeOf(targetType)) {
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
    if (!exprType.isOptionType()) {
      RAISE_TC_ERROR(Required, *node.getExprNode(), "Option type", exprType.getName());
    }
    node.setType(static_cast<const ReifiedType *>(&exprType)->getElementTypeAt(0));
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
      RAISE_TC_ERROR(Unreachable, *node.getRightNode());
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
    if (!leftType.isOptionType()) {
      RAISE_TC_ERROR(Required, *node.getLeftNode(), "Option type", leftType.getName());
    }
    auto &elementType = static_cast<const ReifiedType &>(leftType).getElementTypeAt(0);
    this->checkTypeWithCoercion(elementType, node.refRightNode());
    node.setType(elementType);
    return;
  }

  auto &leftType = this->checkTypeAsExpr(*node.getLeftNode());
  auto &rightType = this->checkTypeAsExpr(*node.getRightNode());

  // check referencial equality of func object
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

void TypeChecker::visitArgsNode(ArgsNode &node) {
  auto hf = node.getTypeContext();
  if (is<const MethodHandle *>(hf)) {
    auto *handle = get<const MethodHandle *>(hf);
    unsigned int argSize = node.getNodes().size();
    // check param size
    unsigned int paramSize = handle->getParamSize();
    if (paramSize != argSize) {
      RAISE_TC_ERROR(UnmatchParam, node, paramSize, argSize);
    }

    // check type each node
    for (unsigned int i = 0; i < paramSize; i++) {
      this->checkTypeWithCoercion(handle->getParamTypeAt(i), node.refNodes()[i]);
    }
  } else {
    auto *funcType = get<const FunctionType *>(hf);
    unsigned int size = funcType->getParamSize();
    unsigned int argSize = node.getNodes().size();
    // check param size
    if (size != argSize) {
      RAISE_TC_ERROR(UnmatchParam, node, size, argSize);
    }

    // check type each node
    for (unsigned int i = 0; i < size; i++) {
      this->checkTypeWithCoercion(funcType->getParamTypeAt(i), node.refNodes()[i]);
    }
  }
  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitApplyNode(ApplyNode &node) {
  /**
   * resolve handle
   */
  HandleOrFuncType hf = this->resolveCallee(node);
  node.getArgsNode().setTypeContext(hf);
  this->checkTypeExactly(node.getArgsNode());
  if (is<const MethodHandle *>(hf)) {
    node.setHandle(get<const MethodHandle *>(hf));
    node.setType(node.getHandle()->getReturnType());
  } else {
    node.setType(get<const FunctionType *>(hf)->getReturnType());
  }
}

void TypeChecker::visitNewNode(NewNode &node) {
  auto &type = this->checkTypeAsExpr(node.getTargetTypeNode());
  if (type.isOptionType() || this->typePool.isArrayType(type) || this->typePool.isMapType(type)) {
    unsigned int size = node.getArgsNode().getNodes().size();
    if (size > 0) {
      RAISE_TC_ERROR(UnmatchParam, node.getArgsNode(), 0, size);
    }
  } else {
    auto *handle = this->typePool.lookupConstructor(type);
    if (handle == nullptr) {
      RAISE_TC_ERROR(UndefinedInit, node.getTargetTypeNode(), type.getName());
    }
    node.getArgsNode().setTypeContext(handle);
    this->checkTypeExactly(node.getArgsNode());
    node.setHandle(handle);
  }

  node.setType(type);
}

void TypeChecker::visitEmbedNode(EmbedNode &node) {
  auto &exprType = this->checkTypeAsExpr(node.getExprNode());
  node.setType(exprType);

  if (node.getKind() == EmbedNode::STR_EXPR) {
    auto &type = this->typePool.get(TYPE::String);
    if (!type.isSameOrBaseTypeOf(exprType)) { // call __INTERP__()
      std::string methodName(OP_INTERP);
      auto *handle =
          exprType.isOptionType() ? nullptr : this->typePool.lookupMethod(exprType, methodName);
      if (handle == nullptr) { // if exprType is
        RAISE_TC_ERROR(UndefinedMethod, node.getExprNode(), methodName.c_str());
      }
      assert(handle->getReturnType() == type);
      node.setHandle(handle);
    }
  } else {
    if (!this->typePool.get(TYPE::String).isSameOrBaseTypeOf(exprType) &&
        !this->typePool.get(TYPE::StringArray).isSameOrBaseTypeOf(exprType) &&
        !this->typePool.get(TYPE::UnixFD)
             .isSameOrBaseTypeOf(exprType)) { // call __STR__ or __CMD__ARG
      // first try lookup __CMD_ARG__ method
      std::string methodName(OP_CMD_ARG);
      auto *handle = this->typePool.lookupMethod(exprType, methodName);

      if (handle == nullptr) { // if not found, lookup __STR__
        methodName = OP_STR;
        handle =
            exprType.isOptionType() ? nullptr : this->typePool.lookupMethod(exprType, methodName);
        if (handle == nullptr) {
          RAISE_TC_ERROR(UndefinedMethod, node.getExprNode(), methodName.c_str());
        }
      }
      assert(handle->getReturnType().is(TYPE::String) ||
             handle->getReturnType().is(TYPE::StringArray));
      node.setHandle(handle);
      node.setType(handle->getReturnType());
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
  }
}

void TypeChecker::visitCmdArgNode(CmdArgNode &node) {
  for (auto &exprNode : node.getSegmentNodes()) {
    this->checkTypeAsExpr(*exprNode);
    assert(exprNode->getType().is(TYPE::String) || exprNode->getType().is(TYPE::StringArray) ||
           exprNode->getType().is(TYPE::UnixFD) || exprNode->getType().isNothingType());
  }

  if (node.getGlobPathSize() > SYS_LIMIT_GLOB_FRAG_NUM) {
    RAISE_TC_ERROR(GlobLimit, node);
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
  assert(argNode.getType().isNothingType() || argNode.getType().is(TYPE::String) ||
         argNode.getType().is(TYPE::UnixFD));
  node.setType(this->typePool.get(TYPE::Any)); // FIXME:
}

void TypeChecker::visitWildCardNode(WildCardNode &node) {
  node.setType(this->typePool.get(TYPE::String));
}

void TypeChecker::visitPipelineNode(PipelineNode &node) {
  unsigned int size = node.getNodes().size();
  if (size > SYS_LIMIT_PIPE_LEN) {
    RAISE_TC_ERROR(PipeLimit, node);
  }

  {
    auto child = this->intoChild();
    for (unsigned int i = 0; i < size - 1; i++) {
      this->checkTypeExactly(*node.getNodes()[i]);
    }
  }

  {
    auto scope = this->intoBlock();
    if (node.isLastPipe()) { // register pipeline state
      this->addEntry(node, "%%pipe", this->typePool.get(TYPE::Any), FieldAttribute::READ_ONLY);
      node.setBaseIndex(this->curScope->getBaseIndex());
    }
    auto &type = this->checkTypeExactly(*node.getNodes()[size - 1]);
    node.setType(node.isLastPipe() ? type : this->typePool.get(TYPE::Boolean));
  }
}

void TypeChecker::visitWithNode(WithNode &node) {
  auto scope = this->intoBlock();

  // register redir config
  this->addEntry(node, "%%redir", this->typePool.get(TYPE::Any), FieldAttribute::READ_ONLY);

  auto &type = this->checkTypeExactly(node.getExprNode());
  for (auto &e : node.getRedirNodes()) {
    this->checkTypeAsExpr(*e);
  }

  node.setBaseIndex(this->curScope->getBaseIndex());
  node.setType(type);
}

void TypeChecker::visitForkNode(ForkNode &node) {
  auto child = this->intoChild();
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
    RAISE_TC_ERROR(UselessBlock, node);
  }
  auto scope = this->intoBlock();
  this->checkTypeWithCurrentScope(nullptr, node);
}

void TypeChecker::visitTypeAliasNode(TypeAliasNode &node) {
  TypeNode &typeToken = node.getTargetTypeNode();
  auto &type = this->checkTypeExactly(typeToken);
  auto ret = this->curScope->defineTypeAlias(this->typePool, std::string(node.getAlias()), type);
  if (!ret) {
    RAISE_TC_ERROR(DefinedTypeAlias, node, node.getAlias().c_str());
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
      auto loop = this->intoLoop();
      this->checkTypeWithCurrentScope(node.getBlockNode());
      auto &type = this->resolveCoercionOfJumpValue();
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
  auto *patternType = collector.getType();
  if (!patternType) {
    RAISE_TC_ERROR(NeedPattern, node);
  }
  if (exprType->isOptionType()) {
    exprType = &static_cast<const ReifiedType *>(exprType)->getElementTypeAt(0);
  }
  if (!patternType->isSameOrBaseTypeOf(*exprType)) {
    RAISE_TC_ERROR(Required, node.getExprNode(), patternType->getName(), exprType->getName());
  }

  // resolve arm expr type
  unsigned int size = node.getArmNodes().size();
  std::vector<const DSType *> types(size);
  for (unsigned int i = 0; i < size; i++) {
    types[i] = &this->checkTypeExactly(*node.getArmNodes()[i]);
  }
  auto &type = this->resolveCommonSuperType(types);

  // apply coercion
  for (auto &armNode : node.getArmNodes()) {
    this->checkTypeWithCoercion(type, armNode->refActionNode());
    armNode->setType(type);
  }

  if (!type.isVoidType() && !collector.hasElsePattern()) {
    RAISE_TC_ERROR(NeedDefault, node);
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
      RAISE_TC_ERROR(DupPattern, *elseNode);
    }
    collector.setElsePattern(true);
  }

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
      RAISE_TC_ERROR(Required, *e, collector.getType()->getName(), type->getName());
    }
  }

  for (auto &e : node.refPatternNodes()) {
    this->applyConstFolding(e);
  }

  for (auto &e : node.getPatternNodes()) {
    if (e->is(NodeKind::Regex)) {
      continue;
    }
    if (!collector.collect(*e)) {
      RAISE_TC_ERROR(DupPattern, *e);
    }
  }
}

const DSType &TypeChecker::resolveCommonSuperType(const std::vector<const DSType *> &types) {
  for (auto &type : types) {
    unsigned int size = types.size();
    unsigned int index = 0;
    for (; index < size; index++) {
      auto &curType = types[index];
      if (type->isSameOrBaseTypeOf(*curType)) {
        continue;
      }

      if (!this->checkCoercion(*type, *curType)) {
        break;
      }
    }
    if (index == size) {
      return *type;
    }
  }
  return this->typePool.get(TYPE::Void);
}

static auto initConstVarMap() {
  struct utsname name {};
  if (uname(&name) == -1) {
    fatal_perror("cannot get utsname");
  }

  StrRefMap<std::string> map = {
      {CVAR_VERSION, X_INFO_VERSION_CORE}, {CVAR_DATA_DIR, SYSTEM_DATA_DIR},
      {CVAR_MODULE_DIR, SYSTEM_MOD_DIR},   {CVAR_OSTYPE, name.sysname},
      {CVAR_MACHTYPE, BUILD_ARCH},
  };
  return map;
}

bool TypeChecker::applyConstFolding(std::unique_ptr<Node> &node) {
  switch (node->getNodeKind()) {
  case NodeKind::String:
  case NodeKind::Number:
  case NodeKind::Regex:
  case NodeKind::WildCard:
    return true;
  case NodeKind::UnaryOp: { // !, +, -, !
    auto &unaryNode = cast<UnaryOpNode>(*node);
    Token token = node->getToken();
    const auto op = unaryNode.getOp();
    if (node->getType().is(TYPE::Int) &&
        (op == TokenKind::MINUS || op == TokenKind::PLUS || op == TokenKind::NOT)) {
      auto &applyNode = unaryNode.refApplyNode();
      assert(applyNode->getExprNode().is(NodeKind::Access));
      auto &accessNode = cast<AccessNode>(applyNode->getExprNode());
      auto &recvNode = accessNode.refRecvNode();
      if (!this->applyConstFolding(recvNode)) {
        break;
      }
      assert(isa<NumberNode>(*recvNode));
      int64_t value = cast<NumberNode>(*recvNode).getIntValue();
      if (op == TokenKind::MINUS) {
        value = -value;
      } else if (op == TokenKind::NOT) {
        uint64_t v = ~static_cast<uint64_t>(value);
        value = static_cast<int64_t>(v);
      }
      node = NumberNode::newInt(token, value);
      node->setType(this->typePool.get(TYPE::Int));
      return true;
    }
    break;
  }
  case NodeKind::StringExpr: {
    auto &exprNode = cast<StringExprNode>(*node);
    Token token = node->getToken();
    std::string value;
    for (auto &e : exprNode.refExprNodes()) {
      if (!this->applyConstFolding(e)) {
        break;
      }
      assert(isa<StringNode>(*e));
      value += cast<StringNode>(*e).getValue();
    }
    node = std::make_unique<StringNode>(token, std::move(value));
    node->setType(this->typePool.get(TYPE::String));
    return true;
  }
  case NodeKind::Embed: {
    auto &embedNode = cast<EmbedNode>(*node);
    if (!embedNode.getHandle() && this->applyConstFolding(embedNode.refExprNode())) {
      node = std::move(embedNode.refExprNode());
      return true;
    }
    break;
  }
  case NodeKind::Var: {
    assert(this->lexer);
    static const auto constMap = initConstVarMap();
    auto &varNode = cast<VarNode>(*node);
    Token token = varNode.getToken();
    std::string value;
    if (hasFlag(varNode.attr(), FieldAttribute::MOD_CONST)) {
      if (varNode.getVarName() == CVAR_SCRIPT_NAME) {
        value = this->lexer->getSourceName();
      } else if (varNode.getVarName() == CVAR_SCRIPT_DIR) {
        value = this->lexer->getScriptDir();
      } else {
        break;
      }
    } else {
      auto iter = constMap.find(varNode.getVarName());
      if (iter == constMap.end()) {
        break;
      }
      value = iter->second;
    }
    assert(varNode.getType().is(TYPE::String));
    node = std::make_unique<StringNode>(token, std::move(value));
    node->setType(this->typePool.get(TYPE::String));
    return true;
  }
  case NodeKind::New: {
    auto &newNode = cast<NewNode>(*node);
    if (newNode.getType().is(TYPE::Regex)) {
      if (!this->applyConstFolding(newNode.getArgsNode().refNodes()[0]) ||
          !this->applyConstFolding(newNode.getArgsNode().refNodes()[1])) {
        break;
      }
      assert(isa<StringNode>(*newNode.getArgsNode().refNodes()[0]));
      assert(isa<StringNode>(*newNode.getArgsNode().refNodes()[1]));
      Token token = newNode.getToken();
      std::string reStr = cast<StringNode>(*newNode.getArgsNode().refNodes()[0]).takeValue();
      std::string reFlag = cast<StringNode>(*newNode.getArgsNode().refNodes()[1]).takeValue();
      node = std::make_unique<RegexNode>(token, std::move(reStr), std::move(reFlag));
      this->checkTypeAsExpr(*node);
      return true;
    }
    break;
  }
  default:
    break;
  }
  RAISE_TC_ERROR(Constant, *node);
}

void TypeChecker::checkTypeAsBreakContinue(JumpNode &node) {
  if (this->fctx.loopLevel() == 0) {
    RAISE_TC_ERROR(InsideLoop, node);
  }

  if (this->fctx.finallyLevel() > this->fctx.loopLevel()) {
    RAISE_TC_ERROR(InsideFinally, node);
  }

  if (this->fctx.childLevel() > this->fctx.loopLevel()) {
    RAISE_TC_ERROR(InsideChild, node);
  }

  if (this->fctx.tryCatchLevel() > this->fctx.loopLevel()) {
    node.setLeavingBlock(true);
  }

  if (node.getExprNode().is(NodeKind::Empty)) {
    this->checkType(this->typePool.get(TYPE::Void), node.getExprNode());
  } else if (node.getOpKind() == JumpNode::BREAK) {
    this->checkTypeAsSomeExpr(node.getExprNode());
    this->breakGather.addJumpNode(&node);
  }
  assert(!node.getExprNode().isUntyped());
}

void TypeChecker::checkTypeAsReturn(JumpNode &node) {
  if (this->fctx.finallyLevel() > 0) {
    RAISE_TC_ERROR(InsideFinally, node);
  }

  if (this->fctx.childLevel() > 0) {
    RAISE_TC_ERROR(InsideChild, node);
  }

  auto *returnType = this->getCurrentReturnType();
  if (returnType == nullptr) {
    RAISE_TC_ERROR(InsideFunc, node);
  }
  auto &exprType = this->checkType(*returnType, node.getExprNode());
  if (exprType.isVoidType()) {
    if (!node.getExprNode().is(NodeKind::Empty)) {
      RAISE_TC_ERROR(NotNeedExpr, node.getExprNode());
    }
  }
}

void TypeChecker::visitJumpNode(JumpNode &node) {
  switch (node.getOpKind()) {
  case JumpNode::BREAK:
  case JumpNode::CONTINUE:
    this->checkTypeAsBreakContinue(node);
    break;
  case JumpNode::THROW: {
    if (this->fctx.finallyLevel() > 0) {
      RAISE_TC_ERROR(InsideFinally, node);
    }
    this->checkType(this->typePool.get(TYPE::Any), node.getExprNode());
    break;
  }
  case JumpNode::RETURN: {
    this->checkTypeAsReturn(node);
    break;
  }
  }
  node.setType(this->typePool.get(TYPE::Nothing));
}

void TypeChecker::visitCatchNode(CatchNode &node) {
  auto &exceptionType = this->checkTypeAsSomeExpr(node.getTypeNode());
  /**
   * not allow Void, Nothing and Option type.
   */
  if (exceptionType.isOptionType()) {
    RAISE_TC_ERROR(Unacceptable, node.getTypeNode(), exceptionType.getName());
  }

  {
    auto scope = this->intoBlock();
    /**
     * check type catch block
     */
    auto handle =
        this->addEntry(node, node.getExceptionName(), exceptionType, FieldAttribute::READ_ONLY);
    node.setAttribute(*handle);
    this->checkTypeWithCurrentScope(nullptr, node.getBlockNode());
  }
  node.setType(node.getBlockNode().getType());
}

void TypeChecker::visitTryNode(TryNode &node) {
  if (node.getCatchNodes().empty() && node.getFinallyNode() == nullptr) {
    RAISE_TC_ERROR(MeaninglessTry, node);
  }
  assert(node.getExprNode().is(NodeKind::Block));
  if (cast<BlockNode>(node.getExprNode()).getNodes().empty()) {
    RAISE_TC_ERROR(EmptyTry, node.getExprNode());
  }

  // check type try block
  const DSType *exprType = nullptr;
  {
    auto try1 = this->intoTry();
    exprType = &this->checkTypeExactly(node.getExprNode());
  }

  // check type catch block
  for (auto &c : node.getCatchNodes()) {
    auto try1 = this->intoTry();
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

  // check type finally block, may be empty node
  if (node.getFinallyNode() != nullptr) {
    auto finally1 = this->intoFinally();
    this->checkTypeWithCoercion(this->typePool.get(TYPE::Void), node.refFinallyNode());

    if (findInnerNode<BlockNode>(node.getFinallyNode())->getNodes().empty()) {
      RAISE_TC_ERROR(UselessBlock, *node.getFinallyNode());
    }
    if (node.getFinallyNode()->getType().isNothingType()) {
      RAISE_TC_ERROR(InsideFinally, *node.getFinallyNode());
    }
  }

  /**
   * verify catch block order
   */
  const int size = node.getCatchNodes().size();
  for (int i = 0; i < size - 1; i++) {
    auto &curType =
        findInnerNode<CatchNode>(node.getCatchNodes()[i].get())->getTypeNode().getType();
    auto &nextType =
        findInnerNode<CatchNode>(node.getCatchNodes()[i + 1].get())->getTypeNode().getType();
    if (curType.isSameOrBaseTypeOf(nextType)) {
      auto &nextNode = node.getCatchNodes()[i + 1];
      RAISE_TC_ERROR(Unreachable, *nextNode);
    }
  }
  node.setType(*exprType);
}

void TypeChecker::visitVarDeclNode(VarDeclNode &node) {
  const DSType *exprType = nullptr;
  FieldAttribute attr{};
  switch (node.getKind()) {
  case VarDeclNode::LET:
  case VarDeclNode::VAR:
    if (node.getKind() == VarDeclNode::LET) {
      setFlag(attr, FieldAttribute::READ_ONLY);
    }
    exprType = &this->checkTypeAsSomeExpr(*node.getExprNode());
    break;
  case VarDeclNode::IMPORT_ENV:
  case VarDeclNode::EXPORT_ENV:
    setFlag(attr, FieldAttribute::ENV);
    exprType = &this->typePool.get(TYPE::String);
    if (node.getExprNode() != nullptr) {
      this->checkType(*exprType, *node.getExprNode());
    }
    break;
  }

  auto handle = this->addEntry(node, node.getVarName(), *exprType, attr);
  node.setAttribute(*handle);
  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitAssignNode(AssignNode &node) {
  if (!isAssignable(node.getLeftNode())) {
    RAISE_TC_ERROR(Assignable, node.getLeftNode());
  }
  auto &leftNode = static_cast<AssignableNode &>(node.getLeftNode());
  auto &leftType = this->checkTypeAsExpr(leftNode);
  if (hasFlag(leftNode.attr(), FieldAttribute::READ_ONLY)) {
    RAISE_TC_ERROR(ReadOnly, leftNode);
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
    this->addEntry(node, node.toEnvCtxName(), this->typePool.get(TYPE::Any),
                   FieldAttribute::READ_ONLY);

    for (auto &e : node.getAssignNodes()) {
      auto &rightType = this->checkType(this->typePool.get(TYPE::String), e->getRightNode());
      assert(isa<VarNode>(e->getLeftNode()));
      auto &leftNode = cast<VarNode>(e->getLeftNode());
      auto *handle =
          this->addEntry(leftNode, leftNode.getVarName(), rightType, FieldAttribute::ENV);
      leftNode.setAttribute(*handle);
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

static void addReturnNodeToLast(BlockNode &blockNode, const TypePool &pool,
                                std::unique_ptr<Node> exprNode) {
  assert(!blockNode.isUntyped() && !blockNode.getType().isNothingType());
  assert(!exprNode->isUntyped());

  auto returnNode = JumpNode::newReturn(exprNode->getToken(), std::move(exprNode));
  returnNode->setType(pool.get(TYPE::Nothing));
  blockNode.setType(returnNode->getType());
  blockNode.addNode(std::move(returnNode));
}

void TypeChecker::visitFunctionNode(FunctionNode &node) {
  if (!this->isTopLevel()) { // only available toplevel scope
    RAISE_TC_ERROR(OutsideToplevel, node);
  }

  // resolve return type, param type
  auto &returnType = this->checkTypeExactly(node.getReturnTypeToken());
  unsigned int paramSize = node.getParamTypeNodes().size();
  std::vector<const DSType *> paramTypes(paramSize);
  for (unsigned int i = 0; i < paramSize; i++) {
    auto &type = this->checkTypeAsSomeExpr(*node.getParamTypeNodes()[i]);
    paramTypes[i] = &type;
  }

  // register function handle
  auto typeOrError = this->typePool.createFuncType(returnType, std::move(paramTypes));
  assert(typeOrError);
  auto &funcType = static_cast<const FunctionType &>(*std::move(typeOrError).take());
  node.setFuncType(funcType);
  auto handle = this->addEntry(node, node.getFuncName(), funcType,
                               FieldAttribute::FUNC_HANDLE | FieldAttribute::READ_ONLY);
  node.setVarIndex(handle->getIndex());

  {
    auto func = this->intoFunc(returnType);
    // register parameter
    for (unsigned int i = 0; i < paramSize; i++) {
      VarNode &paramNode = *node.getParamNodes()[i];
      auto fieldHandle = this->addEntry(paramNode, paramNode.getVarName(),
                                        funcType.getParamTypeAt(i), FieldAttribute());
      paramNode.setAttribute(*fieldHandle);
    }
    // check type func body
    this->checkTypeWithCurrentScope(node.getBlockNode());
    node.setMaxVarNum(this->curScope->getMaxLocalVarIndex());
  }

  // insert terminal node if not found
  BlockNode &blockNode = node.getBlockNode();
  if (returnType.isVoidType() && !blockNode.getType().isNothingType()) {
    auto emptyNode = std::make_unique<EmptyNode>();
    emptyNode->setType(this->typePool.get(TYPE::Void));
    addReturnNodeToLast(blockNode, this->typePool, std::move(emptyNode));
  }
  if (!blockNode.getType().isNothingType()) {
    RAISE_TC_ERROR(UnfoundReturn, blockNode);
  }

  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
  if (!this->isTopLevel()) { // only available toplevel scope
    RAISE_TC_ERROR(OutsideToplevel, node);
  }

  // register command name
  auto handle = this->addUdcEntry(node);
  node.setUdcIndex(handle->getIndex());

  {
    auto func = this->intoFunc(this->typePool.get(TYPE::Int)); // pseudo return type
    // register dummy parameter (for propagating command attr)
    this->addEntry(node, "%%attr", this->typePool.get(TYPE::Any), FieldAttribute::READ_ONLY);

    // register dummy parameter (for closing file descriptor)
    this->addEntry(node, "%%redir", this->typePool.get(TYPE::Any), FieldAttribute::READ_ONLY);

    // register special characters (@, #, 0, 1, ... 9)
    this->addEntry(node, "@", this->typePool.get(TYPE::StringArray), FieldAttribute::READ_ONLY);
    this->addEntry(node, "#", this->typePool.get(TYPE::Int), FieldAttribute::READ_ONLY);
    for (unsigned int i = 0; i < 10; i++) {
      this->addEntry(node, std::to_string(i), this->typePool.get(TYPE::String),
                     FieldAttribute::READ_ONLY);
    }

    // check type command body
    this->checkTypeWithCurrentScope(node.getBlockNode());
    node.setMaxVarNum(this->curScope->getMaxLocalVarIndex());
  }

  // insert return node if not found
  if (node.getBlockNode().getNodes().empty() ||
      !node.getBlockNode().getNodes().back()->getType().isNothingType()) {
    auto varNode = std::make_unique<VarNode>(Token{0, 1}, "?");
    this->checkTypeAsExpr(*varNode);
    addReturnNodeToLast(node.getBlockNode(), this->typePool, std::move(varNode));
  }

  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitInterfaceNode(InterfaceNode &node) {
  //    if(!this->isTopLevel()) {   // only available toplevel scope
  //        RAISE_TC_ERROR(OutsideToplevel, node);
  //    }
  RAISE_TC_ERROR(OutsideToplevel, node);
}

void TypeChecker::visitSourceNode(SourceNode &node) {
  assert(this->isTopLevel());

  // import module
  auto ret = this->curScope->importForeignHandles(node.getModType(), node.getName().empty());
  if (!ret.empty()) {
    RAISE_TC_ERROR(ConflictSymbol, node, ret.c_str(), node.getPathName().c_str());
  }
  if (!node.getName().empty()) { // scoped import
    auto handle = node.getModType().toHandle();

    // register actual module handle
    if (!this->curScope->defineAlias(std::string(node.getName()), handle)) {
      RAISE_TC_ERROR(DefinedSymbol, node, node.getName().c_str());
    }
    std::string cmdName = toCmdFullName(node.getName());
    if (!this->curScope->defineAlias(std::move(cmdName), handle)) { // for module subcommand
      RAISE_TC_ERROR(DefinedCmd, node, node.getName().c_str());
    }
    if (!this->curScope->defineTypeAlias(this->typePool, std::string(node.getName()),
                                         node.getModType())) {
      RAISE_TC_ERROR(DefinedTypeAlias, node, node.getName().c_str());
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
  auto &pathNode = node.getPathNode();
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
      RAISE_TC_ERROR(NoGlobDir, pathNode, path.c_str());
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
        RAISE_TC_ERROR(NoGlobMatch, pathNode, path.c_str());
      } else {
        RAISE_TC_ERROR(GlobRetLimit, pathNode, path.c_str());
      }
    }
  }
  node.setPathList(std::move(ret));
}

void TypeChecker::visitSourceListNode(SourceListNode &node) {
  if (!this->isTopLevel()) { // only available toplevel scope
    RAISE_TC_ERROR(OutsideToplevel, node);
  }
  bool isGlob = node.getPathNode().getGlobPathSize() > 0 && node.getName().empty();
  auto &exprType = this->typePool.get(isGlob ? TYPE::StringArray : TYPE::String);
  this->checkType(exprType, node.getPathNode());

  for (auto &e : node.getPathNode().refSegmentNodes()) {
    this->applyConstFolding(e);
    assert(isa<StringNode>(*e) || isa<WildCardNode>(*e));
    if (isa<StringNode>(*e)) {
      auto ref = StringRef(cast<StringNode>(*e).getValue());
      if (ref.hasNullChar()) {
        RAISE_TC_ERROR(NullInPath, node.getPathNode());
      }
    }
  }
  this->resolvePathList(node);
  node.setType(this->typePool.get(TYPE::Void));
}

void TypeChecker::visitCodeCompNode(CodeCompNode &node) {
  assert(this->ccHandler);
  switch (node.getKind()) {
  case CodeCompNode::VAR:
    this->ccHandler->addVarNameRequest(this->lexer->toName(node.getTypingToken()), this->curScope);
    break;
  case CodeCompNode::MEMBER: {
    assert(node.getExprNode());
    auto &recvType = this->checkTypeAsExpr(*node.getExprNode());
    this->ccHandler->addMemberRequest(recvType, this->lexer->toTokenText(node.getTypingToken()));
    break;
  }
  case CodeCompNode::TYPE: {
    const DSType *recvType = nullptr;
    if (node.getExprNode()) {
      recvType = &this->checkTypeExactly(*node.getExprNode());
      if (!recvType->isModType()) {
        RAISE_TC_ERROR(Required, *node.getExprNode(), "Module type", recvType->getName());
      }
    }
    this->ccHandler->addTypeNameRequest(this->lexer->toName(node.getTypingToken()), recvType,
                                        this->curScope);
    break;
  }
  }
  RAISE_TC_ERROR(Unreachable, node);
}

void TypeChecker::visitEmptyNode(EmptyNode &node) { node.setType(this->typePool.get(TYPE::Void)); }

static bool mayBeCmd(const Node &node) {
  if (node.is(NodeKind::Cmd)) {
    return true;
  }
  if (node.is(NodeKind::Pipeline)) {
    if (cast<const PipelineNode>(node).getNodes().back()->is(NodeKind::Cmd)) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<Node> TypeChecker::operator()(const DSType *prevType, std::unique_ptr<Node> &&node,
                                              IntrusivePtr<NameScope> global) {
  // set scope
  this->curScope = std::move(global);

  if (prevType != nullptr && prevType->isNothingType()) {
    RAISE_TC_ERROR(Unreachable, *node);
  }

  if (this->toplevelPrinting && this->curScope->inRootModule() && !mayBeCmd(*node)) {
    this->checkTypeExactly(*node);
    node = this->newPrintOpNode(std::move(node));
  } else {
    this->checkTypeWithCoercion(this->typePool.get(TYPE::Void), node); // pop stack top
  }
  return std::move(node);
}

} // namespace ydsh
