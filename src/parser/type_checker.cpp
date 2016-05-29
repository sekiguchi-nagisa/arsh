/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#include "../core/symbol.h"
#include "../core/object.h"
#include "type_checker.h"

namespace ydsh {
namespace parser {

// ###########################
// ##     TypeGenerator     ##
// ###########################

DSType &TypeChecker::TypeGenerator::generateTypeAndThrow(TypeNode *typeNode) throw(TypeCheckError) {
    try {
        return this->generateType(typeNode);
    } catch(TypeLookupError &e) {
        throw TypeCheckError(typeNode->getToken(), e);
    }
}

void TypeChecker::TypeGenerator::visitDefault(Node &) {
    fatal("unsupported\n");
}

void TypeChecker::TypeGenerator::visitBaseTypeNode(BaseTypeNode &typeNode) {
    DSType &type = this->pool.getTypeAndThrowIfUndefined(typeNode.getTokenText());
    typeNode.setType(type);
}

void TypeChecker::TypeGenerator::visitReifiedTypeNode(ReifiedTypeNode &typeNode) {
    unsigned int size = typeNode.getElementTypeNodes().size();
    auto &typeTemplate = this->pool.getTypeTemplate(typeNode.getTemplate()->getTokenText());
    std::vector<DSType *> elementTypes(size);
    for(unsigned int i = 0; i < size; i++) {
        elementTypes[i] = &this->generateType(typeNode.getElementTypeNodes()[i]);
    }
    DSType &type = this->pool.createReifiedType(typeTemplate, std::move(elementTypes));
    typeNode.setType(type);
}

void TypeChecker::TypeGenerator::visitFuncTypeNode(FuncTypeNode &typeNode) {
    auto &returnType = this->generateType(typeNode.getReturnTypeNode());
    unsigned int size = typeNode.getParamTypeNodes().size();
    std::vector<DSType *> paramTypes(size);
    for(unsigned int i = 0; i < size; i++) {
        paramTypes[i] = &this->generateType(typeNode.getParamTypeNodes()[i]);
    }
    DSType &type = this->pool.createFuncType(&returnType, std::move(paramTypes));
    typeNode.setType(type);
}

void TypeChecker::TypeGenerator::visitDBusIfaceTypeNode(DBusIfaceTypeNode &typeNode) {
    DSType &type = this->pool.getDBusInterfaceType(typeNode.getTokenText());
    typeNode.setType(type);
}

void TypeChecker::TypeGenerator::visitReturnTypeNode(ReturnTypeNode &typeNode) {
    unsigned int size = typeNode.getTypeNodes().size();
    if(size == 1) {
        DSType &type = this->generateType(typeNode.getTypeNodes()[0]);
        typeNode.setType(type);
        return;
    }

    std::vector<DSType *> types(size);
    for(unsigned int i = 0; i < size; i++) {
        types[i] = &this->generateType(typeNode.getTypeNodes()[i]);
    }
    DSType &type = this->pool.createTupleType(std::move(types));
    typeNode.setType(type);
}

void TypeChecker::TypeGenerator::visitTypeOfNode(TypeOfNode &typeNode) {
    if(this->checker == nullptr) {  // not support typeof operator(in D-Bus interface loading)
        RAISE_TC_ERROR(DisallowTypeof, *typeNode.getExprNode());
    }
    DSType &type = this->checker->checkType(typeNode.getExprNode());
    if(type.isBottomType()) {
        RAISE_TC_ERROR(Unacceptable, *typeNode.getExprNode(), this->checker->typePool.getTypeName(type));
    }
    typeNode.setType(type);
}

DSType &TypeChecker::TypeGenerator::generateType(TypeNode *typeNode) {
    typeNode->accept(*this);
    return typeNode->getType();
}


// ##################################
// ##     BlockLeavingDetector     ##
// ##################################

void BlockLeavingDetector::visitDefault(Node &) { }  // do nothing

void BlockLeavingDetector::visitBreakNode(BreakNode &node) {
    if(this->action == Action::RAISE) {
        RAISE_TC_ERROR(InsideFinally, node);
    } else {
        node.setLeavingBlock(true);
    }
}

void BlockLeavingDetector::visitContinueNode(ContinueNode &node) {
    if(this->action == Action::RAISE) {
        RAISE_TC_ERROR(InsideFinally, node);
    } else {
        node.setLeavingBlock(true);
    }
}

void BlockLeavingDetector::visitBlockNode(BlockNode &node) {
    for(auto &e : node.getNodeList()) {
        this->visit(*e);
    }
}

void BlockLeavingDetector::visitIfNode(IfNode &node) {
    this->visit(*node.getThenNode());
    this->visit(*node.getElseNode());
}

void BlockLeavingDetector::visitTryNode(TryNode &node) {
    this->visit(*node.getBlockNode());
    for(auto &e : node.getCatchNodes()) {
        this->visit(*e);
    }
}

void BlockLeavingDetector::visitCatchNode(CatchNode &node) {
    this->visit(*node.getBlockNode());
}

void BlockLeavingDetector::operator()(BlockNode &node) {
    this->visit(node);
}


// #########################
// ##     TypeChecker     ##
// #########################

TypeChecker::TypeChecker(TypePool &typePool, SymbolTable &symbolTable) :
        typePool(typePool), symbolTable(symbolTable), typeGen(this), curReturnType(0),
        visitingDepth(0), loopDepth(0), finallyDepth(0), toplevelPrinting(false) { }

void TypeChecker::checkTypeRootNode(RootNode &rootNode) {
    rootNode.accept(*this);
}

void TypeChecker::recover(bool abortType) {
    this->symbolTable.abort();
    if(abortType) {
        this->typePool.abort();
    }

    this->curReturnType = nullptr;
    this->visitingDepth = 0;
    this->loopDepth = 0;
    this->finallyDepth = 0;
}

DSType *TypeChecker::resolveInterface(TypePool &typePool, InterfaceNode *node) {
    TypeGenerator typeGen(typePool);
    return resolveInterface(typePool, typeGen, node);
}

DSType *TypeChecker::resolveInterface(TypePool &typePool,
                                      TypeChecker::TypeGenerator &typeGen, InterfaceNode *node) {
    auto &type = typePool.createInterfaceType(node->getInterfaceName());

    // create field handle
    unsigned int fieldSize = node->getFieldDeclNodes().size();
    for(unsigned int i = 0; i < fieldSize; i++) {
        VarDeclNode *fieldDeclNode = node->getFieldDeclNodes()[i];
        auto &fieldType = typeGen.generateTypeAndThrow(node->getFieldTypeNodes()[i]);
        FieldHandle *handle = type.newFieldHandle(
                fieldDeclNode->getVarName(), fieldType, fieldDeclNode->isReadOnly());
        if(handle == nullptr) {
            RAISE_TC_ERROR(DefinedField, *fieldDeclNode, fieldDeclNode->getVarName());
        }
    }

    // create method handle
    for(FunctionNode *funcNode : node->getMethodDeclNodes()) {
        MethodHandle *handle = type.newMethodHandle(funcNode->getName());
        handle->setRecvType(type);
        handle->setReturnType(typeGen.generateTypeAndThrow(funcNode->getReturnTypeToken()));
        // resolve multi return
        ReturnTypeNode *rToken = dynamic_cast<ReturnTypeNode *>(funcNode->getReturnTypeToken());
        if(rToken != nullptr && rToken->hasMultiReturn()) {
            handle->setAttribute(MethodHandle::MULTI_RETURN);
        }

        unsigned int paramSize = funcNode->getParamNodes().size();
        for(unsigned int i = 0; i < paramSize; i++) {
            handle->addParamType(typeGen.generateTypeAndThrow(funcNode->getParamTypeNodes()[i]));
        }
    }

    node->setType(typePool.getVoidType());

    return &type;
}

// type check entry point
DSType &TypeChecker::checkType(Node *targetNode) {
    return this->checkType(nullptr, targetNode, &this->typePool.getVoidType());
}

DSType &TypeChecker::checkType(DSType &requiredType, Node *targetNode) {
    return this->checkType(&requiredType, targetNode, nullptr);
}

DSType &TypeChecker::checkType(DSType *requiredType, Node *targetNode, DSType *unacceptableType) {
    CoercionKind kind = CoercionKind::NOP;
    return this->checkType(requiredType, targetNode, unacceptableType, kind);
}

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
        if(!type.isBottomType() && unacceptableType != nullptr &&
                unacceptableType->isSameOrBaseTypeOf(type)) {
            RAISE_TC_ERROR(Unacceptable, *targetNode, this->typePool.getTypeName(type));
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

    RAISE_TC_ERROR(Required, *targetNode, this->typePool.getTypeName(*requiredType),
               this->typePool.getTypeName(type));
}

void TypeChecker::checkTypeWithCurrentScope(BlockNode *blockNode) {
    bool prevIsTerminal = false;
    for(Node * &targetNode : blockNode->refNodeList()) {
        if(prevIsTerminal) {
            RAISE_TC_ERROR(Unreachable, *targetNode);
        }
        this->checkTypeWithCoercion(this->typePool.getVoidType(), targetNode);
        prevIsTerminal = targetNode->getType().isBottomType();

        // check empty block
        BlockNode *b;
        if((b = dynamic_cast<BlockNode *>(targetNode)) != nullptr && b->getNodeList().empty()) {
            RAISE_TC_ERROR(UselessBlock, *targetNode);
        }
    }
    blockNode->setType(prevIsTerminal ? this->typePool.getBottomType() : this->typePool.getVoidType());
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

void TypeChecker::resolveCoercion(DSType &requiredType, Node * &targetNode) {
    targetNode = CastNode::newTypedCastNode(this->typePool, targetNode, requiredType);
}

FieldHandle *TypeChecker::addEntryAndThrowIfDefined(Node &node, const std::string &symbolName, DSType &type,
                                                    bool readOnly) {
    FieldHandle *handle = this->symbolTable.registerHandle(symbolName, type, readOnly);
    if(handle == nullptr) {
        RAISE_TC_ERROR(DefinedSymbol, node, symbolName);
    }
    return handle;
}

void TypeChecker::checkAndThrowIfOutOfLoop(Node &node) {
    if(this->loopDepth > 0) {
        return;
    }
    RAISE_TC_ERROR(InsideLoop, node);
}

void TypeChecker::checkTerminalNodeExistence(BlockNode &blockNode, DSType &returnType) {
    assert(!blockNode.isUntyped());
    if(returnType.isVoidType() && !blockNode.getType().isBottomType()) {
        /**
         * insert return node to block end
         */
        ReturnNode *returnNode = new ReturnNode(0, new EmptyNode());
        returnNode->getExprNode()->setType(this->typePool.getVoidType());
        returnNode->setType(this->typePool.getBottomType());
        blockNode.addNode(returnNode);
        blockNode.setType(returnNode->getType());
    }
    if(!blockNode.getType().isBottomType()) {
        RAISE_TC_ERROR(UnfoundReturn, blockNode);
    }
}

void TypeChecker::pushReturnType(DSType &returnType) {
    this->curReturnType = &returnType;
}

DSType *TypeChecker::popReturnType() {
    DSType *returnType = this->curReturnType;
    this->curReturnType = nullptr;
    return returnType;
}

void TypeChecker::checkAndThrowIfInsideFinally(BlockEndNode &node) {
    if(this->finallyDepth > 0) {
        RAISE_TC_ERROR(InsideFinally, node);
    }
}

DSType &TypeChecker::toType(TypeNode *typeToken) {
    return this->typeGen.generateTypeAndThrow(typeToken);
}

// for ApplyNode type checking
HandleOrFuncType TypeChecker::resolveCallee(Node &recvNode) {
    VarNode *varNode = dynamic_cast<VarNode *>(&recvNode);
    if(varNode != nullptr) {
        return this->resolveCallee(*varNode);
    }

    FunctionType *funcType =
            dynamic_cast<FunctionType *>(&this->checkType(this->typePool.getBaseFuncType(), &recvNode));
    if(funcType == nullptr) {
        RAISE_TC_ERROR(NotCallable, recvNode);
    }
    return HandleOrFuncType(funcType);
}

HandleOrFuncType TypeChecker::resolveCallee(VarNode &recvNode) {
    FieldHandle *handle = this->symbolTable.lookupHandle(recvNode.getVarName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedSymbol, recvNode, recvNode.getVarName());
    }
    recvNode.setAttribute(handle);

    FunctionHandle *funcHandle = dynamic_cast<FunctionHandle *>(handle);
    if(funcHandle != nullptr) {
        return HandleOrFuncType(funcHandle);
    }

    DSType *type = handle->getFieldType(this->typePool);
    FunctionType *funcType = dynamic_cast<FunctionType *>(type);
    if(funcType == nullptr) {
        if(this->typePool.getBaseFuncType() == *type) {
            RAISE_TC_ERROR(NotCallable, recvNode);
        } else {
            RAISE_TC_ERROR(Required, recvNode, this->typePool.getTypeName(this->typePool.getBaseFuncType()),
                       this->typePool.getTypeName(*type));
        }
    }
    return HandleOrFuncType(funcType);
}

void TypeChecker::checkTypeArgsNode(Node &node, MethodHandle *handle, std::vector<Node *> &argNodes) {
    unsigned int argSize = argNodes.size();
    do {
        // check param size
        unsigned int paramSize = handle->getParamTypes().size();
        if(paramSize != argSize) {
            RAISE_TC_ERROR(UnmatchParam, node, std::to_string(paramSize), std::to_string(argSize));
        }

        // check type each node
        for(unsigned int i = 0; i < paramSize; i++) {
            this->checkTypeWithCoercion(*handle->getParamTypes()[i], argNodes[i]);
        }
    } while(handle->getNext() != nullptr);  //FIXME: method overloading
}

// visitor api
void TypeChecker::visit(Node &) {
    fatal("unsupported\n");
}

void TypeChecker::visitBaseTypeNode(BaseTypeNode &) {
    fatal("unsupported\n");
}

void TypeChecker::visitReifiedTypeNode(ReifiedTypeNode &) {
    fatal("unsupported\n");
}

void TypeChecker::visitFuncTypeNode(FuncTypeNode &) {
    fatal("unsupported\n");
}

void TypeChecker::visitDBusIfaceTypeNode(DBusIfaceTypeNode &) {
    fatal("unsupported\n");
}

void TypeChecker::visitReturnTypeNode(ReturnTypeNode &) {
    fatal("unsupported\n");
}

void TypeChecker::visitTypeOfNode(TypeOfNode &) {
    fatal("unsupported\n");
}

void TypeChecker::visitIntValueNode(IntValueNode &node) {
    DSType *type = nullptr;
    switch(node.getKind()) {
    case IntValueNode::BYTE:
        type = &this->typePool.getByteType();
        break;
    case IntValueNode::INT16:
        type = &this->typePool.getInt16Type();
        break;
    case IntValueNode::UINT16:
        type = &this->typePool.getUint16Type();
        break;
    case IntValueNode::INT32:
        type = &this->typePool.getInt32Type();
        break;
    case IntValueNode::UINT32:
        type = &this->typePool.getUint32Type();
        break;
    }
    node.setType(*type);
}

void TypeChecker::visitLongValueNode(LongValueNode &node) {
    if(node.isUnsignedValue()) {
        node.setType(this->typePool.getUint64Type());
    } else {
        node.setType(this->typePool.getInt64Type());
    }
}

void TypeChecker::visitFloatValueNode(FloatValueNode &node) {
    node.setType(this->typePool.getFloatType());
}

void TypeChecker::visitStringValueNode(StringValueNode &node) {
    node.setType(this->typePool.getStringType());
}

void TypeChecker::visitObjectPathNode(ObjectPathNode &node) {
    node.setType(this->typePool.getObjectPathType());
}

void TypeChecker::visitStringExprNode(StringExprNode &node) {
    const unsigned int size = node.getExprNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        Node *exprNode = node.getExprNodes()[i];
        auto &exprType = this->checkType(exprNode);
        if(exprType != this->typePool.getStringType()) { // call __INTERP__()
            std::string methodName(OP_INTERP);
            MethodHandle *handle = exprType.lookupMethodHandle(this->typePool, methodName);
            assert(handle != nullptr);

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

void TypeChecker::visitArrayNode(ArrayNode &node) {
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

void TypeChecker::visitMapNode(MapNode &node) {
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

void TypeChecker::visitTupleNode(TupleNode &node) {
    unsigned int size = node.getNodes().size();
    std::vector<DSType *> types(size);
    for(unsigned int i = 0; i < size; i++) {
        types[i] = &this->checkType(node.getNodes()[i]);
    }
    node.setType(this->typePool.createTupleType(std::move(types)));
}

void TypeChecker::visitVarNode(VarNode &node) {
    FieldHandle *handle = this->symbolTable.lookupHandle(node.getVarName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedSymbol, node, node.getVarName());
    }

    node.setAttribute(handle);
    node.setType(*handle->getFieldType(this->typePool));
}

void TypeChecker::visitAccessNode(AccessNode &node) {
    auto &recvType = this->checkType(node.getRecvNode());
    FieldHandle *handle = recvType.lookupFieldHandle(this->typePool, node.getFieldName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedField, node, node.getFieldName());
    }

    node.setAttribute(handle);
    node.setType(*handle->getFieldType(this->typePool));
}

void TypeChecker::visitCastNode(CastNode &node) {
    auto &exprType = this->checkType(node.getExprNode());
    auto &targetType = this->toType(node.getTargetTypeNode());
    node.setType(targetType);

    // resolve cast op
    if(!node.resolveCastOp(this->typePool)) {
        RAISE_TC_ERROR(CastOp, node, this->typePool.getTypeName(exprType), this->typePool.getTypeName(targetType));
    }
}

void TypeChecker::visitInstanceOfNode(InstanceOfNode &node) {
    auto &exprType = this->checkType(node.getTargetNode());
    auto &targetType = this->toType(node.getTargetTypeNode());

    if(targetType.isSameOrBaseTypeOf(exprType)) {
        node.setOpKind(InstanceOfNode::ALWAYS_TRUE);
    } else if(exprType.isSameOrBaseTypeOf(targetType)) {
        node.setOpKind(InstanceOfNode::INSTANCEOF);
    } else {
        node.setOpKind(InstanceOfNode::ALWAYS_FALSE);
    }
    node.setType(this->typePool.getBooleanType());
}

void TypeChecker::visitPrintNode(PrintNode &node) {
    this->checkType(node.getExprNode());
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitUnaryOpNode(UnaryOpNode &node) {
    this->checkType(node.getExprNode());
    MethodCallNode *applyNode = node.createApplyNode();
    node.setType(this->checkType(applyNode));
}

void TypeChecker::visitBinaryOpNode(BinaryOpNode &node) {
    auto &leftType = this->checkType(node.getLeftNode());
    auto &rightType = this->checkType(node.getRightNode());

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

    MethodCallNode *applyNode = node.createApplyNode();
    node.setType(this->checkType(applyNode));
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
        RAISE_TC_ERROR(UnmatchParam, node, std::to_string(size), std::to_string(argSize));
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
    MethodHandle *handle = recvType.lookupMethodHandle(this->typePool, node.getMethodName());
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedMethod, node, node.getMethodName());
    }

    // check type argument
    this->checkTypeArgsNode(node, handle, node.refArgNodes());

    node.setHandle(handle);
    node.setType(*handle->getReturnType());
}

void TypeChecker::visitNewNode(NewNode &node) {
    auto &type = this->toType(node.getTargetTypeNode());
    MethodHandle *handle = type.getConstructorHandle(this->typePool);
    if(handle == nullptr) {
        RAISE_TC_ERROR(UndefinedInit, node, this->typePool.getTypeName(type));
    }

    this->checkTypeArgsNode(node, handle, node.refArgNodes());
    node.setType(type);
}

void TypeChecker::visitCondOpNode(CondOpNode &node) {
    auto &booleanType = this->typePool.getBooleanType();
    this->checkType(booleanType, node.getLeftNode());
    this->checkType(booleanType, node.getRightNode());
    node.setType(booleanType);
}

void TypeChecker::visitTernaryNode(TernaryNode &node) {
    this->checkType(this->typePool.getBooleanType(), node.getCondNode());
    auto &leftType = this->checkType(node.getLeftNode());
    auto &rightType = this->checkType(node.getRightNode());

    if(leftType.isSameOrBaseTypeOf(rightType)) {
        node.setType(leftType);
    } else if(rightType.isSameOrBaseTypeOf(leftType)) {
        node.setType(rightType);
    } else if(this->checkCoercion(leftType, rightType)) {
        this->checkTypeWithCoercion(leftType, node.refRightNode());
        node.setType(leftType);
    } else {
        this->checkTypeWithCoercion(rightType, node.refLeftNode());
        node.setType(rightType);
    }
}

void TypeChecker::visitCmdNode(CmdNode &node) {
    this->checkType(this->typePool.getStringType(), node.getNameNode());
    for(auto *argNode : node.getArgNodes()) {
        this->checkType(argNode);
    }
    node.setType(this->typePool.getProcType());
}

void TypeChecker::visitCmdArgNode(CmdArgNode &node) {
    const unsigned int size = node.getSegmentNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        Node *exprNode = node.getSegmentNodes()[i];
        auto &segmentType = this->checkType(exprNode);

        if(segmentType != this->typePool.getStringType() &&
                segmentType != this->typePool.getStringArrayType()) {    // call __STR__ or __CMD__ARG
            // first try lookup __CMD_ARG__ method
            std::string methodName(OP_CMD_ARG);
            MethodHandle *handle = segmentType.lookupMethodHandle(this->typePool, methodName);

            if(handle == nullptr || (*handle->getReturnType() != this->typePool.getStringType() &&
                    *handle->getReturnType() != this->typePool.getStringArrayType())) { // if not found, lookup __STR__
                methodName = OP_STR;
                handle = segmentType.lookupMethodHandle(this->typePool, methodName);
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

void TypeChecker::visitRedirNode(RedirNode &node) {
    CmdArgNode *argNode = node.getTargetNode();
    this->checkType(argNode);

    // not allow String Array type
    if(argNode->getSegmentNodes().size() == 1) {
        this->checkType(nullptr, argNode->getSegmentNodes()[0], &this->typePool.getStringArrayType());
    }

    node.setType(this->typePool.getAnyType());   //FIXME
}

void TypeChecker::visitTildeNode(TildeNode &node) {
    node.setType(this->typePool.getStringType());
}

void TypeChecker::visitPipedCmdNode(PipedCmdNode &node) {
    for(auto *cmdNode : node.getCmdNodes()) {
        this->checkType(this->typePool.getProcType(), cmdNode);
    }
    node.setType(this->typePool.getBooleanType());
}

void TypeChecker::visitSubstitutionNode(SubstitutionNode &node) {
    this->checkType(nullptr, node.getExprNode(), nullptr);
    if(node.isStrExpr()) {
        node.setType(this->typePool.getStringType());
    } else {
        node.setType(this->typePool.getStringArrayType());
    }
}

void TypeChecker::visitAssertNode(AssertNode &node) {
    this->checkType(this->typePool.getBooleanType(), node.getCondNode());
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitBlockNode(BlockNode &node) {
    this->symbolTable.enterScope();
    this->checkTypeWithCurrentScope(&node);
    this->symbolTable.exitScope();
}

void TypeChecker::visitBreakNode(BreakNode &node) {
    this->checkAndThrowIfOutOfLoop(node);
    node.setType(this->typePool.getBottomType());
}

void TypeChecker::visitContinueNode(ContinueNode &node) {
    this->checkAndThrowIfOutOfLoop(node);
    node.setType(this->typePool.getBottomType());
}

void TypeChecker::visitExportEnvNode(ExportEnvNode &node) {
    auto &stringType = this->typePool.getStringType();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node.getEnvName(), stringType, false);
    handle->setAttribute(FieldHandle::ENV);

    node.setAttribute(handle);
    this->checkType(stringType, node.getExprNode());
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitImportEnvNode(ImportEnvNode &node) {
    auto &stringType = this->typePool.getStringType();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node.getEnvName(), stringType, false);
    handle->setAttribute(FieldHandle::ENV);

    if(node.getDefaultValueNode() != nullptr) {
        this->checkType(this->typePool.getStringType(), node.getDefaultValueNode());
    }

    node.setAttribute(handle);
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitTypeAliasNode(TypeAliasNode &node) {
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

void TypeChecker::visitForNode(ForNode &node) {
    this->symbolTable.enterScope();

    this->checkTypeWithCoercion(this->typePool.getVoidType(), node.refInitNode());
    this->checkType(this->typePool.getBooleanType(), node.getCondNode());
    this->checkTypeWithCoercion(this->typePool.getVoidType(), node.refIterNode());

    this->enterLoop();
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->exitLoop();
    
    this->symbolTable.exitScope();
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitWhileNode(WhileNode &node) {
    this->checkType(this->typePool.getBooleanType(), node.getCondNode());
    this->enterLoop();
    this->checkType(this->typePool.getVoidType(), node.getBlockNode());
    this->exitLoop();
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitDoWhileNode(DoWhileNode &node) {
    this->symbolTable.enterScope();
    this->enterLoop();
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->exitLoop();

    /**
     * block node and cond node is same scope.
     */
    this->checkType(this->typePool.getBooleanType(), node.getCondNode());

    this->symbolTable.exitScope();
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitIfNode(IfNode &node) {
    this->checkType(this->typePool.getBooleanType(), node.getCondNode());
    this->checkType(this->typePool.getVoidType(), node.getThenNode());
    this->checkType(this->typePool.getVoidType(), node.getElseNode());

    // check if terminal node
    bool terminal = node.getThenNode()->getType().isBottomType()
                    && node.getElseNode()->getType().isBottomType();
    node.setType(terminal ? this->typePool.getBottomType() : this->typePool.getVoidType());
}

void TypeChecker::visitReturnNode(ReturnNode &node) {
    this->checkAndThrowIfInsideFinally(node);
    DSType *returnType = this->getCurrentReturnType();
    if(returnType == nullptr) {
        RAISE_TC_ERROR(InsideFunc, node);
    }
    auto &exprType = this->checkType(*returnType, node.getExprNode());
    if(exprType == this->typePool.getVoidType()) {
        if(dynamic_cast<EmptyNode *>(node.getExprNode()) == nullptr) {
            RAISE_TC_ERROR(NotNeedExpr, node);
        }
    }
    node.setType(this->typePool.getBottomType());
}

void TypeChecker::visitThrowNode(ThrowNode &node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkType(node.getExprNode());
    node.setType(this->typePool.getBottomType());
}

void TypeChecker::visitCatchNode(CatchNode &node) {
    auto &exceptionType = this->toType(node.getTypeNode());

    /**
     * check type catch block
     */
    this->symbolTable.enterScope();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node.getExceptionName(), exceptionType, true);
    node.setAttribute(handle);
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->symbolTable.exitScope();
    node.setType(node.getBlockNode()->getType());
}

void TypeChecker::visitTryNode(TryNode &node) {
    if(node.getCatchNodes().empty() && node.getFinallyNode() == nullptr) {
        RAISE_TC_ERROR(UselessTry, node);
    }
    if(node.getBlockNode()->getNodeList().empty()) {
        RAISE_TC_ERROR(EmptyTry, *node.getBlockNode());
    }

    this->checkType(this->typePool.getVoidType(), node.getBlockNode());

    BlockLeavingDetector detector(BlockLeavingDetector::MARK);
    detector(*node.getBlockNode());

    // check type catch block
    for(CatchNode *c : node.getCatchNodes()) {
        this->checkType(this->typePool.getVoidType(), c);
        detector(*c->getBlockNode());
    }

    // check type finally block, may be empty node
    if(node.getFinallyNode() != nullptr) {
        this->finallyDepth++;
        this->checkType(this->typePool.getVoidType(), node.getFinallyNode());
        this->finallyDepth--;

        if(node.getFinallyNode()->getNodeList().empty()) {
            RAISE_TC_ERROR(UselessBlock, *node.getFinallyNode());
        }

        BlockLeavingDetector()(*node.getFinallyNode());
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

void TypeChecker::visitVarDeclNode(VarDeclNode &node) {
    auto &initValueType = this->checkType(node.getInitValueNode());
    if(initValueType.isBottomType()) {
        RAISE_TC_ERROR(Unacceptable, *node.getInitValueNode(), this->typePool.getTypeName(initValueType));
    }

    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node.getVarName(), initValueType, node.isReadOnly());
    node.setAttribute(handle);
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitAssignNode(AssignNode &node) {
    AssignableNode *leftNode = dynamic_cast<AssignableNode *>(node.getLeftNode());
    if(leftNode == nullptr) {
        RAISE_TC_ERROR(Assignable, node);
    }

    auto &leftType = this->checkType(leftNode);
    if(leftNode->isReadOnly()) {
        RAISE_TC_ERROR(ReadOnly, node);
    }

    if(dynamic_cast<AccessNode *>(leftNode) != nullptr) {
        node.setAttribute(AssignNode::FIELD_ASSIGN);
    }
    if(node.isSelfAssignment()) {
        BinaryOpNode *opNode = dynamic_cast<BinaryOpNode *>(node.getRightNode());
        opNode->getLeftNode()->setType(leftType);
        AccessNode *accessNode = dynamic_cast<AccessNode *>(leftNode);
        if(accessNode != nullptr) {
            accessNode->setAdditionalOp(AccessNode::DUP_RECV);
        }
        auto &rightType = this->checkType(node.getRightNode());
        if(leftType != rightType) { // convert right hand-side type to left type
            node.refRightNode() = CastNode::newTypedCastNode(this->typePool, node.refRightNode(), leftType);
        }
    } else {
        this->checkTypeWithCoercion(leftType, node.refRightNode());
    }

    node.setType(this->typePool.getVoidType());
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
        node.refRightNode() = CastNode::newTypedCastNode(this->typePool, node.refRightNode(), elementType);
    }

    node.getSetterNode()->getArgNodes()[1]->setType(elementType);
    this->checkType(this->typePool.getVoidType(), node.getSetterNode());

    node.setType(this->typePool.getVoidType());
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
        if(type->isVoidType()) {
            RAISE_TC_ERROR(Unacceptable, *node.getParamTypeNodes()[i], this->typePool.getTypeName(*type));
        }
        paramTypes[i] = type;
    }

    // register function handle
    FunctionHandle *handle =
            this->symbolTable.registerFuncHandle(node.getName(), returnType, paramTypes);
    if(handle == nullptr) {
        RAISE_TC_ERROR(DefinedSymbol, node, node.getName());
    }
    node.setVarIndex(handle->getFieldIndex());

    // prepare type checking
    this->pushReturnType(returnType);
    this->symbolTable.enterFunc();
    this->symbolTable.enterScope();

    // register parameter
    for(unsigned int i = 0; i < paramSize; i++) {
        VarNode *paramNode = node.getParamNodes()[i];
        FieldHandle *fieldHandle = this->addEntryAndThrowIfDefined(
                *paramNode, paramNode->getVarName(), *paramTypes[i], false);
        paramNode->setAttribute(fieldHandle);
    }

    // check type func body
    this->checkTypeWithCurrentScope(node.getBlockNode());
    this->symbolTable.exitScope();

    node.setMaxVarNum(this->symbolTable.getMaxVarIndex());
    this->symbolTable.exitFunc();
    this->popReturnType();

    this->checkTerminalNodeExistence(*node.getBlockNode(), returnType);

    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }

    this->pushReturnType(this->typePool.getIntType());    // pseudo return type
    this->symbolTable.enterFunc();

    // check type command body
    this->checkType(this->typePool.getVoidType(), node.getBlockNode());

    node.setMaxVarNum(this->symbolTable.getMaxVarIndex());
    this->symbolTable.exitFunc();
    this->popReturnType();

    if(!this->typePool.addUserDefinedCommandName(node.getName())) {
        RAISE_TC_ERROR(DefinedCmd, node, node.getName());
    }
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitInterfaceNode(InterfaceNode &node) {
    if(!this->isTopLevel()) {   // only available toplevel scope
        RAISE_TC_ERROR(OutsideToplevel, node);
    }
    resolveInterface(this->typePool, this->typeGen, &node);
}

void TypeChecker::visitBindVarNode(BindVarNode &node) {
    DSType *valueType = node.getValue()->getType();
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node.getVarName(), *valueType, true);
    node.setAttribute(handle);
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitEmptyNode(EmptyNode &node) {
    node.setType(this->typePool.getVoidType());
}

void TypeChecker::visitDummyNode(DummyNode &) {
}   // do nothing

void TypeChecker::visitRootNode(RootNode &node) {
    this->symbolTable.commit();
    this->typePool.commit();

    bool prevIsTerminal = false;
    for(auto &targetNode : node.refNodeList()) {
        if(prevIsTerminal) {
            RAISE_TC_ERROR(Unreachable, *targetNode);
        }
        if(dynamic_cast<PipedCmdNode *>(targetNode) != nullptr) {  // pop stack top
            this->checkTypeWithCoercion(this->typePool.getVoidType(), targetNode);
        } else if(this->toplevelPrinting) {
            this->checkType(nullptr, targetNode, nullptr);
            if(!targetNode->getType().isVoidType() && !targetNode->getType().isBottomType()) {
                targetNode = PrintNode::newTypedPrintNode(this->typePool, targetNode);
            }
        } else {
            this->checkTypeWithCoercion(this->typePool.getVoidType(), targetNode);
        }
        prevIsTerminal = targetNode->getType().isBottomType();

        // check empty block
        BlockNode *blockNode;
        if((blockNode = dynamic_cast<BlockNode *>(targetNode)) != nullptr && blockNode->getNodeList().empty()) {
            RAISE_TC_ERROR(UselessBlock, *targetNode);
        }
    }

    node.setMaxVarNum(this->symbolTable.getMaxVarIndex());
    node.setMaxGVarNum(this->symbolTable.getMaxGVarIndex());
    node.setType(this->typePool.getVoidType());
}

} // namespace parser
} // namespace ydsh
