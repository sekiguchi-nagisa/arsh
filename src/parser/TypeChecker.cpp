/*
 * TypeChecker.cpp
 *
 *  Created on: 2015/01/07
 *      Author: skgchxngsxyz-osx
 */

#include <assert.h>
#include <vector>

#include "TypeChecker.h"
#include "TypeError.h"
#include "../core/magic_method.h"

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

void TypeChecker::checkTypeAcceptingVoidType(Node *targetNode) {
    this->checkType(0, targetNode, 0);
}

void TypeChecker::checkType(Node *targetNode) {
    this->checkType(0, targetNode, this->typePool->getVoidType());
}

void TypeChecker::checkType(DSType *requiredType, Node *targetNode) {
    this->checkType(requiredType, targetNode, 0);
}

//TODO:
void TypeChecker::checkType(DSType *requiredType, Node *targetNode, DSType *unacceptableType) {
    /**
     * if target node is statement, always check type.
     */
    ExprNode *exprNode = dynamic_cast<ExprNode*>(targetNode);
    if(exprNode == 0) {
        targetNode->accept(this);
        return;
    }

    /**
     * if target node is expr node and type is null,
     * try type check.
     */
    if(exprNode->getType() == 0) {
        //exprNode = (ExprNode) exprNode.accept(this);
        exprNode->accept(this);
    }

    /**
     * after type checking, if type is still null,
     * throw exception.
     */
    DSType *type = exprNode->getType();
    if(type == 0) {
        E_Unresolved->report(exprNode->getLineNum());
        return;
    }

    /**
     * do not try type matching.
     */
    if(requiredType == 0) {
        if(unacceptableType != 0 && unacceptableType->isAssignableFrom(type)) {
            E_Unacceptable->report(exprNode->getLineNum(), type->getTypeName());
            return;
        }
        return;
    }

    /**
     * try type matching.
     */
    if(requiredType->isAssignableFrom(type)) {
        return;
    }
    E_Required->report(exprNode->getLineNum(), requiredType->getTypeName(), type->getTypeName());
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
    if(!this->symbolTable.addEntry(symbolName, type, readOnly)) {
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
    this->checkType(firstElementNode);
    DSType *elementType = firstElementNode->getType();

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
    this->checkType(firstValueNode);
    DSType *valueType = firstValueNode->getType();

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
    this->checkType(node->getLeftNode());
    this->checkType(node->getRightNode());

    DSType *basePairType = this->typePool->getBasePairType();   //FIXME:
    std::vector<DSType*> elementTypes(2);
    elementTypes.push_back(node->getLeftNode()->getType());
    elementTypes.push_back(node->getRightNode()->getType());
    node->setType(this->typePool->createAndGetReifiedTypeIfUndefined(basePairType, elementTypes));
    return 0;
}

int TypeChecker::visitVarNode(VarNode *node) {
    SymbolEntry *entry = this->symbolTable.getEntry(node->getVarName());
    if(entry == 0) {
        E_UndefinedSymbol->report(node->getLineNum(), node->getVarName());
    }

    node->setGlobal(entry->isGlobal());
    node->setReadOnly(entry->isReadOnly());
    node->setVarIndex(entry->getVarIndex());
    node->setType(entry->getType(this->typePool));
    return 0;
}

int TypeChecker::visitIndexNode(IndexNode *node) {
    this->checkType(node->getRecvNode());
    DSType *recvType = node->getRecvNode()->getType();
    FunctionHandle *handle = recvType->lookupMethodHandle(GET);
    if(handle == 0 || handle->getParamTypes(this->typePool).size() != 2) {
        E_UndefinedMethod->report(node->getLineNum(), GET);
    }

    this->checkType(handle->getParamTypes(this->typePool)[1], node->getIndexNode());
    node->setGetterHandle(handle);
    node->setType(handle->getReturnType(this->typePool));
    return 0;
}

int TypeChecker::visitAccessNode(AccessNode *node) {
    this->checkType(node->getRecvNode());
    DSType *recvType = node->getRecvNode()->getType();
    FieldHandle *handle = recvType->lookupFieldHandle(node->getFieldName());
    if(handle == 0) {
        E_UndefinedField->report(node->getLineNum(), node->getFieldName());
    }

    node->setFieldIndex(handle->getFieldIndex());
    node->setType(handle->getFieldType(this->typePool));
    return 0;
}

int TypeChecker::visitCastNode(CastNode *node) {
    E_Unimplemented->report(node->getLineNum(), "CastNode");
    return 0;
} //TODO
int TypeChecker::visitInstanceOfNode(InstanceOfNode *node) {
    E_Unimplemented->report(node->getLineNum(), "InstanceOfNode");
    return 0;
} //TODO
int TypeChecker::visitApplyNode(ApplyNode *node) {
    E_Unimplemented->report(node->getLineNum(), "ApplyNode");
    return 0;
} //TODO
int TypeChecker::visitConstructorCallNode(ConstructorCallNode *node) {
    E_Unimplemented->report(node->getLineNum(), "ConstructorCallNode");
    return 0;
} //TODO

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
    this->checkType(returnType, node->getExprNode());
    if(node->getExprNode()->getType()->equals(this->typePool->getVoidType())) {
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
    this->checkType(node->getInitValueNode());
    this->addEntryAndThrowIfDefined(node, node->getVarName(),
            node->getInitValueNode()->getType(), node->isReadOnly());
    node->setGlobal(this->symbolTable.inGlobalScope());
    return 0;
}

int TypeChecker::visitAssignNode(AssignNode *node) {
    E_Unimplemented->report(node->getLineNum(), "AssignNode");
    return 0;
} //TODO
int TypeChecker::visitFunctionNode(FunctionNode *node) {
    E_Unimplemented->report(node->getLineNum(), "FunctionNode");
    return 0;
} //TODO

int TypeChecker::visitEmptyNode(EmptyNode *node) {
    node->setType(this->typePool->getVoidType());
    return 0;
}
