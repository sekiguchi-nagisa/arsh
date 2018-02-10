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

// #########################
// ##     BreakGather     ##
// #########################

void BreakGather::clear() {
    delete this->entry;
}

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


// ###########################
// ##     TypeGenerator     ##
// ###########################

DSType *TypeGenerator::toTypeImpl(TypeNode &node) {
    switch(node.typeKind) {
    case TypeNode::Base: {
        return &symbolTable.getTypeAndThrowIfUndefined(static_cast<BaseTypeNode&>(node).getTokenText());
    }
    case TypeNode::Reified: {
        auto &typeNode = static_cast<ReifiedTypeNode&>(node);
        unsigned int size = typeNode.getElementTypeNodes().size();
        auto &typeTemplate = symbolTable.getTypeTemplate(typeNode.getTemplate()->getTokenText());
        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = &this->toType(*typeNode.getElementTypeNodes()[i]);
        }
        return &symbolTable.createReifiedType(typeTemplate, std::move(elementTypes));
    }
    case TypeNode::Func: {
        auto &typeNode = static_cast<FuncTypeNode&>(node);
        auto &returnType = this->toType(*typeNode.getReturnTypeNode());
        unsigned int size = typeNode.getParamTypeNodes().size();
        std::vector<DSType *> paramTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            paramTypes[i] = &this->toType(*typeNode.getParamTypeNodes()[i]);
        }
        return &symbolTable.createFuncType(&returnType, std::move(paramTypes));
    }
    case TypeNode::DBusIface: {
        return &symbolTable.getDBusInterfaceType(static_cast<DBusIfaceTypeNode&>(node).getTokenText());
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
        return &symbolTable.createTupleType(std::move(types));
    }
    case TypeNode::TypeOf:
        if(this->checker == nullptr) {
            RAISE_TC_ERROR(DisallowTypeof, node);
        } else {
            auto &typeNode = static_cast<TypeOfNode&>(node);
            auto &type = this->checker->checkType(typeNode.getExprNode());
            if(type.isNothingType()) {
                RAISE_TC_ERROR(Unacceptable, *typeNode.getExprNode(), this->symbolTable.getTypeName(type));
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
    auto &type = this->symbolTable.createInterfaceType(node->getInterfaceName());

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

    node->setType(this->symbolTable.getVoidType());
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
        targetNode->accept(*this);
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
        if(!type.isNothingType() && unacceptableType != nullptr &&
                unacceptableType->isSameOrBaseTypeOf(type)) {
            RAISE_TC_ERROR(Unacceptable, *targetNode, this->symbolTable.getTypeName(type));
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

    RAISE_TC_ERROR(Required, *targetNode, this->symbolTable.getTypeName(*requiredType),
                   this->symbolTable.getTypeName(type));
}

void TypeChecker::checkTypeWithCurrentScope(DSType *requiredType, BlockNode *blockNode) {
    DSType *blockType = &this->symbolTable.getVoidType();
    for(auto iter = blockNode->refNodes().begin(); iter != blockNode->refNodes().end(); ++iter) {
        auto &targetNode = *iter;
        if(blockType->isNothingType()) {
            RAISE_TC_ERROR(Unreachable, *targetNode);
        }
        if(iter == blockNode->refNodes().end() - 1) {
            if(requiredType != nullptr) {
                this->checkTypeWithCoercion(*requiredType, targetNode);
            } else {
                this->checkType(nullptr, targetNode, nullptr);
            }
        } else {
            this->checkTypeWithCoercion(this->symbolTable.getVoidType(), targetNode);
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

bool TypeChecker::checkCoercion(const DSType &requiredType, DSType &targetType) {
    if(requiredType.isVoidType()) {  // pop stack top
        return true;
    }

    if(requiredType == this->symbolTable.getBooleanType()) {
        if(targetType.isOptionType()) {
            return true;
        }
        auto *handle = targetType.lookupMethodHandle(this->symbolTable, OP_BOOL);
        if(handle != nullptr) {
            return true;
        }
    }

    // int widening or float cast
    int targetPrecision = this->symbolTable.getIntPrecision(targetType);
    if(targetPrecision > SymbolTable::INVALID_PRECISION) {
        int requiredPrecision = this->symbolTable.getIntPrecision(requiredType);
        if(requiredType == this->symbolTable.getFloatType() && targetPrecision < SymbolTable::INT64_PRECISION) {
            return true;    // check int (except for Int64, Uint64) to float cast
        }
        if(targetPrecision < requiredPrecision && requiredPrecision <= SymbolTable::INT64_PRECISION) {
            return true;    // check int widening
        }
    }
    return false;
}

DSType& TypeChecker::resolveCoercionOfJumpValue() {
    auto &jumpNodes = this->breakGather.getJumpNodes();
    if(jumpNodes.empty()) {
        return this->symbolTable.getVoidType();
    }

    auto &firstType = jumpNodes[0]->getExprNode()->getType();
    assert(!firstType.isNothingType() && !firstType.isVoidType());

    for(auto &jumpNode : jumpNodes) {
        if(firstType != jumpNode->getExprNode()->getType()) {
            this->checkTypeWithCoercion(firstType, jumpNode->refExprNode());
        }
    }
    return this->symbolTable.createReifiedType(this->symbolTable.getOptionTemplate(), {&firstType});
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

    auto &type = this->checkType(this->symbolTable.getBaseFuncType(), &recvNode);
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

    DSType *type = handle->getFieldType(this->symbolTable);
    if(!type->isFuncType()) {
        if(this->symbolTable.getBaseFuncType() == *type) {
            RAISE_TC_ERROR(NotCallable, recvNode);
        } else {
            RAISE_TC_ERROR(Required, recvNode, this->symbolTable.getTypeName(this->symbolTable.getBaseFuncType()),
                           this->symbolTable.getTypeName(*type));
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

void TypeChecker::resolveCastOp(TypeOpNode &node) {
    auto &exprType = node.getExprNode()->getType();
    auto &targetType = node.getType();

    if(node.getType().isVoidType()) {
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
    int beforeIndex = this->symbolTable.getNumTypeIndex(exprType);
    int afterIndex = this->symbolTable.getNumTypeIndex(targetType);
    if(beforeIndex > -1 && afterIndex > -1) {
        assert(beforeIndex < 8 && afterIndex < 8);
        node.setOpKind(TypeOpNode::NUM_CAST);
        return;
    }

    if(exprType.isOptionType()) {
        if(targetType == this->symbolTable.getBooleanType()) {
            node.setOpKind(TypeOpNode::CHECK_UNWRAP);
            return;
        }
    } else  {
        if(targetType == this->symbolTable.getStringType()) {
            node.setOpKind(TypeOpNode::TO_STRING);
            return;
        }
        if(targetType == this->symbolTable.getBooleanType() &&
                exprType.lookupMethodHandle(this->symbolTable, OP_BOOL) != nullptr) {
            node.setOpKind(TypeOpNode::TO_BOOL);
            return;
        }
        if(!targetType.isNothingType() && exprType.isSameOrBaseTypeOf(targetType)) {
            node.setOpKind(TypeOpNode::CHECK_CAST);
            return;
        }
    }

    RAISE_TC_ERROR(CastOp, node, this->symbolTable.getTypeName(exprType), this->symbolTable.getTypeName(targetType));
}

TypeOpNode *TypeChecker::newTypedCastNode(Node *targetNode, DSType &type) {
    assert(!targetNode->isUntyped());
    auto *castNode = new TypeOpNode(targetNode, nullptr, TypeOpNode::NO_CAST);
    castNode->setType(type);
    return castNode;
}

Node* TypeChecker::newPrintOpNode(Node *node) {
    if(!node->getType().isVoidType() && !node->getType().isNothingType()) {
        auto *castNode = newTypedCastNode(node, this->symbolTable.getVoidType());
        castNode->setOpKind(TypeOpNode::PRINT);
        node = castNode;
    }
    return node;
}

void TypeChecker::convertToStringExpr(BinaryOpNode &node) {
    int needCast = 0;
    if(node.getLeftNode()->getType() != this->symbolTable.getStringType()) {
        needCast--;
    }
    if(node.getRightNode()->getType() != this->symbolTable.getStringType()) {
        needCast++;
    }

    // perform string cast
    if(needCast == -1) {
        this->resolveCoercion(this->symbolTable.getStringType(), node.refLeftNode());
    } else if(needCast == 1) {
        this->resolveCoercion(this->symbolTable.getStringType(), node.refRightNode());
    }

    auto *exprNode = new StringExprNode(node.getLeftNode()->getPos());
    exprNode->addExprNode(node.getLeftNode());
    exprNode->addExprNode(node.getRightNode());

    // assign null to prevent double free
    node.refLeftNode() = nullptr;
    node.refRightNode() = nullptr;

    node.setOptNode(exprNode);
}

// visitor api
void TypeChecker::visitTypeNode(TypeNode &node) {
    TypeGenerator(this->symbolTable, this).toType(node);
}

void TypeChecker::visitNumberNode(NumberNode &node) {
    switch(node.kind) {
    case NumberNode::Byte:
        node.setType(this->symbolTable.getByteType());
        break;
    case NumberNode::Int16:
        node.setType(this->symbolTable.getInt16Type());
        break;
    case NumberNode::Uint16:
        node.setType(this->symbolTable.getUint16Type());
        break;
    case NumberNode::Int32:
        node.setType(this->symbolTable.getInt32Type());
        break;
    case NumberNode::Uint32:
        node.setType(this->symbolTable.getUint32Type());
        break;
    case NumberNode::Int64:
        node.setType(this->symbolTable.getInt64Type());
        break;
    case NumberNode::Uint64:
        node.setType(this->symbolTable.getUint64Type());
        break;
    case NumberNode::Float:
        node.setType(this->symbolTable.getFloatType());
        break;
    case NumberNode::Signal:
        node.setType(this->symbolTable.getSignalType());
        break;
    }
}

void TypeChecker::visitStringNode(StringNode &node) {
    node.setType(node.isObjectPath() ? this->symbolTable.getObjectPathType() : this->symbolTable.getStringType());
}

void TypeChecker::visitStringExprNode(StringExprNode &node) {
    const unsigned int size = node.getExprNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        Node *exprNode = node.getExprNodes()[i];
        auto &exprType = this->checkType(exprNode);
        if(!this->symbolTable.getStringType().isSameOrBaseTypeOf(exprType)) { // call __INTERP__()
            std::string methodName(OP_INTERP);
            MethodHandle *handle = exprType.isOptionType() ? nullptr :
                                   exprType.lookupMethodHandle(this->symbolTable, methodName);
            if(handle == nullptr) { // if exprType is
                RAISE_TC_ERROR(UndefinedMethod, *exprNode, methodName.c_str());
            }

            auto *callNode = new MethodCallNode(exprNode, std::move(methodName));

            // check type argument
            this->checkTypeArgsNode(node, handle, callNode->refArgNodes());
            callNode->setHandle(handle);
            callNode->setType(*handle->getReturnType());

            node.setExprNode(i, callNode);
        }
    }
    node.setType(this->symbolTable.getStringType());
}

void TypeChecker::visitRegexNode(RegexNode &node) {
    node.setType(this->symbolTable.getRegexType());
}

void TypeChecker::visitArrayNode(ArrayNode &node) {
    unsigned int size = node.getExprNodes().size();
    assert(size != 0);
    Node *firstElementNode = node.getExprNodes()[0];
    auto &elementType = this->checkType(firstElementNode);

    for(unsigned int i = 1; i < size; i++) {
        this->checkTypeWithCoercion(elementType, node.refExprNodes()[i]);
    }

    auto &arrayTemplate = this->symbolTable.getArrayTemplate();
    std::vector<DSType *> elementTypes(1);
    elementTypes[0] = &elementType;
    node.setType(this->symbolTable.createReifiedType(arrayTemplate, std::move(elementTypes)));
}

void TypeChecker::visitMapNode(MapNode &node) {
    unsigned int size = node.getValueNodes().size();
    assert(size != 0);
    Node *firstKeyNode = node.getKeyNodes()[0];
    auto &keyType = this->checkType(this->symbolTable.getValueType(), firstKeyNode);
    Node *firstValueNode = node.getValueNodes()[0];
    auto &valueType = this->checkType(firstValueNode);

    for(unsigned int i = 1; i < size; i++) {
        this->checkTypeWithCoercion(keyType, node.refKeyNodes()[i]);
        this->checkTypeWithCoercion(valueType, node.refValueNodes()[i]);
    }

    auto &mapTemplate = this->symbolTable.getMapTemplate();
    std::vector<DSType *> elementTypes(2);
    elementTypes[0] = &keyType;
    elementTypes[1] = &valueType;
    node.setType(this->symbolTable.createReifiedType(mapTemplate, std::move(elementTypes)));
}

void TypeChecker::visitTupleNode(TupleNode &node) {
    unsigned int size = node.getNodes().size();
    std::vector<DSType *> types(size);
    for(unsigned int i = 0; i < size; i++) {
        types[i] = &this->checkType(node.getNodes()[i]);
    }
    node.setType(this->symbolTable.createTupleType(std::move(types)));
}

void TypeChecker::visitVarNode(VarNode &node) {
    FieldHandle *handle = this->symbolTable.lookupHandle(node.getVarName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedSymbol, node, node.getVarName().c_str());
    }

    node.setAttribute(handle);
    node.setType(*handle->getFieldType(this->symbolTable));
}

void TypeChecker::visitAccessNode(AccessNode &node) {
    auto &recvType = this->checkType(node.getRecvNode());
    FieldHandle *handle = recvType.lookupFieldHandle(this->symbolTable, node.getFieldName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedField, *node.getNameNode(), node.getFieldName().c_str());
    }

    node.setAttribute(handle);
    node.setType(*handle->getFieldType(this->symbolTable));
}

void TypeChecker::visitTypeOpNode(TypeOpNode &node) {
    auto &exprType = this->checkType(node.getExprNode());
    auto &targetType = this->toType(node.getTargetTypeNode());

    if(node.isCastOp()) {
        node.setType(targetType);
        this->resolveCastOp(node);
    } else {
        if(targetType.isSameOrBaseTypeOf(exprType)) {
            node.setOpKind(TypeOpNode::ALWAYS_TRUE);
        } else if(!exprType.isOptionType() && exprType.isSameOrBaseTypeOf(targetType)) {
            node.setOpKind(TypeOpNode::INSTANCEOF);
        } else {
            node.setOpKind(TypeOpNode::ALWAYS_FALSE);
        }
        node.setType(this->symbolTable.getBooleanType());
    }
}

void TypeChecker::visitUnaryOpNode(UnaryOpNode &node) {
    auto &exprType = this->checkType(node.getExprNode());
    if(node.isUnwrapOp()) {
        if(!exprType.isOptionType()) {
            RAISE_TC_ERROR(Required, *node.getExprNode(), "Option type", this->symbolTable.getTypeName(exprType));
        }
        node.setType(*static_cast<ReifiedType *>(&exprType)->getElementTypes()[0]);
    } else {
        if(exprType.isOptionType()) {
            this->resolveCoercion(this->symbolTable.getBooleanType(), node.refExprNode());
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

void TypeChecker::visitBinaryOpNode(BinaryOpNode &node) {
    if(node.getOp() == COND_AND || node.getOp() == COND_OR) {
        auto &booleanType = this->symbolTable.getBooleanType();
        this->checkTypeWithCoercion(booleanType, node.refLeftNode());
        if(node.getLeftNode()->getType().isNothingType()) {
            RAISE_TC_ERROR(Unreachable, *node.getRightNode());
        }

        this->checkTypeWithCoercion(booleanType, node.refRightNode());
        node.setType(booleanType);
        return;
    }

    if(node.getOp() == NULL_COALE) {
        auto &leftType = this->checkType(node.getLeftNode());
        if(!leftType.isOptionType()) {
            RAISE_TC_ERROR(Required, *node.getLeftNode(), "Option type", this->symbolTable.getTypeName(leftType));
        }
        auto &elementType = static_cast<ReifiedType &>(leftType).getElementTypes()[0];
        this->checkTypeWithCoercion(*elementType, node.refRightNode());
        node.setType(*elementType);
        return;
    }

    auto &leftType = this->checkType(node.getLeftNode());
    auto &rightType = this->checkType(node.getRightNode());

    // check referencial equality of func object
    if(leftType.isFuncType() && leftType == rightType
       && (node.getOp() == TokenKind::EQ || node.getOp() == TokenKind::NE)) {
        node.setType(this->symbolTable.getBooleanType());
        return;
    }

    // string concatenation
    if(node.getOp() == TokenKind::ADD &&
                (leftType == this->symbolTable.getStringType() || rightType == this->symbolTable.getStringType())) {
        this->convertToStringExpr(node);
        node.setType(this->checkType(node.getOptNode()));
        return;
    }

    int leftPrecision = this->symbolTable.getIntPrecision(leftType);
    int rightPrecision = this->symbolTable.getIntPrecision(rightType);

    // check int cats
    if(leftPrecision > SymbolTable::INVALID_PRECISION &&
       leftPrecision < SymbolTable::INT32_PRECISION &&
       rightPrecision > SymbolTable::INVALID_PRECISION &&
       rightPrecision < SymbolTable::INT32_PRECISION) {   // int widening
        this->resolveCoercion(this->symbolTable.getInt32Type(), node.refLeftNode());
        this->resolveCoercion(this->symbolTable.getInt32Type(), node.refRightNode());
    } else if(leftPrecision != rightPrecision && this->checkCoercion(rightType, leftType)) {    // cast left
        this->resolveCoercion(rightType, node.refLeftNode());
    }

    toMethodCall(node);
    node.setType(this->checkType(node.getOptNode()));
}

void TypeChecker::visitApplyNode(ApplyNode &node) {
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

void TypeChecker::visitMethodCallNode(MethodCallNode &node) {
    auto &recvType = this->checkType(node.getRecvNode());
    MethodHandle *handle = recvType.lookupMethodHandle(this->symbolTable, node.getMethodName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedMethod, node, node.getMethodName().c_str());
    }

    // check type argument
    this->checkTypeArgsNode(node, handle, node.refArgNodes());

    node.setHandle(handle);
    node.setType(*handle->getReturnType());
}

void TypeChecker::visitNewNode(NewNode &node) {
    auto &type = this->toType(node.getTargetTypeNode());
    if(type.isOptionType()) {
        unsigned int size = node.getArgNodes().size();
        if(size > 0) {
            RAISE_TC_ERROR(UnmatchParam, node, 0, size);
        }
    } else {
        MethodHandle *handle = type.getConstructorHandle(this->symbolTable);
        if(handle == nullptr) {
            RAISE_TC_ERROR(UndefinedInit, node, this->symbolTable.getTypeName(type));
        }

        this->checkTypeArgsNode(node, handle, node.refArgNodes());
    }

    node.setType(type);
}

void TypeChecker::visitCmdNode(CmdNode &node) {
    this->checkType(this->symbolTable.getStringType(), node.getNameNode());
    for(auto *argNode : node.getArgNodes()) {
        this->checkType(argNode);
    }
    if(node.getNameNode()->is(NodeKind::String)
       && static_cast<StringNode*>(node.getNameNode())->getValue() == "exit") {
        node.setType(this->symbolTable.getNothingType());
    } else {
        node.setType(this->symbolTable.getBooleanType());
    }
}

void TypeChecker::visitCmdArgNode(CmdArgNode &node) {
    const unsigned int size = node.getSegmentNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        Node *exprNode = node.getSegmentNodes()[i];
        auto &segmentType = this->checkType(exprNode);

        if(!this->symbolTable.getStringType().isSameOrBaseTypeOf(segmentType) &&
                !this->symbolTable.getStringArrayType().isSameOrBaseTypeOf(segmentType)) { // call __STR__ or __CMD__ARG
            // first try lookup __CMD_ARG__ method
            std::string methodName(OP_CMD_ARG);
            MethodHandle *handle = segmentType.lookupMethodHandle(this->symbolTable, methodName);

            if(handle == nullptr || (*handle->getReturnType() != this->symbolTable.getStringType() &&
                                     *handle->getReturnType() != this->symbolTable.getStringArrayType())) { // if not found, lookup __STR__
                methodName = OP_STR;
                handle = segmentType.isOptionType() ? nullptr :
                         segmentType.lookupMethodHandle(this->symbolTable, methodName);
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
            this->checkType(nullptr, exprNode, &this->symbolTable.getStringArrayType());
        }
    }
    node.setType(this->symbolTable.getAnyType());   //FIXME
}

void TypeChecker::visitRedirNode(RedirNode &node) {
    CmdArgNode *argNode = node.getTargetNode();

    // check UnixFD
    if(argNode->getSegmentNodes().size() == 1) {
        auto &type = this->checkType(argNode->getSegmentNodes()[0]);
        if(type == this->symbolTable.getUnixFDType() && !node.isHereStr()) {
            argNode->setType(type);
            node.setType(this->symbolTable.getAnyType());
            return;
        }
    }

    this->checkType(argNode);

    // not allow String Array type
    if(argNode->getSegmentNodes().size() == 1) {
        this->checkType(nullptr, argNode->getSegmentNodes()[0], &this->symbolTable.getStringArrayType());
    }

    node.setType(this->symbolTable.getAnyType());   //FIXME
}

void TypeChecker::visitPipelineNode(PipelineNode &node) {
    unsigned int size = node.getNodes().size();
    this->fctx.enterChild();
    for(unsigned int i = 0; i < size - 1; i++) {
        this->checkType(nullptr, node.getNodes()[i], nullptr);
    }
    this->fctx.leave();

    this->symbolTable.enterScope();

    // register pipeline state
    this->addEntryAndThrowIfDefined(node, "%%pipe", this->symbolTable.getAnyType(), FieldAttribute::READ_ONLY);
    auto &type = this->checkType(nullptr, node.getNodes()[size - 1], nullptr);

    node.setBaseIndex(this->symbolTable.curScope().getBaseIndex());
    this->symbolTable.exitScope();
    node.setType(type);
}

void TypeChecker::visitWithNode(WithNode &node) {
    this->symbolTable.enterScope();

    // register redir config
    this->addEntryAndThrowIfDefined(node, "%%redir", this->symbolTable.getAnyType(), FieldAttribute::READ_ONLY);

    auto &type = this->checkType(nullptr, node.getExprNode(), nullptr);
    for(auto &e : node.getRedirNodes()) {
        this->checkType(e);
    }

    node.setBaseIndex(this->symbolTable.curScope().getBaseIndex());
    this->symbolTable.exitScope();
    node.setType(type);
}

void TypeChecker::visitForkNode(ForkNode &node) {
    this->fctx.enterChild();
    this->checkType(nullptr, node.getExprNode(), node.isJob() ? &this->symbolTable.getJobType() : nullptr);
    this->fctx.leave();

    DSType *type = nullptr;
    switch(node.getOpKind()) {
    case ForkNode::SUB_STR:
        type = &this->symbolTable.getStringType();
        break;
    case ForkNode::SUB_ARRAY:
        type = &this->symbolTable.getStringArrayType();
        break;
    case ForkNode::BG:
    case ForkNode::COPROC:
    case ForkNode::DISOWN:
        type = &this->symbolTable.getJobType();
        break;
    }
    node.setType(*type);
}

void TypeChecker::visitAssertNode(AssertNode &node) {
    this->checkTypeWithCoercion(this->symbolTable.getBooleanType(), node.refCondNode());
    this->checkType(this->symbolTable.getStringType(), node.getMessageNode());
    node.setType(this->symbolTable.getVoidType());
}

void TypeChecker::visitBlockNode(BlockNode &node) {
    this->symbolTable.enterScope();
    this->checkTypeWithCurrentScope(nullptr, &node);
    this->symbolTable.exitScope();
}

void TypeChecker::visitTypeAliasNode(TypeAliasNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }

    TypeNode *typeToken = node.getTargetTypeNode();
    try {
        this->symbolTable.setAlias(node.getAlias(), this->toType(typeToken));
        node.setType(this->symbolTable.getVoidType());
    } catch(TypeLookupError &e) {
        throw TypeCheckError(typeToken->getToken(), e);
    }
}

void TypeChecker::visitLoopNode(LoopNode &node) {
    this->symbolTable.enterScope();

    this->checkTypeWithCoercion(this->symbolTable.getVoidType(), node.refInitNode());

    this->symbolTable.enterScope();

    if(node.getInitNode()->is(NodeKind::VarDecl)) {
        bool b = this->symbolTable.disallowShadowing(static_cast<VarDeclNode *>(node.getInitNode())->getVarName());
        (void) b;
        assert(b);
    }

    if(node.getCondNode() != nullptr) {
        this->checkTypeWithCoercion(this->symbolTable.getBooleanType(), node.refCondNode());
    }
    this->checkTypeWithCoercion(this->symbolTable.getVoidType(), node.refIterNode());

    this->enterLoop();
    this->checkTypeWithCurrentScope(node.getBlockNode());
    auto &type = this->resolveCoercionOfJumpValue();
    this->exitLoop();

    this->symbolTable.exitScope();
    this->symbolTable.exitScope();

    if(!node.getBlockNode()->getType().isNothingType()) {    // insert continue to block end
        auto *jumpNode = JumpNode::newContinue({0, 0});
        jumpNode->setType(this->symbolTable.getNothingType());
        jumpNode->getExprNode()->setType(this->symbolTable.getVoidType());
        node.getBlockNode()->addNode(jumpNode);
        node.getBlockNode()->setType(jumpNode->getType());
    }
    node.setType(type);
}

void TypeChecker::visitIfNode(IfNode &node) {
    this->checkTypeWithCoercion(this->symbolTable.getBooleanType(), node.refCondNode());
    auto &thenType = this->checkType(nullptr, node.getThenNode(), nullptr);
    auto &elseType = this->checkType(nullptr, node.getElseNode(), nullptr);

    if(thenType.isNothingType() && elseType.isNothingType()) {
        node.setType(thenType);
    } else if(thenType.isSameOrBaseTypeOf(elseType)) {
        node.setType(thenType);
    } else if(elseType.isSameOrBaseTypeOf(thenType)) {
        node.setType(elseType);
    } else if(this->checkCoercion(thenType, elseType)) {
        this->checkTypeWithCoercion(thenType, node.refElseNode());
        node.setType(thenType);
    } else if(this->checkCoercion(elseType, thenType)) {
        this->checkTypeWithCoercion(elseType, node.refThenNode());
        node.setType(elseType);
    } else {
        this->checkTypeWithCoercion(this->symbolTable.getVoidType(), node.refThenNode());
        this->checkTypeWithCoercion(this->symbolTable.getVoidType(), node.refElseNode());
        node.setType(this->symbolTable.getVoidType());
    }
}

void TypeChecker::checkTypeAsBreakContinue(JumpNode &node) {
    if(this->fctx.loopLevel() == 0) {
        RAISE_TC_ERROR(InsideLoop, node);
    }

    if(this->fctx.finallyLevel() > this->fctx.loopLevel()) {
        RAISE_TC_ERROR(InsideFinally, node);
    }

    if(this->fctx.childLevel() > this->fctx.loopLevel()) {
        RAISE_TC_ERROR(InsideChild, node);
    }

    if(this->fctx.tryCatchLevel() > this->fctx.loopLevel()) {
        node.setLeavingBlock(true);
    }

    if(node.getExprNode()->is(NodeKind::Empty)) {
        this->checkType(this->symbolTable.getVoidType(), node.getExprNode());
    } else if(node.getOpKind() == JumpNode::BREAK) {
        this->checkType(this->symbolTable.getAnyType(), node.getExprNode());
        this->breakGather.addJumpNode(&node);
    }
    assert(!node.getExprNode()->isUntyped());
}

void TypeChecker::checkTypeAsReturn(JumpNode &node) {
    if(this->fctx.finallyLevel() > 0) {
        RAISE_TC_ERROR(InsideFinally, node);
    }

    if(this->fctx.childLevel() > 0) {
        RAISE_TC_ERROR(InsideChild, node);
    }

    DSType *returnType = this->getCurrentReturnType();
    if(returnType == nullptr) {
        RAISE_TC_ERROR(InsideFunc, node);
    }
    auto &exprType = this->checkType(*returnType, node.getExprNode());
    if(exprType.isVoidType()) {
        if(!node.getExprNode()->is(NodeKind::Empty)) {
            RAISE_TC_ERROR(NotNeedExpr, node);
        }
    }
}

void TypeChecker::visitJumpNode(JumpNode &node) {
    switch(node.getOpKind()) {
    case JumpNode::BREAK:
    case JumpNode::CONTINUE:
        this->checkTypeAsBreakContinue(node);
        break;
    case JumpNode::THROW: {
        if(this->fctx.finallyLevel() > 0) {
            RAISE_TC_ERROR(InsideFinally, node);
        }
        this->checkType(this->symbolTable.getAnyType(), node.getExprNode());
        break;
    }
    case JumpNode::RETURN: {
        this->checkTypeAsReturn(node);
        break;
    }
    }
    node.setType(this->symbolTable.getNothingType());
}

void TypeChecker::visitCatchNode(CatchNode &node) {
    auto &exceptionType = this->toType(node.getTypeNode());
    if(!this->symbolTable.getAnyType().isSameOrBaseTypeOf(exceptionType) || exceptionType.isNothingType()) {
        RAISE_TC_ERROR(Unacceptable, *node.getTypeNode(), this->symbolTable.getTypeName(exceptionType));
    }

    /**
     * check type catch block
     */
    this->symbolTable.enterScope();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node.getExceptionName(), exceptionType, FieldAttribute::READ_ONLY);
    node.setAttribute(handle);
    this->checkTypeWithCurrentScope(nullptr, node.getBlockNode());
    this->symbolTable.exitScope();
    node.setType(node.getBlockNode()->getType());
}

void TypeChecker::visitTryNode(TryNode &node) {
    if(node.getCatchNodes().empty() && node.getFinallyNode() == nullptr) {
        RAISE_TC_ERROR(UselessTry, node);
    }
    assert(node.getExprNode()->getNodeKind() == NodeKind::Block);
    if(static_cast<BlockNode*>(node.getExprNode())->getNodes().empty()) {
        RAISE_TC_ERROR(EmptyTry, *node.getExprNode());
    }

    // check type try block
    this->fctx.enterTry();
    auto *exprType = &this->checkType(nullptr, node.getExprNode(), nullptr);
    this->fctx.leave();

    // check type catch block
    for(auto &c : node.getCatchNodes()) {
        this->fctx.enterTry();
        auto &catchType = this->checkType(nullptr, c, nullptr);
        this->fctx.leave();

        if(!exprType->isSameOrBaseTypeOf(catchType) && !this->checkCoercion(*exprType, catchType)) {
            exprType = &this->symbolTable.getVoidType();
        }
    }

    // perform coercion
    this->checkTypeWithCoercion(*exprType, node.refExprNode());
    for(auto &c : node.refCatchNodes()) {
        this->checkTypeWithCoercion(*exprType, c);
    }

    // check type finally block, may be empty node
    if(node.getFinallyNode() != nullptr) {
        this->fctx.enterFinally();
        this->checkTypeWithCoercion(this->symbolTable.getVoidType(), node.refFinallyNode());
        this->fctx.leave();

        if(findInnerNode<BlockNode>(node.getFinallyNode())->getNodes().empty()) {
            RAISE_TC_ERROR(UselessBlock, *node.getFinallyNode());
        }
        if(node.getFinallyNode()->getType().isNothingType()) {
            RAISE_TC_ERROR(InsideFinally, *node.getFinallyNode());
        }
    }

    /**
     * verify catch block order
     */
    const int size = node.getCatchNodes().size();
    for(int i = 0; i < size - 1; i++) {
        auto &curType = findInnerNode<CatchNode>(node.getCatchNodes()[i])->getTypeNode()->getType();
        auto &nextType = findInnerNode<CatchNode>(node.getCatchNodes()[i + 1])->getTypeNode()->getType();
        if(curType.isSameOrBaseTypeOf(nextType)) {
            auto &nextNode = node.getCatchNodes()[i + 1];
            RAISE_TC_ERROR(Unreachable, *nextNode);
        }
    }
    node.setType(*exprType);
}

void TypeChecker::visitVarDeclNode(VarDeclNode &node) {
    DSType *exprType = nullptr;
    FieldAttributes attr;
    switch(node.getKind()) {
    case VarDeclNode::CONST:
    case VarDeclNode::VAR:
        if(node.getKind() == VarDeclNode::CONST) {
            attr.set(FieldAttribute::READ_ONLY);
        }
        exprType = &this->checkType(node.getExprNode());
        if(exprType->isNothingType()) {
            RAISE_TC_ERROR(Unacceptable, *node.getExprNode(), this->symbolTable.getTypeName(*exprType));
        }
        break;
    case VarDeclNode::IMPORT_ENV:
    case VarDeclNode::EXPORT_ENV:
        attr.set(FieldAttribute::ENV);
        exprType = &this->symbolTable.getStringType();
        if(node.getExprNode() != nullptr) {
            this->checkType(*exprType, node.getExprNode());
        }
        break;
    }

    FieldHandle *handle = this->addEntryAndThrowIfDefined(node, node.getVarName(), *exprType, attr);
    node.setAttribute(handle);
    node.setType(this->symbolTable.getVoidType());
}

void TypeChecker::visitAssignNode(AssignNode &node) {
    if(!isAssignable(*node.getLeftNode())) {
        RAISE_TC_ERROR(Assignable, *node.getLeftNode());
    }
    auto *leftNode = static_cast<AssignableNode *>(node.getLeftNode());
    auto &leftType = this->checkType(leftNode);
    if(leftNode->attr().has(FieldAttribute::READ_ONLY)) {
        RAISE_TC_ERROR(ReadOnly, *leftNode);
    }

    if(leftNode->is(NodeKind::Access)) {
        node.setAttribute(AssignNode::FIELD_ASSIGN);
    }
    if(node.isSelfAssignment()) {
        assert(node.getRightNode()->is(NodeKind::BinaryOp));
        auto *opNode = static_cast<BinaryOpNode *>(node.getRightNode());
        opNode->getLeftNode()->setType(leftType);
        if(leftNode->is(NodeKind::Access)) {
            static_cast<AccessNode *>(leftNode)->setAdditionalOp(AccessNode::DUP_RECV);
        }
        auto &rightType = this->checkType(node.getRightNode());
        if(leftType != rightType) { // convert right hand-side type to left type
            this->resolveCoercion(leftType, node.refRightNode());
        }
    } else {
        this->checkTypeWithCoercion(leftType, node.refRightNode());
    }

    node.setType(this->symbolTable.getVoidType());
}

void TypeChecker::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
    auto &recvType = this->checkType(node.getRecvNode());
    auto &indexType = this->checkType(node.getIndexNode());

    node.setRecvType(recvType);
    node.setIndexType(indexType);

    auto &elementType = this->checkType(node.getGetterNode());
    static_cast<BinaryOpNode *>(node.getRightNode())->getLeftNode()->setType(elementType);

    // convert right hand-side type to element type
    auto &rightType = this->checkType(node.getRightNode());
    if(elementType != rightType) {
        this->resolveCoercion(elementType, node.refRightNode());
    }

    node.getSetterNode()->getArgNodes()[1]->setType(elementType);
    this->checkType(this->symbolTable.getVoidType(), node.getSetterNode());

    node.setType(this->symbolTable.getVoidType());
}

void TypeChecker::visitFunctionNode(FunctionNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }

    // resolve return type, param type
    auto &returnType = this->toType(node.getReturnTypeToken());
    unsigned int paramSize = node.getParamTypeNodes().size();
    std::vector<DSType *> paramTypes(paramSize);
    for(unsigned int i = 0; i < paramSize; i++) {
        auto *type = &this->toType(node.getParamTypeNodes()[i]);
        if(type->isVoidType() || type->isNothingType()) {
            RAISE_TC_ERROR(Unacceptable, *node.getParamTypeNodes()[i], this->symbolTable.getTypeName(*type));
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
    if(returnType.isVoidType() && !blockNode->getType().isNothingType()) {
        auto *emptyNode = new EmptyNode();
        emptyNode->setType(this->symbolTable.getVoidType());
        blockNode->addReturnNodeToLast(this->symbolTable, emptyNode);
    }
    if(!blockNode->getType().isNothingType()) {
        RAISE_TC_ERROR(UnfoundReturn, *blockNode);
    }

    node.setType(this->symbolTable.getVoidType());
}

void TypeChecker::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }

    // register command name
    auto pair = this->symbolTable.registerUdc(node.getName(), this->symbolTable.getAnyType());
    if(pair.second == SymbolError::DEFINED) {
        RAISE_TC_ERROR(DefinedCmd, node, node.getName().c_str());
    }
    node.setUdcIndex(pair.first->getFieldIndex());

    this->pushReturnType(this->symbolTable.getIntType());    // pseudo return type
    this->symbolTable.enterFunc();
    this->symbolTable.enterScope();

    // register dummy parameter (for propagating command attr)
    this->addEntryAndThrowIfDefined(node, "%%attr", this->symbolTable.getAnyType(), FieldAttribute::READ_ONLY);

    // register dummy parameter (for closing file descriptor)
    this->addEntryAndThrowIfDefined(node, "%%redir", this->symbolTable.getAnyType(), FieldAttribute::READ_ONLY);

    // register special characters (@, #, 0, 1, ... 9)
    this->addEntryAndThrowIfDefined(node, "@", this->symbolTable.getStringArrayType(), FieldAttribute::READ_ONLY);
    this->addEntryAndThrowIfDefined(node, "#", this->symbolTable.getInt32Type(), FieldAttribute::READ_ONLY);
    for(unsigned int i = 0; i < 10; i++) {
        this->addEntryAndThrowIfDefined(node, std::to_string(i), this->symbolTable.getStringType(), FieldAttribute::READ_ONLY);
    }

    // check type command body
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->symbolTable.exitScope();

    node.setMaxVarNum(this->symbolTable.getMaxVarIndex());
    this->symbolTable.exitFunc();
    this->popReturnType();

    // insert return node if not found
    if(node.getBlockNode()->getNodes().empty() ||
            !node.getBlockNode()->getNodes().back()->getType().isNothingType()) {
        VarNode *varNode = new VarNode({0, 1}, "?");
        this->checkType(varNode);
        node.getBlockNode()->addReturnNodeToLast(this->symbolTable, varNode);
    }

    node.setType(this->symbolTable.getVoidType());
}

void TypeChecker::visitInterfaceNode(InterfaceNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }
    TypeGenerator(this->symbolTable, this).resolveInterface(&node);
}

void TypeChecker::visitEmptyNode(EmptyNode &node) {
    node.setType(this->symbolTable.getVoidType());
}

DSType* TypeChecker::operator()(const DSType *prevType, Node *&node) {
    if(prevType != nullptr && prevType->isNothingType()) {
        RAISE_TC_ERROR(Unreachable, *node);
    }

    auto kind = node->getNodeKind();
    if(kind == NodeKind::Pipeline || kind == NodeKind::Cmd) {
        this->checkTypeWithCoercion(this->symbolTable.getVoidType(), node);  // pop stack top
    } else if(this->toplevelPrinting) {
        this->checkType(nullptr, node, nullptr);
        node = this->newPrintOpNode(node);
    } else {
        this->checkTypeWithCoercion(this->symbolTable.getVoidType(), node);
    }

    // check empty block
    if(node->is(NodeKind::Block) && static_cast<BlockNode *>(node)->getNodes().empty()) {
        RAISE_TC_ERROR(UselessBlock, *node);
    }

    return &node->getType();
}

} // namespace ydsh
