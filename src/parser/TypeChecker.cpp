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
#include <ast/node_utils.h>
#include <parser/TypeChecker.h>
#include <parser/TypeCheckError.h>

TypeChecker::TypeChecker(TypePool *typePool) :
        typePool(typePool), symbolTable(), curReturnType(0), loopContextStack(), finallyContextStack() {
}

TypeChecker::~TypeChecker() {
    this->finallyContextStack.clear();
}

void TypeChecker::checkTypeRootNode(RootNode *rootNode) {	//FIXME
    for(Node *node : rootNode->getNodeList()) {
        this->checkTypeAcceptingVoidType(node);
    }
}

// type check entry point

DSType *TypeChecker::checkTypeAcceptingVoidType(Node *targetNode) {
    return this->checkType(0, targetNode, 0);
}

DSType *TypeChecker::checkType(Node *targetNode) {
    return this->checkType(0, targetNode, this->typePool->getVoidType());
}

DSType *TypeChecker::checkType(DSType *requiredType, Node *targetNode) {
    return this->checkType(requiredType, targetNode, 0);
}

//TODO:
DSType *TypeChecker::checkType(DSType *requiredType, Node *targetNode, DSType *unacceptableType) {
    /**
     * if target node is statement, always check type.
     */
    ExprNode *exprNode = dynamic_cast<ExprNode*>(targetNode);
    if(exprNode == 0) {
        targetNode->accept(this);
        return 0;
    }

    /**
     * if target node is expr node and type is null,
     * try type check.
     */
    if(exprNode->getType() == 0) {
        exprNode->accept(this);
    }

    /**
     * after type checking, if type is still null,
     * throw exception.
     */
    DSType *type = exprNode->getType();
    if(type == 0) {
        E_Unresolved(exprNode);
    }

    /**
     * do not try type matching.
     */
    if(requiredType == 0) {
        if(unacceptableType != 0 && unacceptableType->isAssignableFrom(type)) {
            E_Unacceptable(exprNode, type->getTypeName());
        }
        return type;
    }

    /**
     * try type matching.
     */
    if(!requiredType->isAssignableFrom(type)) {
        E_Required(exprNode, requiredType->getTypeName(), type->getTypeName());
    }
    return type;
}

void TypeChecker::checkTypeWithNewBlockScope(BlockNode *blockNode) {
    this->symbolTable.enterScope();
    this->checkTypeWithCurrentBlockScope(blockNode);
    this->symbolTable.exitScope();
}

void TypeChecker::checkTypeWithCurrentBlockScope(BlockNode *blockNode) {
    blockNode->accept(this);
}

void TypeChecker::addEntryAndThrowIfDefined(Node *node, const std::string &symbolName, DSType *type,
        bool readOnly) {
    if(!this->symbolTable.registerHandle(symbolName, type, readOnly)) {
        E_DefinedSymbol(node, symbolName);
    }
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
        return this->findBlockEnd(ifNode->getThenNode())
                && this->findBlockEnd(ifNode->getElseNode());
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
    } catch(TypeLookupException &e) {
        int lineNum = typeToken->getLineNum();
        delete typeToken;
        throw TypeCheckException(lineNum, e.getTemplate(), e.getArgs());
    }
}

// for ApplyNode type checking
TypeChecker::HandleOrFuncType TypeChecker::resolveCallee(ExprNode *recvNode, ApplyNode *applyNode) {
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

TypeChecker::HandleOrFuncType TypeChecker::resolveCallee(AccessNode *recvNode, ApplyNode *applyNode) {
    DSType *actualRecvType = this->checkType(recvNode->getRecvNode());
    FieldHandle *handle = actualRecvType->lookupFieldHandle(recvNode->getFieldName());
    if(handle == 0) {
        E_UndefinedField(recvNode, recvNode->getFieldName());
    }

    recvNode->setHandle(handle);
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

TypeChecker::HandleOrFuncType TypeChecker::resolveCallee(VarNode *recvNode, ApplyNode *applyNode) {
    FieldHandle *handle = this->symbolTable.lookupHandle(recvNode->getVarName());
    if(handle == 0) {
        E_UndefinedSymbol(recvNode, recvNode->getVarName());
    }
    recvNode->setHandle(handle);
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
    // check named arg existence
    bool foundNamedArg = false;
    for(const std::pair<std::string, ExprNode*> &argPair : argsNode->getArgPairs()) {
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
    if(!foundNamedArg) {
        this->checkTypeArgsNode(handle->getParamTypes(this->typePool), argsNode, isFuncCall);
        return;
    }

    // check param size
    const std::vector<DSType*> &paramTypes = handle->getParamTypes(this->typePool);
    unsigned int paramSize = paramTypes.size();
    unsigned int argSize = argsNode->getArgPairs().size();
    if(argSize > (paramSize - (isFuncCall ? 0 : 1))) {
        E_UnmatchParam(argsNode,
                std::to_string(paramSize - (isFuncCall ? 0 : 1)),
                std::to_string(argSize));
    }

    // resolve named param index
    argsNode->initIndexMap();
    argsNode->setParamSize(paramSize);
    unsigned int count = 0;
    for(const std::pair<std::string, ExprNode*> &argPair : argsNode->getArgPairs()) {
        int index = handle->getParamIndex(argPair.first);
        if(index == -1) {
            E_UnfoundNamedParam(argsNode, argPair.first);
        }
        argsNode->addParamIndex(count++, index);
    }

    // check argument duplication
    bool foundIndexMap[paramSize];
    // init with false
    for(unsigned int i = 0; i < paramSize; i++) {
        foundIndexMap[i] = false;
    }
    for(unsigned int i = 0; i < argSize; i++) {
        if(foundIndexMap[argsNode->getParamIndexMap()[i]]) {
            E_DupNamedArg(argsNode, argsNode->getArgPairs()[i].first);
        }
        foundIndexMap[argsNode->getParamIndexMap()[i]] = true;
    }

    // check default value existence
    for(unsigned int i = isFuncCall ? 0 : 1; i < paramSize; i++) {
        if(!foundIndexMap[i] && !handle->hasDefaultValue(i)) {
            E_NoDefaultValue(argsNode);
        }
    }

    // check type each arg
    for(unsigned int i = isFuncCall ? 0 : 1; i < argSize; i++) {
        this->checkType(paramTypes[argsNode->getParamIndexMap()[i]],
                argsNode->getArgPairs()[i].second);
    }
}

void TypeChecker::checkTypeArgsNode(FunctionType *funcType, ArgsNode *argsNode, bool isFuncCall) {
    // check has no named arg
    for(const std::pair<std::string, ExprNode*> &argPair : argsNode->getArgPairs()) {
        if(argPair.first != "") {
            E_UnneedNamedArg(argsNode);
        }
    }

    // check type each node
    this->checkTypeArgsNode(funcType->getParamTypes(), argsNode, isFuncCall);
}

void TypeChecker::checkTypeArgsNode(const std::vector<DSType*> &paramTypes, ArgsNode *argsNode, bool isFuncCall) {
    unsigned int size = paramTypes.size();
    unsigned int argSize = argsNode->getArgPairs().size();
    // check param size
    if((size - (isFuncCall ? 0 : 1)) != argSize) {
        E_UnmatchParam(argsNode,
                std::to_string(size - (isFuncCall ? 0 : 1)),
                std::to_string(argSize));
    }

    // check type each node
    for(unsigned int i = isFuncCall ? 0 : 1; i < size; i++) {
        this->checkType(paramTypes[i], argsNode->getArgPairs()[i].second);
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

int TypeChecker::visitIntValueNode(IntValueNode *node) {	//TODO: int8, int16 ..etc
    node->setType(this->typePool->getIntType());
    return 0;
}

int TypeChecker::visitFloatValueNode(FloatValueNode *node) {
    node->setType(this->typePool->getFloatType());
    return 0;
}

int TypeChecker::visitBooleanValueNode(BooleanValueNode *node) {
    node->setType(this->typePool->getBooleanType());
    return 0;
}

int TypeChecker::visitStringValueNode(StringValueNode *node) {
    node->setType(this->typePool->getStringType());
    return 0;
}

int TypeChecker::visitStringExprNode(StringExprNode *node) {
    for(ExprNode *exprNode : node->getExprNodes()) {
        this->checkType(this->typePool->getStringType(), exprNode);
    }
    node->setType(this->typePool->getStringType());
    return 0;
}

int TypeChecker::visitArrayNode(ArrayNode *node) {
    int size = node->getExprNodes().size();
    assert(size != 0);
    ExprNode *firstElementNode = node->getExprNodes()[0];
    DSType *elementType = this->checkType(firstElementNode);

    for(int i = 1; i < size; i++) {
        this->checkType(elementType, node->getExprNodes()[i]);
    }

    DSType *baseArrayType = this->typePool->getBaseArrayType(); //FIXME:
    std::vector<DSType*> elementTypes(1);
    elementTypes.push_back(elementType);
    node->setType(this->typePool->createAndGetReifiedTypeIfUndefined(baseArrayType, elementTypes));
    return 0;
}

int TypeChecker::visitMapNode(MapNode *node) {
    int size = node->getValueNodes().size();
    assert(size != 0);
    ExprNode *firstValueNode = node->getValueNodes()[0];
    DSType *valueType = this->checkType(firstValueNode);

    for(int i = 0; i < size; i++) {
        this->checkType(this->typePool->getStringType(), node->getKeyNodes()[i]);
        this->checkType(valueType, node->getValueNodes()[i]);
    }

    DSType *baseMapType = this->typePool->getBaseMapType(); //FIXME:
    std::vector<DSType*> elementTypes(1);
    elementTypes.push_back(valueType);
    node->setType(this->typePool->createAndGetReifiedTypeIfUndefined(baseMapType, elementTypes));
    return 0;
}

int TypeChecker::visitPairNode(PairNode *node) {
    DSType *leftType = this->checkType(node->getLeftNode());
    DSType *rightType = this->checkType(node->getRightNode());

    DSType *basePairType = this->typePool->getBasePairType();   //FIXME:
    std::vector<DSType*> elementTypes(2);
    elementTypes.push_back(leftType);
    elementTypes.push_back(rightType);
    node->setType(this->typePool->createAndGetReifiedTypeIfUndefined(basePairType, elementTypes));
    return 0;
}

int TypeChecker::visitVarNode(VarNode *node) {
    FieldHandle *handle = this->symbolTable.lookupHandle(node->getVarName());
    if(handle == 0) {
        E_UndefinedSymbol(node, node->getVarName());
    }

    node->setHandle(handle);
    node->setType(handle->getFieldType(this->typePool));
    return 0;
}

int TypeChecker::visitAccessNode(AccessNode *node) {
    DSType *recvType = this->checkType(node->getRecvNode());
    FieldHandle *handle = recvType->lookupFieldHandle(node->getFieldName());
    if(handle == 0) {
        E_UndefinedField(node, node->getFieldName());
    }

    node->setHandle(handle);
    node->setType(handle->getFieldType(this->typePool));
    return 0;
}

int TypeChecker::visitCastNode(CastNode *node) {
    E_Unimplemented(node, "CastNode");
    return 0;
} //TODO

int TypeChecker::visitInstanceOfNode(InstanceOfNode *node) {
    DSType *exprType = this->checkType(node->getTargetNode());
    DSType *targetType = this->toType(node->removeTargetTypeToken());
    node->setTargetType(targetType);

    if(exprType->isAssignableFrom(targetType) || targetType->isAssignableFrom(exprType)) {
        node->resolveOpKind(InstanceOfNode::INSTANCEOF);
    } else {
        node->resolveOpKind(InstanceOfNode::ALWAYS_FALSE);
    }
    node->setType(this->typePool->getBooleanType());
    return 0;
}

int TypeChecker::visitOperatorCallNode(OperatorCallNode *node) {
    const std::vector<ExprNode*> argNodes = node->getArgNodes();
    for(ExprNode *argNode : argNodes) {
        this->checkType(argNode);
    }
    DSType *recvType = argNodes[0]->getType();

    // lookup handle
    const std::string opName = resolveOpName(node->getOp());
    FunctionHandle *handle = 0;
    if(argNodes.size() == 1) {
        handle = recvType->lookupMethodHandle(opName);
    } else {    // resolve overload
        std::string namePrefix = opName.substr(0, opName.size() - 2);
        for(int i = 1; i < 5; i++) {
            handle = recvType->lookupMethodHandle(i == 1 ?
                    opName : namePrefix + std::to_string(i) + "__");
            if(handle != 0) {
                const std::vector<DSType*> &paramTypes = handle->getParamTypes(this->typePool);
                if(paramTypes.size() == 2 &&
                        paramTypes[1]->isAssignableFrom(argNodes[1]->getType())) {
                    break;
                }
                handle = 0;
            }
        }
    }
    if(handle == 0) {
        E_UndefinedMethod(node, opName);
    }

    // check param size
    const std::vector<DSType*> &paramTypes = handle->getParamTypes(this->typePool);
    unsigned int size = paramTypes.size();
    if(size != argNodes.size()) {
        E_UnmatchParam(node, std::to_string(size), std::to_string(argNodes.size()));
    }

    // try type match
    for(unsigned int i = 0; i < size; i++) {
        this->checkType(paramTypes[i], argNodes[i]);
    }
    node->setHandle(handle);
    node->setType(handle->getReturnType(this->typePool));
    return 0;
}

int TypeChecker::visitArgsNode(ArgsNode *node) {
    return 0;   // not call it
}

int TypeChecker::visitApplyNode(ApplyNode *node) {
    /**
     * resolve handle
     */
    ExprNode *recvNode = node->getRecvNode();
    HandleOrFuncType hf = this->resolveCallee(recvNode, node);

    /**
     * check type arg nodes
     */
    if(hf.treatAsHandle()) {
        this->checkTypeArgsNode(hf.getHandle(), node->getArgsNode(), node->isFuncCall());
        node->setType(hf.getHandle()->getReturnType(this->typePool));
    } else {
        this->checkTypeArgsNode(hf.getFuncType(), node->getArgsNode(), node->isFuncCall());
        node->setType(hf.getFuncType()->getReturnType());
    }
    return 0;
}

int TypeChecker::visitNewNode(NewNode *node) {
    DSType *type = this->toType(node->removeTargetTypeToken());
    ConstructorHandle *handle = type->getConstructorHandle();
    if(handle == 0) {
        E_UndefinedInit(node, type->getTypeName());
    }
    this->checkTypeArgsNode(handle, node->getArgsNode(), false);
    node->setType(type);
    return 0;
}

int TypeChecker::visitCondOpNode(CondOpNode *node) {
    DSType *booleanType = this->typePool->getBooleanType();
    this->checkType(booleanType, node->getLeftNode());
    this->checkType(booleanType, node->getRightNode());
    node->setType(booleanType);
    return 0;
}

int TypeChecker::visitProcessNode(ProcessNode *node) {
    for(ProcArgNode *argNode : node->getArgNodes()) {
        this->checkTypeAcceptingVoidType(argNode);    //FIXME: accept void type
    }
    // check type redirect options
    for(const std::pair<int, ExprNode*> &optionPair : node->getRedirOptions()) {
        this->checkTypeAcceptingVoidType(optionPair.second);  //FIXME: accept void type
    }
    node->setType(this->typePool->getVoidType());   //FIXME: ProcessNode is always void type
    return 0;
}

int TypeChecker::visitProcArgNode(ProcArgNode *node) {
    for(ExprNode *exprNode : node->getSegmentNodes()) {
        this->checkType(exprNode);
    }
    node->setType(this->typePool->getVoidType());   //FIXME: ProcArgNode is always void type
    return 0;
}

int TypeChecker::visitSpecialCharNode(SpecialCharNode *node) {
    E_Unimplemented(node, "SpecialCharNode");
    return 0;
} //TODO

int TypeChecker::visitTaskNode(TaskNode *node) {    //TODO: parent node
    for(ProcessNode *procNode : node->getProcNodes()) {
        this->checkTypeAcceptingVoidType(procNode);   //FIXME: accept void
    }

    /**
     * resolve task type
     */
    node->setType(this->typePool->getVoidType());
    return 0;
}

int TypeChecker::visitInnerTaskNode(InnerTaskNode *node) {
    E_Unimplemented(node, "InnerTaskNode");
    return 0;
} //TODO

int TypeChecker::visitAssertNode(AssertNode *node) {
    this->checkType(this->typePool->getBooleanType(), node->getExprNode());
    return 0;
}

int TypeChecker::visitBlockNode(BlockNode *node) {
    int count = 0;
    int size = node->getNodeList().size();
    for(Node *targetNode : node->getNodeList()) {
        this->checkTypeAcceptingVoidType(targetNode);
        if(dynamic_cast<BlockEndNode*>(targetNode) != 0 && (count != size - 1)) {
            E_Unreachable(node);
        }
        count++;
    }
    return 0;
}

int TypeChecker::visitBreakNode(BreakNode *node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkAndThrowIfOutOfLoop(node);
    return 0;
}

int TypeChecker::visitContinueNode(ContinueNode *node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkAndThrowIfOutOfLoop(node);
    return 0;
}

int TypeChecker::visitExportEnvNode(ExportEnvNode *node) {
    DSType *stringType = this->typePool->getStringType();
    this->addEntryAndThrowIfDefined(node, node->getEnvName(), stringType, true);
    this->checkType(stringType, node->getExprNode());
    return 0;
}

int TypeChecker::visitImportEnvNode(ImportEnvNode *node) {
    DSType *stringType = this->typePool->getStringType();
    this->addEntryAndThrowIfDefined(node, node->getEnvName(), stringType, true);
    return 0;
}

int TypeChecker::visitForNode(ForNode *node) {
    this->symbolTable.enterScope();
    this->checkTypeAcceptingVoidType(node->getInitNode());
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->checkTypeAcceptingVoidType(node->getIterNode());
    this->enterLoop();
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());
    this->exitLoop();
    this->symbolTable.exitScope();
    return 0;
}

int TypeChecker::visitWhileNode(WhileNode *node) {
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->enterLoop();
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    this->exitLoop();
    return 0;
}

int TypeChecker::visitIfNode(IfNode *node) {
    this->checkType(this->typePool->getBooleanType(), node->getCondNode());
    this->checkTypeWithNewBlockScope(node->getThenNode());
    this->checkTypeWithNewBlockScope(node->getElseNode());
    return 0;
}

int TypeChecker::visitReturnNode(ReturnNode *node) {
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
    return 0;
}

int TypeChecker::visitThrowNode(ThrowNode *node) {
    this->checkAndThrowIfInsideFinally(node);
    this->checkType(node->getExprNode()); //TODO: currently accept all type
    return 0;
}

int TypeChecker::visitCatchNode(CatchNode *node) {
    DSType *exceptionType = this->toType(node->removeTypeToken());
    node->setExceptionType(exceptionType);

    /**
     * check type catch block
     */
    this->symbolTable.enterScope();
    this->addEntryAndThrowIfDefined(node, node->getExceptionName(), exceptionType, true);
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());
    this->symbolTable.exitScope();
    return 0;
}

int TypeChecker::visitTryNode(TryNode *node) {
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    // check type catch block
    for(CatchNode *c : node->getCatchNodes()) {
        this->checkType(c);
    }

    // check type finally block, may be empty node
    this->checkTypeAcceptingVoidType(node->getFinallyNode());

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
    return 0;
}

int TypeChecker::visitFinallyNode(FinallyNode *node) {
    this->finallyContextStack.push_back(true);
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    this->finallyContextStack.pop_back();
    return 0;
}

int TypeChecker::visitVarDeclNode(VarDeclNode *node) {
    DSType *initValueType = this->checkType(node->getInitValueNode());
    this->addEntryAndThrowIfDefined(node, node->getVarName(), initValueType, node->isReadOnly());
    node->setGlobal(this->symbolTable.inGlobalScope());
    return 0;
}

int TypeChecker::visitAssignNode(AssignNode *node) {
    AssignableNode *leftNode = dynamic_cast<AssignableNode*>(node->getLeftNode());
    if(leftNode == 0) {
        E_Assignable(node);
    }
    if(leftNode->isReadOnly()) {
        E_ReadOnly(node);
    }

    this->checkType(leftNode);
    this->checkType(node->getRightNode());
    node->setType(this->typePool->getVoidType());
    return 0;
}

int TypeChecker::visitFieldSelfAssignNode(FieldSelfAssignNode *node) {
    ApplyNode *applyNode = node->getApplyNode();
    this->checkType(applyNode);

    AccessNode *accessNode = dynamic_cast<AccessNode*>(applyNode->getRecvNode());
    accessNode->setAdditionalOp(AccessNode::DUP_RECV);

    node->setType(this->typePool->getVoidType());
    return 0;
}

int TypeChecker::visitFunctionNode(FunctionNode *node) {
    E_Unimplemented(node, "FunctionNode");
    return 0;
} //TODO

int TypeChecker::visitEmptyNode(EmptyNode *node) {
    node->setType(this->typePool->getVoidType());
    return 0;
}
