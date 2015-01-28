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
#include <parser/TypeChecker.h>
#include <parser/TypeError.h>

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
        E_Unresolved->report(exprNode->getLineNum());
    }

    /**
     * do not try type matching.
     */
    if(requiredType == 0) {
        if(unacceptableType != 0 && unacceptableType->isAssignableFrom(type)) {
            E_Unacceptable->report(exprNode->getLineNum(), type->getTypeName());
        }
        return type;
    }

    /**
     * try type matching.
     */
    if(!requiredType->isAssignableFrom(type)) {
        E_Required->report(exprNode->getLineNum(), requiredType->getTypeName(), type->getTypeName());
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
    if(!this->symbolTable.addHandle(symbolName, type, readOnly)) {
        E_DefinedSymbol->report(node->getLineNum(), symbolName);
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
    E_InsideLoop->report(node->getLineNum());
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
        E_UnfoundReturn->report(blockNode->getLineNum());
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
        E_InsideFinally->report(node->getLineNum());
    }
}

// for ApplyNode type checking
void TypeChecker::checkTypeAsConstructorCall(NewNode *recvNode, ApplyNode *applyNode) {
    DSType *type = this->checkType(recvNode);
    this->checkTypeArgNodes(recvNode->getHandle(), applyNode->getArgNodes());
    applyNode->setType(type);
}

void TypeChecker::checkTypeAsMethodCall(AccessNode *recvNode, ApplyNode *applyNode) {   //TODO: overload
    DSType *actualRecvType = this->checkType(recvNode->getRecvNode());
    FieldHandle *handle = actualRecvType->lookupFieldHandle(recvNode->getFieldName());
    if(handle == 0) {
        E_UndefinedField->report(recvNode->getLineNum(), recvNode->getFieldName());
    }

    recvNode->setFieldIndex(handle->getFieldIndex());
    FunctionHandle *funcHandle = dynamic_cast<FunctionHandle*>(handle);
    // treat as method call
    if(funcHandle != 0) {
        recvNode->setAdditionalOp(AccessNode::DUP_RECV_AND_SWAP);
        this->checkTypeArgNodes(funcHandle, applyNode->getArgNodes());
        applyNode->setType(funcHandle->getReturnType(this->typePool));
        return;
    }

    FunctionType *funcType = dynamic_cast<FunctionType*>(handle->getFieldType(this->typePool));
    // treat as method call
    if(funcType != 0 && funcType->treatAsMethod(actualRecvType)) {
        recvNode->setAdditionalOp(AccessNode::DUP_RECV_AND_SWAP);
        this->checkTypeArgNodes(funcType, applyNode->getArgNodes());
        applyNode->setType(funcType->getReturnType());
        return;
    }

    // treat as function call
    if(funcType != 0) {
        this->checkTypeAsFuncCall(recvNode, applyNode);
        return;
    }

    E_UndefinedMethod->report(recvNode->getLineNum(), recvNode->getFieldName());
}

void TypeChecker::checkTypeAsFuncCall(ExprNode *recvNode, ApplyNode *applyNode) {    //FIXME: direct function call, overload
    FunctionType *funcType =
            dynamic_cast<FunctionType*>(this->checkType(this->typePool->getBaseFuncType(), recvNode));
    applyNode->setFuncCall(true);
    this->checkTypeArgNodes(funcType, applyNode->getArgNodes());
    applyNode->setType(funcType->getReturnType());
}

void TypeChecker::checkTypeArgNodes(FunctionHandle *handle, const std::vector<ExprNode*> &argNodes) {
    //TODO:
}

void TypeChecker::checkTypeArgNodes(FunctionType *funcType, const std::vector<ExprNode*> &argNodes) {
    //TODO:
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
    FieldHandle *handle = this->symbolTable.getHandle(node->getVarName());
    if(handle == 0) {
        E_UndefinedSymbol->report(node->getLineNum(), node->getVarName());
    }

    node->setHandle(handle);
    node->setType(handle->getFieldType(this->typePool));
    return 0;
}

int TypeChecker::visitAccessNode(AccessNode *node) {
    DSType *recvType = this->checkType(node->getRecvNode());
    FieldHandle *handle = recvType->lookupFieldHandle(node->getFieldName());
    if(handle == 0) {
        E_UndefinedField->report(node->getLineNum(), node->getFieldName());
    }

    node->setFieldIndex(handle->getFieldIndex());
    node->setReadOnly(handle->isReadOnly());
    node->setType(handle->getFieldType(this->typePool));
    return 0;
}

int TypeChecker::visitCastNode(CastNode *node) {
    E_Unimplemented->report(node->getLineNum(), "CastNode");
    return 0;
} //TODO

int TypeChecker::visitInstanceOfNode(InstanceOfNode *node) {
    DSType *exprType = this->checkType(node->getTargetNode());
    TypeToken *t = node->removeTargetTypeToken();
    DSType *targetType = t->toType(this->typePool);
    node->setTargetType(targetType);
    delete t;

    if(exprType->isAssignableFrom(targetType) || targetType->isAssignableFrom(exprType)) {
        node->resolveOpKind(InstanceOfNode::INSTANCEOF);
    } else {
        node->resolveOpKind(InstanceOfNode::ALWAYS_FALSE);
    }
    node->setType(this->typePool->getBooleanType());
    return 0;
}

int TypeChecker::visitApplyNode(ApplyNode *node) {
    ExprNode *recvNode = node->getRecvNode();
    // check type as constructor call
    NewNode *newNode = dynamic_cast<NewNode*>(recvNode);
    if(newNode != 0) {
        this->checkTypeAsConstructorCall(newNode, node);
        return 0;
    }

    // check type as method call
    AccessNode *accessNode = dynamic_cast<AccessNode*>(recvNode);
    if(accessNode != 0) {
        this->checkTypeAsMethodCall(accessNode, node);
        return 0;
    }

    // check type as function call
    this->checkTypeAsFuncCall(recvNode, node);
    return 0;
}

int TypeChecker::visitNewNode(NewNode *node) {
    TypeToken *typeToken = node->removeTargetTypeToken();
    DSType *type = typeToken->toType(this->typePool);
    delete typeToken;

    ConstructorHandle *handle = type->getConstructorHandle();
    if(handle == 0) {
        E_UndefinedInit->report(node->getLineNum(), type->getTypeName());
    }
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
    E_Unimplemented->report(node->getLineNum(), "SpecialCharNode");
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
    E_Unimplemented->report(node->getLineNum(), "InnerTaskNode");
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
            E_Unreachable->report(node->getLineNum());
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
        E_InsideFunc->report(node->getLineNum());
    }
    DSType *exprType = this->checkType(returnType, node->getExprNode());
    if(exprType->equals(this->typePool->getVoidType())) {
        if(dynamic_cast<EmptyNode*>(node->getExprNode()) == 0) {
            E_NotNeedExpr->report(node->getLineNum());
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
    TypeToken *t = node->removeTypeToken();
    DSType *exceptionType = t->toType(this->typePool);
    delete t;

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
            E_Unreachable->report(nextNode->getLineNum());
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
        E_Assignable->report(node->getLineNum());
    }
    if(leftNode->isReadOnly()) {
        E_ReadOnly->report(node->getLineNum());
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
    E_Unimplemented->report(node->getLineNum(), "FunctionNode");
    return 0;
} //TODO

int TypeChecker::visitEmptyNode(EmptyNode *node) {
    node->setType(this->typePool->getVoidType());
    return 0;
}
