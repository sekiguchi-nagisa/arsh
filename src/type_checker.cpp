/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#include <vector>

#include "symbol.h"
#include "object.h"
#include "type_checker.h"

namespace ydsh {

// ###########################
// ##     TypeGenerator     ##
// ###########################

DSType *TypeGenerator::toTypeImpl(TypeNode &node) {
    switch(node.typeKind) {
    case TypeNode::Base: {
        return &pool.getTypeAndThrowIfUndefined(static_cast<BaseTypeNode&>(node).getTokenText());
    }
    case TypeNode::Reified: {
        auto &typeNode = static_cast<ReifiedTypeNode&>(node);
        unsigned int size = typeNode.getElementTypeNodes().size();
        auto &typeTemplate = pool.getTypeTemplate(typeNode.getTemplate()->getTokenText());
        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = &this->toType(*typeNode.getElementTypeNodes()[i]);
        }
        return &pool.createReifiedType(typeTemplate, std::move(elementTypes));
    }
    case TypeNode::Func: {
        auto &typeNode = static_cast<FuncTypeNode&>(node);
        auto &returnType = this->toType(*typeNode.getReturnTypeNode());
        unsigned int size = typeNode.getParamTypeNodes().size();
        std::vector<DSType *> paramTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            paramTypes[i] = &this->toType(*typeNode.getParamTypeNodes()[i]);
        }
        return &pool.createFuncType(&returnType, std::move(paramTypes));
    }
    case TypeNode::DBusIface: {
        return &pool.getDBusInterfaceType(static_cast<DBusIfaceTypeNode&>(node).getTokenText());
    }
    case TypeNode::Return: {
        auto &typeNode = static_cast<ReturnTypeNode&>(node);
        unsigned int size = typeNode.getTypeNodes().size();
        if(size == 1) {
            return &this->toType(*typeNode.getTypeNodes()[0]);
        }

        std::vector<DSType *> types(size);
        for(unsigned int i = 0; i < size; i++) {
            types[i] = &this->toType(*typeNode.getTypeNodes()[i]);
        }
        return &pool.createTupleType(std::move(types));
    }
    case TypeNode::TypeOf:
        if(this->checker == nullptr) {
            RAISE_TC_ERROR(DisallowTypeof, node);
        } else {
            auto &typeNode = static_cast<TypeOfNode&>(node);
            auto &type = this->checker->checkType(typeNode.getExprNode());
            if(type.isBottomType()) {
                RAISE_TC_ERROR(Unacceptable, *typeNode.getExprNode(), this->pool.getTypeName(type).c_str());
            }
            return &type;
        }
    }
    return nullptr; // for suppressing gcc warning (normally unreachable).
}

DSType& TypeGenerator::toType(TypeNode &node) {
    try {
        auto *type = this->toTypeImpl(node);
        node.setType(*type);
        return *type;
    } catch(TypeLookupError &e) {
        throw TypeCheckError(node.getToken(), e);
    }
}

DSType& TypeGenerator::resolveInterface(InterfaceNode *node) {
    auto &type = this->pool.createInterfaceType(node->getInterfaceName());

    // create field handle
    unsigned int fieldSize = node->getFieldDeclNodes().size();
    for(unsigned int i = 0; i < fieldSize; i++) {
        VarDeclNode *fieldDeclNode = node->getFieldDeclNodes()[i];
        auto &fieldType = this->toType(*node->getFieldTypeNodes()[i]);
        FieldHandle *handle = type.newFieldHandle(
                fieldDeclNode->getVarName(), fieldType, fieldDeclNode->isReadOnly());
        if(handle == nullptr) {
            RAISE_TC_ERROR(DefinedField, *fieldDeclNode, fieldDeclNode->getVarName().c_str());
        }
    }

    // create method handle
    for(FunctionNode *funcNode : node->getMethodDeclNodes()) {
        MethodHandle *handle = type.newMethodHandle(funcNode->getName());
        handle->setRecvType(type);
        auto *retTypeNode = funcNode->getReturnTypeToken();
        handle->setReturnType(this->toType(*retTypeNode));
        // resolve multi return
        if(retTypeNode->typeKind == TypeNode::Return && static_cast<ReturnTypeNode *>(retTypeNode)->hasMultiReturn()) {
            handle->setAttribute(MethodHandle::MULTI_RETURN);
        }

        unsigned int paramSize = funcNode->getParamNodes().size();
        for(unsigned int i = 0; i < paramSize; i++) {
            handle->addParamType(this->toType(*funcNode->getParamTypeNodes()[i]));
        }
    }

    node->setType(this->pool.getVoidType());
    return type;
}


// #########################
// ##     TypeChecker     ##
// #########################

DSType &TypeChecker::checkType(DSType *requiredType, Node *targetNode,
                               DSType *unacceptableType, CoercionKind &kind) {
    /**
     * if target node is expr node and type is null,
     * try type check.
     */
    if(targetNode->isUntyped()) {
        this->visitingDepth++;
        this->dispatch(requiredType, *targetNode);
        this->visitingDepth--;
    }

    /**
     * after type checking, Node is not untyped.
     */
    assert(!targetNode->isUntyped());
    auto &type = targetNode->getType();

    /**
     * do not try type matching.
     */
    if(requiredType == nullptr) {
        if(!type.isBottomType() && unacceptableType != nullptr &&
                unacceptableType->isSameOrBaseTypeOf(type)) {
            RAISE_TC_ERROR(Unacceptable, *targetNode, this->typePool.getTypeName(type).c_str());
        }
        return type;
    }

    /**
     * try type matching.
     */
    if(requiredType->isSameOrBaseTypeOf(type)) {
        return type;
    }

    /**
     * check coercion
     */
    if(kind == CoercionKind::INVALID_COERCION && this->checkCoercion(*requiredType, type)) {
        kind = CoercionKind::PERFORM_COERCION;
        return type;
    }

    RAISE_TC_ERROR(Required, *targetNode, this->typePool.getTypeName(*requiredType).c_str(),
               this->typePool.getTypeName(type).c_str());
}

void TypeChecker::checkTypeWithCurrentScope(DSType *requiredType, BlockNode *blockNode) {
    DSType *blockType = &this->typePool.getVoidType();
    for(auto iter = blockNode->refNodes().begin(); iter != blockNode->refNodes().end(); ++iter) {
        auto &targetNode = *iter;
        if(blockType->isBottomType()) {
            RAISE_TC_ERROR(Unreachable, *targetNode);
        }
        if(iter == blockNode->refNodes().end() - 1) {
            if(requiredType != nullptr) {
                this->checkTypeWithCoercion(*requiredType, targetNode);
            } else {
                this->checkType(nullptr, targetNode, nullptr);
            }
        } else {
            this->checkTypeWithCoercion(this->typePool.getVoidType(), targetNode);
        }
        blockType = &targetNode->getType();

        // check empty block
        if(targetNode->is(NodeKind::Block) && static_cast<BlockNode *>(targetNode)->getNodes().empty()) {
            RAISE_TC_ERROR(UselessBlock, *targetNode);
        }
    }

    // set base index of current scope
    blockNode->setBaseIndex(this->symbolTable.curScope().getBaseIndex());
    blockNode->setVarSize(this->symbolTable.curScope().getVarSize());
    blockNode->setMaxVarSize(this->symbolTable.getMaxVarIndex() - blockNode->getBaseIndex());

    assert(blockType != nullptr);
    blockNode->setType(*blockType);
}

void TypeChecker::checkTypeWithCoercion(DSType &requiredType, Node * &targetNode) {
    CoercionKind kind = CoercionKind::INVALID_COERCION;
    this->checkType(&requiredType, targetNode, nullptr, kind);
    if(kind != CoercionKind::INVALID_COERCION && kind != CoercionKind::NOP) {
        this->resolveCoercion(requiredType, targetNode);
    }
}

bool TypeChecker::checkCoercion(const DSType &requiredType, const DSType &targetType) {
    if(requiredType.isVoidType()) {  // pop stack top
        return true;
    }

    // option type checking
    if(targetType.isOptionType() && requiredType == this->typePool.getBooleanType()) {
        return true;
    }

    // int widening or float cast
    int targetPrecision = this->typePool.getIntPrecision(targetType);
    if(targetPrecision > TypePool::INVALID_PRECISION) {
        int requiredPrecision = this->typePool.getIntPrecision(requiredType);
        if(requiredType == this->typePool.getFloatType() && targetPrecision < TypePool::INT64_PRECISION) {
            return true;    // check int (except for Int64, Uint64) to float cast
        } else if(targetPrecision < requiredPrecision && requiredPrecision <= TypePool::INT64_PRECISION) {
            return true;    // check int widening
        }
    }
    return false;
}

FieldHandle *TypeChecker::addEntryAndThrowIfDefined(Node &node, const std::string &symbolName, DSType &type,
                                                    FieldAttributes attribute) {
    auto pair = this->symbolTable.registerHandle(symbolName, type, attribute);
    switch(pair.second) {
    case SymbolError::DUMMY:
        break;
    case SymbolError::DEFINED:
        RAISE_TC_ERROR(DefinedSymbol, node, symbolName.c_str());
    case SymbolError::LIMIT:
        RAISE_TC_ERROR(LocalLimit, node);
    }
    return pair.first;
}

// for ApplyNode type checking
HandleOrFuncType TypeChecker::resolveCallee(Node &recvNode) {
    if(recvNode.is(NodeKind::Var)) {
        return this->resolveCallee(*static_cast<VarNode *>(&recvNode));
    }

    auto &type = this->checkType(this->typePool.getBaseFuncType(), &recvNode);
    if(!type.isFuncType()) {
        RAISE_TC_ERROR(NotCallable, recvNode);
    }
    return HandleOrFuncType(static_cast<FunctionType *>(&type));
}

HandleOrFuncType TypeChecker::resolveCallee(VarNode &recvNode) {
    FieldHandle *handle = this->symbolTable.lookupHandle(recvNode.getVarName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedSymbol, recvNode, recvNode.getVarName().c_str());
    }
    recvNode.setAttribute(handle);

    if(handle->attr().has(FieldAttribute::FUNC_HANDLE)) {
        return HandleOrFuncType(static_cast<FunctionHandle *>(handle));
    }

    DSType *type = handle->getFieldType(this->typePool);
    if(!type->isFuncType()) {
        if(this->typePool.getBaseFuncType() == *type) {
            RAISE_TC_ERROR(NotCallable, recvNode);
        } else {
            RAISE_TC_ERROR(Required, recvNode, this->typePool.getTypeName(this->typePool.getBaseFuncType()).c_str(),
                           this->typePool.getTypeName(*type).c_str());
        }
    }
    return HandleOrFuncType(static_cast<FunctionType *>(type));
}

void TypeChecker::checkTypeArgsNode(Node &node, MethodHandle *handle, std::vector<Node *> &argNodes) {
    unsigned int argSize = argNodes.size();
    do {
        // check param size
        unsigned int paramSize = handle->getParamTypes().size();
        if(paramSize != argSize) {
            RAISE_TC_ERROR(UnmatchParam, node, paramSize, argSize);
        }

        // check type each node
        for(unsigned int i = 0; i < paramSize; i++) {
            this->checkTypeWithCoercion(*handle->getParamTypes()[i], argNodes[i]);
        }
    } while(handle->getNext() != nullptr);  //FIXME: method overloading
}

void TypeChecker::resolveCastOp(TypeOpNode &node, bool allowVoidCast) {
    auto &exprType = node.getExprNode()->getType();
    auto &targetType = node.getType();

    if(allowVoidCast && node.getType().isVoidType()) {
        node.setOpKind(TypeOpNode::TO_VOID);
        return;
    }

    /**
     * nop
     */
    if(targetType.isSameOrBaseTypeOf(exprType)) {
        return;
    }

    /**
     * number cast
     */
    int beforeIndex = this->typePool.getNumTypeIndex(exprType);
    int afterIndex = this->typePool.getNumTypeIndex(targetType);
    if(beforeIndex > -1 && afterIndex > -1) {
        assert(beforeIndex < 8 && afterIndex < 8);
        node.setOpKind(TypeOpNode::NUM_CAST);
        return;
    }

    if(exprType.isOptionType()) {
        if(targetType == this->typePool.getBooleanType()) {
            node.setOpKind(TypeOpNode::CHECK_UNWRAP);
            return;
        }
    } else  {
        if(targetType == this->typePool.getStringType()) {
            node.setOpKind(TypeOpNode::TO_STRING);
            return;
        }
        if(!targetType.isBottomType() && exprType.isSameOrBaseTypeOf(targetType)) {
            node.setOpKind(TypeOpNode::CHECK_CAST);
            return;
        }
    }

    RAISE_TC_ERROR(CastOp, node, this->typePool.getTypeName(exprType).c_str(), this->typePool.getTypeName(targetType).c_str());
}

TypeOpNode *TypeChecker::newTypedCastNode(Node *targetNode, DSType &type) {
    assert(!targetNode->isUntyped());
    TypeOpNode *castNode = new TypeOpNode(targetNode, nullptr, TypeOpNode::NO_CAST);
    castNode->setType(type);
    this->resolveCastOp(*castNode, true);
    return castNode;
}

Node* TypeChecker::newPrintOpNode(Node *node) {
    if(!node->getType().isVoidType() && !node->getType().isBottomType()) {
        auto *castNode = newTypedCastNode(node, this->typePool.getVoidType());
        castNode->setOpKind(TypeOpNode::PRINT);
        node = castNode;
    }
    return node;
}

void TypeChecker::convertToStringExpr(BinaryOpNode &node) {
    int needCast = 0;
    if(node.getLeftNode()->getType() != this->typePool.getStringType()) {
        needCast--;
    }
    if(node.getRightNode()->getType() != this->typePool.getStringType()) {
        needCast++;
    }

    // perform string cast
    if(needCast == -1) {
        node.refLeftNode() = this->newTypedCastNode(node.getLeftNode(), this->typePool.getStringType());
    } else if(needCast == 1) {
        node.refRightNode() = this->newTypedCastNode(node.getRightNode(), this->typePool.getStringType());
    }

    auto *exprNode = new StringExprNode(node.getLeftNode()->getPos());
    exprNode->addExprNode(node.getLeftNode());
    exprNode->addExprNode(node.getRightNode());

    // assign null to prevent double free
    node.refLeftNode() = nullptr;
    node.refRightNode() = nullptr;

    node.setOptNode(exprNode);
}

void TypeChecker::dispatch(DSType *requiredType, Node &node) {

    switch(node.getNodeKind()) {
#define GEN_CASE(OP) case NodeKind::OP: this->visit ## OP ## Node(requiredType, static_cast<OP ## Node &>(node)); break;
    EACH_NODE_KIND(GEN_CASE)
#undef GEN_CASE
    }
}


// visitor api
void TypeChecker::visitTypeNode(DSType *, TypeNode &node) {
    TypeGenerator(this->typePool, this).toType(node);
}

void TypeChecker::visitNumberNode(DSType *, NumberNode &node) {
    switch(node.kind) {
    case NumberNode::Byte:
        node.setType(this->typePool.getByteType());
        break;
    case NumberNode::Int16:
        node.setType(this->typePool.getInt16Type());
        break;
    case NumberNode::Uint16:
        node.setType(this->typePool.getUint16Type());
        break;
    case NumberNode::Int32:
        node.setType(this->typePool.getInt32Type());
        break;
    case NumberNode::Uint32:
        node.setType(this->typePool.getUint32Type());
        break;
    case NumberNode::Int64:
        node.setType(this->typePool.getInt64Type());
        break;
    case NumberNode::Uint64:
        node.setType(this->typePool.getUint64Type());
        break;
    case NumberNode::Float:
        node.setType(this->typePool.getFloatType());
        break;
    case NumberNode::Signal:
        node.setType(this->typePool.getSignalType());
        break;
    }
}

void TypeChecker::visitStringNode(DSType *, StringNode &node) {
    node.setType(node.isObjectPath() ? this->typePool.getObjectPathType() : this->typePool.getStringType());
}

void TypeChecker::visitStringExprNode(DSType *, StringExprNode &node) {
    const unsigned int size = node.getExprNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        Node *exprNode = node.getExprNodes()[i];
        auto &exprType = this->checkType(exprNode);
        if(!this->typePool.getStringType().isSameOrBaseTypeOf(exprType)) { // call __INTERP__()
            std::string methodName(OP_INTERP);
            MethodHandle *handle = exprType.isOptionType() ? nullptr :
                                   exprType.lookupMethodHandle(this->typePool, methodName);
            if(handle == nullptr) { // if exprType is
                RAISE_TC_ERROR(UndefinedMethod, *exprNode, methodName.c_str());
            }

            MethodCallNode *callNode = new MethodCallNode(exprNode, std::move(methodName));

            // check type argument
            this->checkTypeArgsNode(node, handle, callNode->refArgNodes());
            callNode->setHandle(handle);
            callNode->setType(*handle->getReturnType());

            node.setExprNode(i, callNode);
        }
    }
    node.setType(this->typePool.getStringType());
}

void TypeChecker::visitRegexNode(DSType *, RegexNode &node) {
    node.setType(this->typePool.getRegexType());
}

void TypeChecker::visitArrayNode(DSType *, ArrayNode &node) {
    unsigned int size = node.getExprNodes().size();
    assert(size != 0);
    Node *firstElementNode = node.getExprNodes()[0];
    auto &elementType = this->checkType(firstElementNode);

    for(unsigned int i = 1; i < size; i++) {
        this->checkTypeWithCoercion(elementType, node.refExprNodes()[i]);
    }

    auto &arrayTemplate = this->typePool.getArrayTemplate();
    std::vector<DSType *> elementTypes(1);
    elementTypes[0] = &elementType;
    node.setType(this->typePool.createReifiedType(arrayTemplate, std::move(elementTypes)));
}

void TypeChecker::visitMapNode(DSType *, MapNode &node) {
    unsigned int size = node.getValueNodes().size();
    assert(size != 0);
    Node *firstKeyNode = node.getKeyNodes()[0];
    auto &keyType = this->checkType(this->typePool.getValueType(), firstKeyNode);
    Node *firstValueNode = node.getValueNodes()[0];
    auto &valueType = this->checkType(firstValueNode);

    for(unsigned int i = 1; i < size; i++) {
        this->checkTypeWithCoercion(keyType, node.refKeyNodes()[i]);
        this->checkTypeWithCoercion(valueType, node.refValueNodes()[i]);
    }

    auto &mapTemplate = this->typePool.getMapTemplate();
    std::vector<DSType *> elementTypes(2);
    elementTypes[0] = &keyType;
    elementTypes[1] = &valueType;
    node.setType(this->typePool.createReifiedType(mapTemplate, std::move(elementTypes)));
}

void TypeChecker::visitTupleNode(DSType *, TupleNode &node) {
    unsigned int size = node.getNodes().size();
    std::vector<DSType *> types(size);
    for(unsigned int i = 0; i < size; i++) {
        types[i] = &this->checkType(node.getNodes()[i]);
    }
    node.setType(this->typePool.createTupleType(std::move(types)));
}

void TypeChecker::visitVarNode(DSType *, VarNode &node) {
    FieldHandle *handle = this->symbolTable.lookupHandle(node.getVarName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedSymbol, node, node.getVarName().c_str());
    }

    node.setAttribute(handle);
    node.setType(*handle->getFieldType(this->typePool));
}

void TypeChecker::visitAccessNode(DSType *, AccessNode &node) {
    auto &recvType = this->checkType(node.getRecvNode());
    FieldHandle *handle = recvType.lookupFieldHandle(this->typePool, node.getFieldName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedField, *node.getNameNode(), node.getFieldName().c_str());
    }

    node.setAttribute(handle);
    node.setType(*handle->getFieldType(this->typePool));
}

void TypeChecker::visitTypeOpNode(DSType *, TypeOpNode &node) {
    auto &exprType = this->checkType(node.getExprNode());
    auto &targetType = this->toType(node.getTargetTypeNode());

    if(node.isCastOp()) {
        node.setType(targetType);
        this->resolveCastOp(node, false);
    } else {
        if(targetType.isSameOrBaseTypeOf(exprType)) {
            node.setOpKind(TypeOpNode::ALWAYS_TRUE);
        } else if(!exprType.isOptionType() && exprType.isSameOrBaseTypeOf(targetType)) {
            node.setOpKind(TypeOpNode::INSTANCEOF);
        } else {
            node.setOpKind(TypeOpNode::ALWAYS_FALSE);
        }
        node.setType(this->typePool.getBooleanType());
    }
}

void TypeChecker::visitUnaryOpNode(DSType *, UnaryOpNode &node) {
    auto &exprType = this->checkType(node.getExprNode());
    if(node.isUnwrapOp()) {
        if(!exprType.isOptionType()) {
            RAISE_TC_ERROR(Required, *node.getExprNode(), "Option type", this->typePool.getTypeName(exprType).c_str());
        }
        node.setType(*static_cast<ReifiedType *>(&exprType)->getElementTypes()[0]);
    } else {
        if(exprType.isOptionType()) {
            node.setExprNode(this->newTypedCastNode(node.getExprNode(), this->typePool.getBooleanType()));
        }
        MethodCallNode *applyNode = node.createApplyNode();
        node.setType(this->checkType(applyNode));
    }
}

static void toMethodCall(BinaryOpNode &node) {
    auto *methodCallNode = new MethodCallNode(node.getLeftNode(), resolveBinaryOpName(node.getOp()));
    methodCallNode->refArgNodes().push_back(node.getRightNode());

    // assign null to prevent double free
    node.refLeftNode() = nullptr;
    node.refRightNode() = nullptr;

    node.setOptNode(methodCallNode);
}

void TypeChecker::visitBinaryOpNode(DSType *, BinaryOpNode &node) {
    if(node.getOp() == COND_AND || node.getOp() == COND_OR) {
        auto &booleanType = this->typePool.getBooleanType();
        this->checkTypeWithCoercion(booleanType, node.refLeftNode());
        this->checkTypeWithCoercion(booleanType, node.refRightNode());
        node.setType(booleanType);
        return;
    }

    if(node.getOp() == NULL_COALE) {
        auto &leftType = this->checkType(node.getLeftNode());
        if(!leftType.isOptionType()) {
            RAISE_TC_ERROR(Required, *node.getLeftNode(), "Option type", this->typePool.getTypeName(leftType).c_str());
        }
        auto &elementType = static_cast<ReifiedType &>(leftType).getElementTypes()[0];
        this->checkTypeWithCoercion(*elementType, node.refRightNode());
        node.setType(*elementType);
        return;
    }

    auto &leftType = this->checkType(node.getLeftNode());
    auto &rightType = this->checkType(node.getRightNode());

    // string concatenation
    if(node.getOp() == TokenKind::PLUS &&
                (leftType == this->typePool.getStringType() || rightType == this->typePool.getStringType())) {
        this->convertToStringExpr(node);
        node.setType(this->checkType(node.getOptNode()));
        return;
    }

    int leftPrecision = this->typePool.getIntPrecision(leftType);
    int rightPrecision = this->typePool.getIntPrecision(rightType);

    // check int cats
    if(leftPrecision > TypePool::INVALID_PRECISION &&
       leftPrecision < TypePool::INT32_PRECISION &&
       rightPrecision > TypePool::INVALID_PRECISION &&
       rightPrecision < TypePool::INT32_PRECISION) {   // int widening
        this->resolveCoercion(this->typePool.getInt32Type(), node.refLeftNode());
        this->resolveCoercion(this->typePool.getInt32Type(), node.refRightNode());
    } else if(leftPrecision != rightPrecision && this->checkCoercion(rightType, leftType)) {    // cast left
        this->resolveCoercion(rightType, node.refLeftNode());
    }

    toMethodCall(node);
    node.setType(this->checkType(node.getOptNode()));
}

void TypeChecker::visitApplyNode(DSType *, ApplyNode &node) {
    /**
     * resolve handle
     */
    Node *exprNode = node.getExprNode();
    HandleOrFuncType hf = this->resolveCallee(*exprNode);

    // resolve param types
    auto &paramTypes = hf.treatAsHandle() ? hf.getHandle()->getParamTypes() :
                       hf.getFuncType()->getParamTypes();
    unsigned int size = paramTypes.size();
    unsigned int argSize = node.getArgNodes().size();
    // check param size
    if(size != argSize) {
        RAISE_TC_ERROR(UnmatchParam, node, size, argSize);
    }

    // check type each node
    for(unsigned int i = 0; i < size; i++) {
        this->checkTypeWithCoercion(*paramTypes[i], node.refArgNodes()[i]);
    }

    // set return type
    node.setType(hf.treatAsHandle() ? *hf.getHandle()->getReturnType() : *hf.getFuncType()->getReturnType());
}

void TypeChecker::visitMethodCallNode(DSType *, MethodCallNode &node) {
    auto &recvType = this->checkType(node.getRecvNode());
    MethodHandle *handle = recvType.lookupMethodHandle(this->typePool, node.getMethodName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedMethod, node, node.getMethodName().c_str());
    }

    // check type argument
    this->checkTypeArgsNode(node, handle, node.refArgNodes());

    node.setHandle(handle);
    node.setType(*handle->getReturnType());
}

void TypeChecker::visitNewNode(DSType *, NewNode &node) {
    auto &type = this->toType(node.getTargetTypeNode());
    if(type.isOptionType()) {
        unsigned int size = node.getArgNodes().size();
        if(size > 0) {
            RAISE_TC_ERROR(UnmatchParam, node, 0, size);
        }
    } else {
        MethodHandle *handle = type.getConstructorHandle(this->typePool);
        if(handle == nullptr) {
            RAISE_TC_ERROR(UndefinedInit, node, this->typePool.getTypeName(type).c_str());
        }

        this->checkTypeArgsNode(node, handle, node.refArgNodes());
    }

    node.setType(type);
}

void TypeChecker::visitCmdNode(DSType *, CmdNode &node) {
    this->checkType(this->typePool.getStringType(), node.getNameNode());
    for(auto *argNode : node.getArgNodes()) {
        this->checkType(argNode);
    }
    node.setType(this->typePool.getBooleanType());
}

void TypeChecker::visitCmdArgNode(DSType *, CmdArgNode &node) {
    const unsigned int size = node.getSegmentNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        Node *exprNode = node.getSegmentNodes()[i];
        auto &segmentType = this->checkType(exprNode);

        if(!this->typePool.getStringType().isSameOrBaseTypeOf(segmentType) &&
                !this->typePool.getStringArrayType().isSameOrBaseTypeOf(segmentType)) { // call __STR__ or __CMD__ARG
            // first try lookup __CMD_ARG__ method
            std::string methodName(OP_CMD_ARG);
            MethodHandle *handle = segmentType.lookupMethodHandle(this->typePool, methodName);

            if(handle == nullptr || (*handle->getReturnType() != this->typePool.getStringType() &&
                                     *handle->getReturnType() != this->typePool.getStringArrayType())) { // if not found, lookup __STR__
                methodName = OP_STR;
                handle = segmentType.isOptionType() ? nullptr :
                         segmentType.lookupMethodHandle(this->typePool, methodName);
                if(handle == nullptr) {
                    RAISE_TC_ERROR(UndefinedMethod, *exprNode, methodName.c_str());
                }
            }

            // create MethodCallNode and check type
            MethodCallNode *callNode = new MethodCallNode(exprNode, std::move(methodName));
            this->checkTypeArgsNode(node, handle, callNode->refArgNodes());
            callNode->setHandle(handle);
            callNode->setType(*handle->getReturnType());

            // overwrite segmentNode
            node.setSegmentNode(i, callNode);
        }
    }

    // not allow String Array type
    if(node.getSegmentNodes().size() > 1) {
        for(Node *exprNode : node.getSegmentNodes()) {
            this->checkType(nullptr, exprNode, &this->typePool.getStringArrayType());
        }
    }
    node.setType(this->typePool.getAnyType());   //FIXME
}

void TypeChecker::visitRedirNode(DSType *, RedirNode &node) {
    CmdArgNode *argNode = node.getTargetNode();
    this->checkType(argNode);

    // not allow String Array type
    if(argNode->getSegmentNodes().size() == 1) {
        this->checkType(nullptr, argNode->getSegmentNodes()[0], &this->typePool.getStringArrayType());
    }

    node.setType(this->typePool.getAnyType());   //FIXME
}

void TypeChecker::visitPipelineNode(DSType *, PipelineNode &node) {
    for(auto *cmdNode : node.getNodes()) {
        this->checkType(cmdNode);
    }
    node.setType(this->typePool.getBooleanType());
}

void TypeChecker::visitSubstitutionNode(DSType *, SubstitutionNode &node) {
    this->checkType(nullptr, node.getExprNode(), nullptr);
    if(node.isStrExpr()) {
        node.setType(this->typePool.getStringType());
    } else {
        node.setType(this->typePool.getStringArrayType());
    }
}

void TypeChecker::visitWithNode(DSType *requiredType, WithNode &node) {
    this->symbolTable.enterScope();

    // register redir config
    this->addEntryAndThrowIfDefined(node, "%%redir", this->typePool.getAnyType(), FieldAttribute::READ_ONLY);

    auto &type = this->checkType(requiredType != nullptr && requiredType->isVoidType() ?
                                 nullptr : requiredType, node.getExprNode(), nullptr);
    for(auto &e : node.getRedirNodes()) {
        this->checkType(e);
    }

    node.setBaseIndex(this->symbolTable.curScope().getBaseIndex());
    this->symbolTable.exitScope();
    node.setType(type);
}

void TypeChecker::visitAssertNode(DSType *, AssertNode &node) {
    this->checkTypeWithCoercion(this->typePool.getBooleanType(), node.refCondNode());
    this->checkType(this->typePool.getStringType(), node.getMessageNode());
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitBlockNode(DSType *requiredType, BlockNode &node) {
    this->symbolTable.enterScope();
    this->checkTypeWithCurrentScope(requiredType, &node);
    this->symbolTable.exitScope();
}

void TypeChecker::visitJumpNode(DSType *, JumpNode &node) {
    if(this->fctx.loopLevel() == 0) {
        RAISE_TC_ERROR(InsideLoop, node);
    }

    if(this->fctx.finallyLevel() > this->fctx.loopLevel()) {
        RAISE_TC_ERROR(InsideFinally, node);
    }

    if(this->fctx.tryCatchLevel() > this->fctx.loopLevel()) {
        node.setLeavingBlock(true);
    }
    node.setType(this->typePool.getBottomType());
}

void TypeChecker::visitTypeAliasNode(DSType *, TypeAliasNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }

    TypeNode *typeToken = node.getTargetTypeNode();
    try {
        this->typePool.setAlias(node.getAlias(), this->toType(typeToken));
        node.setType(this->typePool.getVoidType());
    } catch(TypeLookupError &e) {
        throw TypeCheckError(typeToken->getToken(), e);
    }
}

void TypeChecker::visitLoopNode(DSType *, LoopNode &node) {
    this->symbolTable.enterScope();

    this->checkTypeWithCoercion(this->typePool.getVoidType(), node.refInitNode());

    this->symbolTable.enterScope();

    if(node.getInitNode()->is(NodeKind::VarDecl)) {
        bool b = this->symbolTable.disallowShadowing(static_cast<VarDeclNode *>(node.getInitNode())->getVarName());
        (void) b;
        assert(b);
    }
    this->checkTypeWithCoercion(this->typePool.getBooleanType(), node.refCondNode());
    this->checkTypeWithCoercion(this->typePool.getVoidType(), node.refIterNode());

    this->enterLoop();
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->exitLoop();

    this->symbolTable.exitScope();
    this->symbolTable.exitScope();

    if(!node.getBlockNode()->getType().isBottomType()) {    // insert continue to block end
        auto *jumpNode = new JumpNode({0, 0}, false);
        jumpNode->setType(this->typePool.getBottomType());
        node.getBlockNode()->addNode(jumpNode);
        node.getBlockNode()->setType(jumpNode->getType());
    }
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitIfNode(DSType *requiredType, IfNode &node) {
    this->checkTypeWithCoercion(this->typePool.getBooleanType(), node.refCondNode());
    auto &thenType = this->checkType(nullptr, node.getThenNode(), nullptr);
    auto &elseType = this->checkType(nullptr, node.getElseNode(), nullptr);

    if(thenType.isBottomType() && elseType.isBottomType()) {
        node.setType(thenType);
    } else if(requiredType != nullptr && requiredType->isVoidType()) {
        this->checkTypeWithCoercion(this->typePool.getVoidType(), node.refThenNode());
        this->checkTypeWithCoercion(this->typePool.getVoidType(), node.refElseNode());
        node.setType(this->typePool.getVoidType());
    } else if(thenType.isSameOrBaseTypeOf(elseType)) {
        node.setType(thenType);
    } else if(elseType.isSameOrBaseTypeOf(thenType)) {
        node.setType(elseType);
    } else if(this->checkCoercion(thenType, elseType)) {
        this->checkTypeWithCoercion(thenType, node.refElseNode());
        node.setType(thenType);
    } else {
        this->checkTypeWithCoercion(elseType, node.refThenNode());
        node.setType(elseType);
    }
}

void TypeChecker::visitReturnNode(DSType *, ReturnNode &node) {
    if(this->fctx.finallyLevel() > 0) {
        RAISE_TC_ERROR(InsideFinally, node);
    }

    DSType *returnType = this->getCurrentReturnType();
    if(returnType == nullptr) {
        RAISE_TC_ERROR(InsideFunc, node);
    }
    auto &exprType = this->checkType(*returnType, node.getExprNode());
    if(exprType == this->typePool.getVoidType()) {
        if(!node.getExprNode()->is(NodeKind::Empty)) {
            RAISE_TC_ERROR(NotNeedExpr, node);
        }
    }
    node.setType(this->typePool.getBottomType());
}

void TypeChecker::visitThrowNode(DSType *, ThrowNode &node) {
    if(this->fctx.finallyLevel() > 0) {
        RAISE_TC_ERROR(InsideFinally, node);
    }

    this->checkType(this->typePool.getAnyType(), node.getExprNode());
    node.setType(this->typePool.getBottomType());
}

void TypeChecker::visitCatchNode(DSType *, CatchNode &node) {
    auto &exceptionType = this->toType(node.getTypeNode());
    if(!this->typePool.getAnyType().isSameOrBaseTypeOf(exceptionType)) {
        RAISE_TC_ERROR(Required, *node.getTypeNode(), this->typePool.getTypeName(this->typePool.getAnyType()).c_str(),
                       this->typePool.getTypeName(exceptionType).c_str());
    }

    /**
     * check type catch block
     */
    this->symbolTable.enterScope();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node.getExceptionName(), exceptionType, FieldAttribute::READ_ONLY);
    node.setAttribute(handle);
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->symbolTable.exitScope();
    node.setType(node.getBlockNode()->getType());
}

void TypeChecker::visitTryNode(DSType *, TryNode &node) {
    if(node.getCatchNodes().empty() && node.getFinallyNode() == nullptr) {
        RAISE_TC_ERROR(UselessTry, node);
    }
    if(node.getBlockNode()->getNodes().empty()) {
        RAISE_TC_ERROR(EmptyTry, *node.getBlockNode());
    }

    this->fctx.enterTry();
    this->checkType(this->typePool.getVoidType(), node.getBlockNode());
    this->fctx.leave();

    // check type catch block
    for(CatchNode *c : node.getCatchNodes()) {
        this->fctx.enterTry();
        this->checkType(this->typePool.getVoidType(), c);
        this->fctx.leave();
    }

    // check type finally block, may be empty node
    if(node.getFinallyNode() != nullptr) {
        this->fctx.enterFinally();
        this->checkType(this->typePool.getVoidType(), node.getFinallyNode());
        this->fctx.leave();

        if(node.getFinallyNode()->getNodes().empty()) {
            RAISE_TC_ERROR(UselessBlock, *node.getFinallyNode());
        }
    }

    /**
     * verify catch block order
     */
    const int size = node.getCatchNodes().size();
    for(int i = 0; i < size - 1; i++) {
        DSType &curType = node.getCatchNodes()[i]->getTypeNode()->getType();
        CatchNode *nextNode = node.getCatchNodes()[i + 1];
        DSType &nextType = nextNode->getTypeNode()->getType();
        if(curType.isSameOrBaseTypeOf(nextType)) {
            RAISE_TC_ERROR(Unreachable, *nextNode);
        }
    }

    // check if terminal node
    bool terminal = node.getBlockNode()->getType().isBottomType();
    for(int i = 0; i < size && terminal; i++) {
        terminal = node.getCatchNodes()[i]->getType().isBottomType();
    }
    node.setType(terminal ? this->typePool.getBottomType() : this->typePool.getVoidType());
}

void TypeChecker::visitVarDeclNode(DSType *, VarDeclNode &node) {
    DSType *exprType = nullptr;
    FieldAttributes attr;
    switch(node.getKind()) {
    case VarDeclNode::CONST:
    case VarDeclNode::VAR:
        if(node.getKind() == VarDeclNode::CONST) {
            attr.set(FieldAttribute::READ_ONLY);
        }
        exprType = &this->checkType(node.getExprNode());
        if(exprType->isBottomType()) {
            RAISE_TC_ERROR(Unacceptable, *node.getExprNode(), this->typePool.getTypeName(*exprType).c_str());
        }
        break;
    case VarDeclNode::IMPORT_ENV:
    case VarDeclNode::EXPORT_ENV:
        attr.set(FieldAttribute::ENV);
        exprType = &this->typePool.getStringType();
        if(node.getExprNode() != nullptr) {
            this->checkType(*exprType, node.getExprNode());
        }
        break;
    }

    FieldHandle *handle = this->addEntryAndThrowIfDefined(node, node.getVarName(), *exprType, attr);
    node.setAttribute(handle);
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitAssignNode(DSType *, AssignNode &node) {
    AssignableNode *leftNode = dynamic_cast<AssignableNode *>(node.getLeftNode());
    if(leftNode == nullptr) {
        RAISE_TC_ERROR(Assignable, *node.getLeftNode());
    }

    auto &leftType = this->checkType(leftNode);
    if(leftNode->attr().has(FieldAttribute::READ_ONLY)) {
        RAISE_TC_ERROR(ReadOnly, *leftNode);
    }

    if(leftNode->is(NodeKind::Access)) {
        node.setAttribute(AssignNode::FIELD_ASSIGN);
    }
    if(node.isSelfAssignment()) {
        assert(node.getRightNode()->is(NodeKind::BinaryOp));
        BinaryOpNode *opNode = static_cast<BinaryOpNode *>(node.getRightNode());
        opNode->getLeftNode()->setType(leftType);
        if(leftNode->is(NodeKind::Access)) {
            static_cast<AccessNode *>(leftNode)->setAdditionalOp(AccessNode::DUP_RECV);
        }
        auto &rightType = this->checkType(node.getRightNode());
        if(leftType != rightType) { // convert right hand-side type to left type
            node.refRightNode() = this->newTypedCastNode(node.refRightNode(), leftType);
        }
    } else {
        this->checkTypeWithCoercion(leftType, node.refRightNode());
    }

    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitElementSelfAssignNode(DSType *, ElementSelfAssignNode &node) {
    auto &recvType = this->checkType(node.getRecvNode());
    auto &indexType = this->checkType(node.getIndexNode());

    node.setRecvType(recvType);
    node.setIndexType(indexType);

    auto &elementType = this->checkType(node.getGetterNode());
    static_cast<BinaryOpNode *>(node.getRightNode())->getLeftNode()->setType(elementType);

    // convert right hand-side type to element type
    auto &rightType = this->checkType(node.getRightNode());
    if(elementType != rightType) {
        node.refRightNode() = this->newTypedCastNode(node.refRightNode(), elementType);
    }

    node.getSetterNode()->getArgNodes()[1]->setType(elementType);
    this->checkType(this->typePool.getVoidType(), node.getSetterNode());

    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitFunctionNode(DSType *, FunctionNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }

    // resolve return type, param type
    auto &returnType = this->toType(node.getReturnTypeToken());
    unsigned int paramSize = node.getParamTypeNodes().size();
    std::vector<DSType *> paramTypes(paramSize);
    for(unsigned int i = 0; i < paramSize; i++) {
        auto *type = &this->toType(node.getParamTypeNodes()[i]);
        if(type->isVoidType()) {
            RAISE_TC_ERROR(Unacceptable, *node.getParamTypeNodes()[i], this->typePool.getTypeName(*type).c_str());
        }
        paramTypes[i] = type;
    }

    // register function handle
    auto pair = this->symbolTable.registerFuncHandle(node.getName(), returnType, paramTypes);
    if(pair.second == SymbolError::DEFINED) {
        RAISE_TC_ERROR(DefinedSymbol, node, node.getName().c_str());
    }
    node.setVarIndex(pair.first->getFieldIndex());

    // prepare type checking
    this->pushReturnType(returnType);
    this->symbolTable.enterFunc();
    this->symbolTable.enterScope();

    // register parameter
    for(unsigned int i = 0; i < paramSize; i++) {
        VarNode *paramNode = node.getParamNodes()[i];
        FieldHandle *fieldHandle = this->addEntryAndThrowIfDefined(
                *paramNode, paramNode->getVarName(), *paramTypes[i], FieldAttributes());
        paramNode->setAttribute(fieldHandle);
    }

    // check type func body
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->symbolTable.exitScope();

    node.setMaxVarNum(this->symbolTable.getMaxVarIndex());
    this->symbolTable.exitFunc();
    this->popReturnType();

    // insert terminal node if not found
    BlockNode *blockNode = node.getBlockNode();
    if(returnType.isVoidType() && !blockNode->getType().isBottomType()) {
        EmptyNode *emptyNode = new EmptyNode();
        emptyNode->setType(this->typePool.getVoidType());
        blockNode->addReturnNodeToLast(this->typePool, emptyNode);
    }
    if(!blockNode->getType().isBottomType()) {
        RAISE_TC_ERROR(UnfoundReturn, *blockNode);
    }

    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitUserDefinedCmdNode(DSType *, UserDefinedCmdNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }

    // register command name
    auto pair = this->symbolTable.registerUdc(node.getName(), this->typePool.getAnyType());
    if(pair.second == SymbolError::DEFINED) {
        RAISE_TC_ERROR(DefinedCmd, node, node.getName().c_str());
    }
    node.setUdcIndex(pair.first->getFieldIndex());

    this->pushReturnType(this->typePool.getIntType());    // pseudo return type
    this->symbolTable.enterFunc();
    this->symbolTable.enterScope();

    // register dummy parameter (for closing file descriptor)
    this->addEntryAndThrowIfDefined(node, "%%redir", this->typePool.getAnyType(), FieldAttribute::READ_ONLY);

    // register special characters (@, #, 0, 1, ... 9)
    this->addEntryAndThrowIfDefined(node, "@", this->typePool.getStringArrayType(), FieldAttribute::READ_ONLY);
    this->addEntryAndThrowIfDefined(node, "#", this->typePool.getInt32Type(), FieldAttribute::READ_ONLY);
    for(unsigned int i = 0; i < 10; i++) {
        this->addEntryAndThrowIfDefined(node, std::to_string(i), this->typePool.getStringType(), FieldAttribute::READ_ONLY);
    }

    // check type command body
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->symbolTable.exitScope();

    node.setMaxVarNum(this->symbolTable.getMaxVarIndex());
    this->symbolTable.exitFunc();
    this->popReturnType();

    // insert return node if not found
    if(node.getBlockNode()->getNodes().empty() ||
            !node.getBlockNode()->getNodes().back()->getType().isBottomType()) {
        VarNode *varNode = new VarNode({0, 1}, "?");
        this->checkType(varNode);
        node.getBlockNode()->addReturnNodeToLast(this->typePool, varNode);
    }

    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitInterfaceNode(DSType *, InterfaceNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }
    TypeGenerator(this->typePool, this).resolveInterface(&node);
}

void TypeChecker::visitEmptyNode(DSType *, EmptyNode &node) {
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitRootNode(DSType *, RootNode &node) {
    this->symbolTable.commit();
    this->typePool.commit();
    this->fctx.clear();

    bool prevIsTerminal = false;
    for(auto &targetNode : node.refNodes()) {
        if(prevIsTerminal) {
            RAISE_TC_ERROR(Unreachable, *targetNode);
        }
        auto kind = targetNode->getNodeKind();
        if(kind == NodeKind::Pipeline || kind == NodeKind::Cmd) {
            this->checkTypeWithCoercion(this->typePool.getVoidType(), targetNode);  // pop stack top
        } else if(this->toplevelPrinting) {
            this->checkType(nullptr, targetNode, nullptr);
            targetNode = this->newPrintOpNode(targetNode);
        } else {
            this->checkTypeWithCoercion(this->typePool.getVoidType(), targetNode);
        }
        prevIsTerminal = targetNode->getType().isBottomType();

        // check empty block
        if(targetNode->is(NodeKind::Block) && static_cast<BlockNode *>(targetNode)->getNodes().empty()) {
            RAISE_TC_ERROR(UselessBlock, *targetNode);
        }
    }

    node.setMaxVarNum(this->symbolTable.getMaxVarIndex());
    node.setMaxGVarNum(this->symbolTable.getMaxGVarIndex());
    node.setType(this->typePool.getVoidType());
}

} // namespace ydsh
