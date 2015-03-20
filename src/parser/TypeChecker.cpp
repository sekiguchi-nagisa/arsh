/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#include <assert.h>
#include <vector>

#include <core/magic_method.h>
#include <core/TypeLookupError.h>
#include <parser/TypeChecker.h>
#include <parser/TypeCheckError.h>

// ##############################
// ##     HandleOrFuncType     ##
// ##############################

HandleOrFuncType::HandleOrFuncType(FunctionHandle *handle) :
        hasHandle(true), handle(handle) {
}

HandleOrFuncType::HandleOrFuncType(FunctionType *funcType) :
        hasHandle(false), funcType(funcType) {
}

bool HandleOrFuncType::treatAsHandle() {
    return this->hasHandle;
}

FunctionHandle *HandleOrFuncType::getHandle() {
    return this->hasHandle ? this->handle : 0;
}

FunctionType *HandleOrFuncType::getFuncType() {
    return this->hasHandle ? 0 : this->funcType;
}

// #########################
// ##     TypeChecker     ##
// #########################

TypeChecker::TypeChecker(TypePool *typePool) :
        typePool(typePool), symbolTable(), curReturnType(0), loopContextStack(), finallyContextStack() {
}

TypeChecker::~TypeChecker() {
    this->finallyContextStack.clear();
}

void TypeChecker::checkTypeRootNode(RootNode &rootNode) {
    this->symbolTable.clearEntryCache();

    for(Node *node : rootNode.getNodeList()) {
        this->checkTypeAsStatement(node);
    }
}

// type check entry point

DSType *TypeChecker::checkTypeAsStatement(Node *targetNode) {
    return this->checkType(0, targetNode, 0);
}

DSType *TypeChecker::checkType(Node *targetNode) {
    return this->checkType(0, targetNode, this->typePool->getVoidType());
}

DSType *TypeChecker::checkType(DSType *requiredType, Node *targetNode) {
    return this->checkType(requiredType, targetNode, 0);
}

DSType *TypeChecker::checkType(DSType *requiredType, Node *targetNode,
        DSType *unacceptableType, bool allowCoercion) {
    /**
     * if target node is expr node and type is null,
     * try type check.
     */
    if(targetNode->getType() == 0) {
        targetNode->accept(this);
    }

    /**
     * after type checking, if type is still null,
     * throw exception.
     */
    DSType *type = targetNode->getType();
    if(type == 0) {
        E_Unresolved(targetNode);
    }

    /**
     * do not try type matching.
     */
    if(requiredType == 0) {
        if(unacceptableType != 0 && unacceptableType->isAssignableFrom(type)) {
            E_Unacceptable(targetNode, type->getTypeName());
        }
        return type;
    }

    /**
     * try type matching.
     */
    if(requiredType->isAssignableFrom(type)) {
        return type;
    }

    /**
     * check coercion
     */
    if(allowCoercion && this->supportCoercion(requiredType, type)) {
        return type;
    }

    E_Required(targetNode, requiredType->getTypeName(), type->getTypeName());
    return 0;
}

Node *TypeChecker::checkTypeAndResolveCoercion(DSType *requiredType, Node *targetNode) {
    TypePool *pool = this->typePool;
    DSType *type = this->checkType(requiredType, targetNode, pool->getVoidType(), true);
    if(this->supportCoercion(requiredType, type)) {
        if(requiredType->equals(pool->getFloatType()) && type->equals(pool->getIntType())) {
            // int to float
            return this->intToFloat(targetNode);
        }
    }
    return targetNode;
}

bool TypeChecker::supportCoercion(DSType *requiredType, DSType *targetType) {
    // int to float cast
    if(requiredType->equals(this->typePool->getFloatType()) &&
            targetType->equals(this->typePool->getIntType())) {
        return true;
    }
    return false;
}

CastNode *TypeChecker::intToFloat(Node *targetNode) {
    assert(targetNode->getType() != 0);
    assert(targetNode->getType()->equals(this->typePool->getIntType()));

    CastNode *castNode = new CastNode(targetNode, 0);
    castNode->setOpKind(CastNode::INT_TO_FLOAT);
    castNode->setType(this->typePool->getFloatType());
    return castNode;
}

void TypeChecker::checkTypeWithNewBlockScope(BlockNode *blockNode) {
    this->symbolTable.enterScope();
    this->checkTypeWithCurrentBlockScope(blockNode);
    this->symbolTable.exitScope();
}

void TypeChecker::checkTypeWithCurrentBlockScope(BlockNode *blockNode) {
    blockNode->accept(this);
}

FieldHandle *TypeChecker::addEntryAndThrowIfDefined(Node *node, const std::string &symbolName, DSType *type,
        bool readOnly) {
    FieldHandle *handle = this->symbolTable.registerHandle(symbolName, type, readOnly);
    if(handle == 0) {
        E_DefinedSymbol(node, symbolName);
    }
    return handle;
}

void TypeChecker::enterLoop() {
    this->loopContextStack.push_back(true);
}

void TypeChecker::exitLoop() {
    this->loopContextStack.pop_back();
}

void TypeChecker::checkAndThrowIfOutOfLoop(Node *node) {
    if(!this->loopContextStack.empty() && this->loopContextStack.back()) {
        return;
    }
    E_InsideLoop(node);
}

bool TypeChecker::findBlockEnd(BlockNode *blockNode) {
    if(blockNode->getNodeList().size() == 0) {
        return false;
    }
    Node *endNode = blockNode->getNodeList().back();
    if(dynamic_cast<BlockEndNode*>(endNode) != 0) {
        return true;
    }

    /**
     * if endNode is IfNode, search recursively
     */
    IfNode *ifNode = dynamic_cast<IfNode*>(endNode);
    if(ifNode != 0) {
        if(!this->findBlockEnd(ifNode->getThenNode())) {
            return false;
        }
        for(BlockNode *elifThenNode : ifNode->getElifThenNodes()) {
            if(!this->findBlockEnd(elifThenNode)) {
                return false;
            }
        }
        return this->findBlockEnd(ifNode->getElseNode());
    }
    return false;
}

void TypeChecker::checkBlockEndExistence(BlockNode *blockNode, DSType *returnType) {
    Node *endNode = blockNode->getNodeList().back();

    if(returnType->equals(this->typePool->getVoidType())
            && dynamic_cast<BlockEndNode*>(endNode) == 0) {
        /**
         * insert return node to block end
         */
        blockNode->addNode(new ReturnNode(0, new EmptyNode()));
        return;
    }
    if(!this->findBlockEnd(blockNode)) {
        E_UnfoundReturn(blockNode);
    }
}

void TypeChecker::pushReturnType(DSType *returnType) {
    this->curReturnType = returnType;
}

DSType *TypeChecker::popReturnType() {
    DSType *returnType = this->curReturnType;
    this->curReturnType = 0;
    return returnType;
}

DSType *TypeChecker::getCurrentReturnType() {
    return this->curReturnType;
}

void TypeChecker::checkAndThrowIfInsideFinally(BlockEndNode *node) {
    if(!this->finallyContextStack.empty() && this->finallyContextStack.back()) {
        E_InsideFinally(node);
    }
}

DSType *TypeChecker::toType(TypeToken *typeToken) {
    try {
        DSType *type = typeToken->toType(this->typePool);
        delete typeToken;
        return type;
    } catch(TypeLookupError &e) {
        unsigned int lineNum = typeToken->getLineNum();
        delete typeToken;
        throw TypeCheckError(lineNum, e);
    }
}

// for ApplyNode type checking
HandleOrFuncType TypeChecker::resolveCallee(Node *recvNode, ApplyNode *applyNode) {
    AccessNode *accessNode = dynamic_cast<AccessNode*>(recvNode);
    if(accessNode != 0) {
        return this->resolveCallee(accessNode, applyNode);
    }
    VarNode *varNode = dynamic_cast<VarNode*>(recvNode);
    if(varNode != 0) {
        return this->resolveCallee(varNode, applyNode);
    }

    FunctionType *funcType =
            dynamic_cast<FunctionType*>(this->checkType(this->typePool->getBaseFuncType(), recvNode));
    applyNode->setFuncCall(true);
    return HandleOrFuncType(funcType);
}

HandleOrFuncType TypeChecker::resolveCallee(AccessNode *recvNode, ApplyNode *applyNode) {
    DSType *actualRecvType = this->checkType(recvNode->getRecvNode());
    FieldHandle *handle = actualRecvType->lookupFieldHandle(this->typePool, recvNode->getFieldName());
    if(handle == 0) {
        E_UndefinedField(recvNode, recvNode->getFieldName());
    }

    recvNode->setAttribute(handle);
    // treat as method call
    FunctionHandle *funcHandle = dynamic_cast<FunctionHandle*>(handle);
    if(funcHandle != 0) {
        recvNode->setAdditionalOp(AccessNode::DUP_RECV_AND_SWAP);
        applyNode->setFuncCall(false);
        return HandleOrFuncType(funcHandle);
    }

    FunctionType *funcType = dynamic_cast<FunctionType*>(handle->getFieldType(this->typePool));
    // treat as method call
    if(funcType != 0 && funcType->treatAsMethod(actualRecvType)) {
        recvNode->setAdditionalOp(AccessNode::DUP_RECV_AND_SWAP);
        applyNode->setFuncCall(false);
        return HandleOrFuncType(funcType);
    }

    // treat as function call
    if(funcType == 0) {
        E_UndefinedMethod(recvNode, recvNode->getFieldName());
    }

    applyNode->setFuncCall(true);
    return HandleOrFuncType(funcType);
}

HandleOrFuncType TypeChecker::resolveCallee(VarNode *recvNode, ApplyNode *applyNode) {
    FieldHandle *handle = this->symbolTable.lookupHandle(recvNode->getVarName());
    if(handle == 0) {
        E_UndefinedSymbol(recvNode, recvNode->getVarName());
    }
    recvNode->setAttribute(handle);
    applyNode->setFuncCall(true);

    FunctionHandle *funcHandle = dynamic_cast<FunctionHandle*>(handle);
    if(funcHandle != 0) {
        return HandleOrFuncType(funcHandle);
    }

    DSType *type = handle->getFieldType(this->typePool);
    FunctionType *funcType = dynamic_cast<FunctionType*>(type);
    if(funcType == 0) {
        E_Required(recvNode, this->typePool->getBaseFuncType()->getTypeName(), type->getTypeName());
    }
    return HandleOrFuncType(funcType);
}

void TypeChecker::checkTypeArgsNode(FunctionHandle *handle, ArgsNode *argsNode, bool isFuncCall) {
    const unsigned int startIndex = isFuncCall ? 0 : 1;
    // check named arg existence
    bool foundNamedArg = false;
    for(const std::pair<std::string, Node*> &argPair : argsNode->getArgPairs()) {
        if(argPair.first != "") {
            foundNamedArg = true;
        }
        /**
         * if previously found named parameter, but current argument has no named parameter,
         * report error.
         */
        else if(foundNamedArg) {
            E_NeedNamedArg(argsNode);
        }
    }

    /**
     * if named parameter not found. only check type
     */
    const std::vector<DSType*> &paramTypes = handle->getParamTypes();
    if(!foundNamedArg) {
        this->checkTypeArgsNode(paramTypes, argsNode, isFuncCall);
        return;
    }

    // check param size
    unsigned int paramSize = paramTypes.size();
    unsigned int argSize = argsNode->getArgPairs().size();
    if(argSize > paramSize - startIndex) {
        E_UnmatchParam(argsNode,
                std::to_string(paramSize - startIndex),
                std::to_string(argSize));
    }

    // resolve named param index
    argsNode->initIndexMap();
    argsNode->setParamSize(paramSize);
    unsigned int count = 0;
    for(const std::pair<std::string, Node*> &argPair : argsNode->getArgPairs()) {
        int index = handle->getParamIndex(argPair.first);
        if(index == -1) {
            E_UnfoundNamedParam(argsNode, argPair.first);
        }
        argsNode->addParamIndex(count++, index);
    }

    // check argument duplication
    unsigned int foundIndexMap[paramSize];
    // init with 0
    for(unsigned int i = 0; i < paramSize; i++) {
        foundIndexMap[i] = 0;
    }
    for(unsigned int i = 0; i < argSize; i++) {
        unsigned int paramIndex = argsNode->getParamIndexMap()[i];
        if(foundIndexMap[paramIndex]++) {
            E_DupNamedArg(argsNode, argsNode->getArgPairs()[i].first);
        }
    }

    // check default value existence
    for(unsigned int i = startIndex; i < paramSize; i++) {
        if(foundIndexMap[i] == 0 && !handle->hasDefaultValue(i)) {
            E_NoDefaultValue(argsNode);
        }
    }

    // check type each arg
    for(unsigned int i = 0; i < argSize; i++) {
        argsNode->setArg(i, this->checkTypeAndResolveCoercion(
                paramTypes[argsNode->getParamIndexMap()[i]],
                argsNode->getArgPairs()[i].second));
    }
}

void TypeChecker::checkTypeArgsNode(FunctionType *funcType, ArgsNode *argsNode, bool isFuncCall) {
    // check has no named arg
    for(const std::pair<std::string, Node*> &argPair : argsNode->getArgPairs()) {
        if(argPair.first != "") {
            E_UnneedNamedArg(argsNode);
        }
    }

    // check type each node
    this->checkTypeArgsNode(funcType->getParamTypes(), argsNode, isFuncCall);
}

void TypeChecker::checkTypeArgsNode(const std::vector<DSType*> &paramTypes, ArgsNode *argsNode, bool isFuncCall) {
    const unsigned int startIndex = isFuncCall ? 0 : 1;
    unsigned int size = paramTypes.size();
    unsigned int argSize = argsNode->getArgPairs().size();
    // check param size
    if(size - startIndex != argSize) {
        E_UnmatchParam(argsNode,
                std::to_string(size - startIndex),
                std::to_string(argSize));
    }

    // check type each node
    for(unsigned int i = startIndex; i < size; i++) {
        argsNode->setArg(i,
                this->checkTypeAndResolveCoercion(paramTypes[i], argsNode->getArgPairs()[i].second));
    }
}

void TypeChecker::recover() {
    this->symbolTable.popAllLocal();
    this->symbolTable.removeCachedEntry();

    this->curReturnType = 0;
    this->loopContextStack.clear();
    this->finallyContextStack.clear();
}

// visitor api

void TypeChecker::visitIntValueNode(IntValueNode *node) {	//TODO: int8, int16 ..etc
    DSType *type = this->typePool->getIntType();
    node->setType(type);
}

void TypeChecker::visitFloatValueNode(FloatValueNode *node) {
    DSType *type = this->typePool->getFloatType();
    node->setType(type);
}

void TypeChecker::visitStringValueNode(StringValueNode *node) {
    DSType *type = this->typePool->getStringType();
    node->setType(type);
}

void TypeChecker::visitStringExprNode(StringExprNode *node) {
    for(Node *exprNode : node->getExprNodes()) {
        this->checkType(this->typePool->getStringType(), exprNode);
    }
    node->setType(this->typePool->getStringType());
}

void TypeChecker::visitArrayNode(ArrayNode *node) {
    unsigned int size = node->getExprNodes().size();
    assert(size != 0);
    Node *firstElementNode = node->getExprNodes()[0];
    DSType *elementType = this->checkType(firstElementNode);

    for(unsigned int i = 1; i < size; i++) {
        node->setExprNode(i, this->checkTypeAndResolveCoercion(elementType,
                node->getExprNodes()[i]));
    }

    TypeTemplate *arrayTemplate = this->typePool->getArrayTemplate();
    std::vector<DSType*> elementTypes(1);
    elementTypes[0] = elementType;
    node->setType(this->typePool->createAndGetReifiedTypeIfUndefined(arrayTemplate, elementTypes));
}

void TypeChecker::visitMapNode(MapNode *node) {
    unsigned int size = node->getValueNodes().size();
    assert(size != 0);
    Node *firstKeyNode = node->getKeyNodes()[0];
    DSType *keyType = this->checkType(this->typePool->getValueType(), firstKeyNode);
    Node *firstValueNode = node->getValueNodes()[0];
    DSType *valueType = this->checkType(firstValueNode);

    for(unsigned int i = 1; i < size; i++) {
        node->setKeyNode(i,
                this->checkTypeAndResolveCoercion(keyType, node->getKeyNodes()[i]));
        node->setValueNode(i,
                this->checkTypeAndResolveCoercion(valueType, node->getValueNodes()[i]));
    }

    TypeTemplate *mapTemplate = this->typePool->getMapTemplate();
    std::vector<DSType*> elementTypes(2);
    elementTypes[0] = keyType;
    elementTypes[1] = valueType;
    node->setType(this->typePool->createAndGetReifiedTypeIfUndefined(mapTemplate, elementTypes));
}

void TypeChecker::visitTupleNode(TupleNode *node) {
    unsigned int size = node->getNodes().size();
    std::vector<DSType*> types(size);
    for(unsigned int i = 0; i < size; i++) {
        types[i] = this->checkType(node->getNodes()[i]);
    }
    node->setType(this->typePool->createAndGetTupleTypeIfUndefined(types));
}

void TypeChecker::visitVarNode(VarNode *node) {
    FieldHandle *handle = this->symbolTable.lookupHandle(node->getVarName());
    if(handle == 0) {
        E_UndefinedSymbol(node, node->getVarName());
    }

    node->setAttribute(handle);
    node->setType(handle->getFieldType(this->typePool));
}

void TypeChecker::visitAccessNode(AccessNode *node) {
    DSType *recvType = this->checkType(node->getRecvNode());
    FieldHandle *handle = recvType->lookupFieldHandle(this->typePool, node->getFieldName());
    if(handle == 0) {
        E_UndefinedField(node, node->getFieldName());
    }

    node->setAttribute(handle);
    node->setType(handle->getFieldType(this->typePool));
}

void TypeChecker::visitCastNode(CastNode *node) {
    DSType *exprType = this->checkType(node->getExprNode());
    DSType *targetType = this->toType(node->removeTargetTypeToken());
    node->setType(targetType);

    // resolve cast op
    TypePool *pool = this->typePool;

    /**
     * nop
     */
    if(targetType->isAssignableFrom(exprType)) {
        return;
    }

    /**
     * int to float
     */
    if(exprType->equals(pool->getIntType()) && targetType->equals(pool->getFloatType())) {
        node->setOpKind(CastNode::INT_TO_FLOAT);
        return;
    }

    /**
     * float to int
     */
    if(exprType->equals(pool->getFloatType()) && targetType->equals(pool->getIntType())) {
        node->setOpKind(CastNode::FLOAT_TO_INT);
        return;
    }

    /**
     * to string
     */
    if(targetType->equals(pool->getStringType())) {
        node->setOpKind(CastNode::TO_STRING);
        FieldHandle *handle = exprType->lookupFieldHandle(this->typePool, std::string(OP_TO_STR));
        assert(handle != 0);
        node->setFieldIndex(handle->getFieldIndex());
        return;
    }

    /**
     * check cast
     */
    if(exprType->isAssignableFrom(targetType)) {
        node->setOpKind(CastNode::CHECK_CAST);
        return;
    }

    E_CastOp(node, exprType->getTypeName(), targetType->getTypeName());
}

void TypeChecker::visitInstanceOfNode(InstanceOfNode *node) {
    DSType *exprType = this->checkType(node->getTargetNode());
    DSType *targetType = this->toType(node->removeTargetTypeToken());
    node->setTargetType(targetType);


    if(targetType->isAssignableFrom(exprType)) {
        node->setOpKind(InstanceOfNode::ALWAYS_TRUE);
    } else if(exprType->isAssignableFrom(targetType)) {
        node->setOpKind(InstanceOfNode::INSTANCEOF);
    } else {
        node->setOpKind(InstanceOfNode::ALWAYS_FALSE);
    }
    node->setType(this->typePool->getBooleanType());
}

void TypeChecker::visitBinaryOpNode(BinaryOpNode *node) {
    DSType *leftType = this->checkType(node->getLeftNode());
    DSType *rightType = this->checkType(node->getRightNode());

    if(this->supportCoercion(rightType, leftType)) {    // cast leftNode.
        node->setLeftNode(this->intToFloat(node->getLeftNode()));
    }
    ApplyNode *applyNode = node->creatApplyNode();
    node->setType(this->checkType(applyNode));
}

void TypeChecker::visitArgsNode(ArgsNode *node) {
    // not call it
}

void TypeChecker::visitApplyNode(ApplyNode *node) {
    /**
     * resolve handle
     */
    Node *recvNode = node->getRecvNode();
    HandleOrFuncType hf = this->resolveCallee(recvNode, node);

    /**
     * check type arg nodes
     */
    if(hf.treatAsHandle()) {
        this->checkTypeArgsNode(hf.getHandle(), node->getArgsNode(), node->isFuncCall());
        node->setType(hf.getHandle()->getReturnType());
    } else {
        this->checkTypeArgsNode(hf.getFuncType(), node->getArgsNode(), node->isFuncCall());
        node->setType(hf.getFuncType()->getReturnType());
    }
}

void TypeChecker::visitNewNode(NewNode *node) {
    DSType *type = this->toType(node->removeTargetTypeToken());
    FunctionHandle *handle = type->getConstructorHandle(this->typePool);
    if(handle == 0) {
        E_UndefinedInit(node, type->getTypeName());
    }
    this->checkTypeArgsNode(handle, node->getArgsNode(), false);
    node->setType(type);
}

void TypeChecker::visitCondOpNode(CondOpNode *node) {
    DSType *booleanType = this->typePool->getBooleanType();
    this->checkType(booleanType, node->getLeftNode());
    this->checkType(booleanType, node->getRightNode());
    node->setType(booleanType);
}

void TypeChecker::visitCmdNode(CmdNode *node) {
    for(CmdArgNode *argNode : node->getArgNodes()) {
        this->checkTypeAsStatement(argNode);    // always void
    }
    // check type redirect options
    for(const std::pair<int, Node*> &optionPair : node->getRedirOptions()) {
        this->checkTypeAsStatement(optionPair.second);
    }
    node->setType(this->typePool->getVoidType());   // FIXME
}

void TypeChecker::visitCmdArgNode(CmdArgNode *node) {
    for(Node *exprNode : node->getSegmentNodes()) {
        this->checkType(exprNode);
    }
    node->setType(this->typePool->getVoidType());   //FIXME
}

void TypeChecker::visitSpecialCharNode(SpecialCharNode *node) {
    E_Unimplemented(node, "SpecialCharNode");
} //TODO

void TypeChecker::visitPipedCmdNode(PipedCmdNode *node) {
    for(CmdNode *procNode : node->getCmdNodes()) {
        this->checkTypeAsStatement(procNode);   // always void
    }
    node->setType(this->typePool->getVoidType());   //FIXME
}

void TypeChecker::visitCmdContextNode(CmdContextNode *node) {   //TODO: return type, attribute
    this->checkTypeAsStatement(node->getExprNode());    // FIXME:
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitAssertNode(AssertNode *node) {
    this->checkType(this->typePool->getBooleanType(), node->getExprNode());
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitBlockNode(BlockNode *node) {
    int count = 0;
    int size = node->getNodeList().size();
    for(Node *targetNode : node->getNodeList()) {
        this->checkTypeAsStatement(targetNode);
        if(dynamic_cast<BlockEndNode*>(targetNode) != 0 && (count != size - 1)) {
            E_Unreachable(node);
        }
        count++;
    }
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitBreakNode(BreakNode *node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkAndThrowIfOutOfLoop(node);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitContinueNode(ContinueNode *node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkAndThrowIfOutOfLoop(node);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitExportEnvNode(ExportEnvNode *node) {
    DSType *stringType = this->typePool->getStringType();
    this->addEntryAndThrowIfDefined(node, node->getEnvName(), stringType, true);
    this->checkType(stringType, node->getExprNode());
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitImportEnvNode(ImportEnvNode *node) {
    DSType *stringType = this->typePool->getStringType();
    FieldHandle *handle = this->addEntryAndThrowIfDefined(node, node->getEnvName(), stringType, true);
    node->setAttribute(handle);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitForNode(ForNode *node) {
    this->symbolTable.enterScope();
    this->checkTypeAsStatement(node->getInitNode());
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->checkTypeAsStatement(node->getIterNode());
    this->enterLoop();
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());
    this->exitLoop();
    this->symbolTable.exitScope();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitWhileNode(WhileNode *node) {
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->enterLoop();
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    this->exitLoop();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitDoWhileNode(DoWhileNode *node) {
    this->enterLoop();
    this->symbolTable.enterScope();
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());

    /**
     * block node and cond node is same scope.
     */
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());

    this->symbolTable.exitScope();
    this->exitLoop();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitIfNode(IfNode *node) {
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->checkTypeWithNewBlockScope(node->getThenNode());

    unsigned int size = node->getElifCondNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        this->checkType(this->typePool->getBooleanType(), node->getElifCondNodes()[i]);
        this->checkTypeWithNewBlockScope(node->getElifThenNodes()[i]);
    }

    this->checkTypeWithNewBlockScope(node->getElseNode());
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitReturnNode(ReturnNode *node) {
    this->checkAndThrowIfInsideFinally(node);
    DSType *returnType = this->getCurrentReturnType();
    if(returnType == 0) {
        E_InsideFunc(node);
    }
    DSType *exprType = this->checkType(returnType, node->getExprNode());
    if(exprType->equals(this->typePool->getVoidType())) {
        if(dynamic_cast<EmptyNode*>(node->getExprNode()) == 0) {
            E_NotNeedExpr(node);
        }
    }
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitThrowNode(ThrowNode *node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkType(node->getExprNode());
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitCatchNode(CatchNode *node) {
    DSType *exceptionType = this->toType(node->removeTypeToken());
    node->setExceptionType(exceptionType);

    /**
     * check type catch block
     */
    this->symbolTable.enterScope();
    this->addEntryAndThrowIfDefined(node, node->getExceptionName(), exceptionType, true);
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());
    this->symbolTable.exitScope();
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitTryNode(TryNode *node) {
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    // check type catch block
    for(CatchNode *c : node->getCatchNodes()) {
        this->checkTypeAsStatement(c);
    }

    // check type finally block, may be empty node
    this->checkTypeAsStatement(node->getFinallyNode());

    /**
     * verify catch block order
     */
    int size = node->getCatchNodes().size();
    for(int i = 0; i < size - 1; i++) {
        DSType *curType = node->getCatchNodes()[i]->getExceptionType();
        CatchNode *nextNode = node->getCatchNodes()[i + 1];
        DSType *nextType = nextNode->getExceptionType();
        if(curType->isAssignableFrom(nextType)) {
            E_Unreachable(nextNode);
        }
    }
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitFinallyNode(FinallyNode *node) {
    this->finallyContextStack.push_back(true);
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    this->finallyContextStack.pop_back();
}

void TypeChecker::visitVarDeclNode(VarDeclNode *node) {
    DSType *initValueType = this->checkType(node->getInitValueNode());
    FieldHandle *handle =
            this->addEntryAndThrowIfDefined(node, node->getVarName(), initValueType, node->isReadOnly());
    node->setAttribute(handle);
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitAssignNode(AssignNode *node) {
    AssignableNode *leftNode = dynamic_cast<AssignableNode*>(node->getLeftNode());
    if(leftNode == 0) {
        E_Assignable(node);
    }

    DSType *leftType = this->checkType(leftNode);
    if(leftNode->isReadOnly()) {
        E_ReadOnly(node);
    }

    if(node->isSelfAssignment()) {
        BinaryOpNode *opNode = dynamic_cast<BinaryOpNode*>(node->getRightNode());
        opNode->getLeftNode()->setType(leftType);
        AccessNode *accessNode = dynamic_cast<AccessNode*>(leftNode);
        if(accessNode != 0) {
            accessNode->setAdditionalOp(AccessNode::DUP_RECV);
        }
    }
    node->setRightNode(this->checkTypeAndResolveCoercion(leftType, node->getRightNode()));
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitFunctionNode(FunctionNode *node) {
    E_Unimplemented(node, "FunctionNode");
} //TODO

void TypeChecker::visitEmptyNode(EmptyNode *node) {
    node->setType(this->typePool->getVoidType());
}

void TypeChecker::visitDummyNode(DummyNode *node) {
    // do nothing.
}
