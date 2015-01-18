/*
 * TypeChecker.cpp
 *
 *  Created on: 2015/01/07
 *      Author: skgchxngsxyz-osx
 */

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
    int size = rootNode->getNodes().size();
    for(int i = 0; i < size; i++) {
        this->checkTypeAcceptingVoidType(rootNode->getNodes()[i]);
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
    if(dynamic_cast<EmptyBlockNode*>(blockNode) != 0) {
        return false;
    }
    int endIndex = blockNode->getNodes().size() - 1;
    if(endIndex < 0) {
        return false;
    }
    Node *endNode = blockNode->getNodes()[endIndex];
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
    int endIndex = blockNode->getNodes().size() - 1;
    Node *endNode = blockNode->getNodes()[endIndex];

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
    return 0;
} //TODO
int TypeChecker::visitMapNode(MapNode *node) {
    return 0;
} //TODO
int TypeChecker::visitPairNode(PairNode *node) {
    return 0;
} //TODO

int TypeChecker::visitVarNode(VarNode *node) {
    SymbolEntry *entry = this->symbolTable.getEntry(node->getVarName());
    if(entry == 0) {
        E_UndefinedSymbol->report(node->getLineNum(), node->getVarName());
    }

    node->setGlobal(entry->isGlobal());
    node->setReadOnly(entry->isReadOnly());
    node->setVarIndex(entry->getVarIndex());
    node->setType(entry->getType());
    return 0;
}

int TypeChecker::visitIndexNode(IndexNode *node) {
    this->checkType(node->getRecvNode());
    DSType *recvType = node->getRecvNode()->getType();
    FunctionHandle *handle = recvType->lookupMethodHandle(GET);
    if(handle == 0 || handle->getFuncType()->getParamSize() != 2) {
        E_UndefinedMethod->report(node->getLineNum(), GET);
    }

    FunctionType *funcType = handle->getFuncType();
    this->checkType(funcType->getParamTypes()[1], node->getIndexNode());
    node->setGetterHandle(handle);
    node->setType(funcType->getReturnType());
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
    node->setType(handle->getFieldType());
    return 0;
}

int TypeChecker::visitCastNode(CastNode *node) {
    return 0;
} //TODO
int TypeChecker::visitInstanceOfNode(InstanceOfNode *node) {
    return 0;
} //TODO
int TypeChecker::visitApplyNode(ApplyNode *node) {
    return 0;
} //TODO
int TypeChecker::visitConstructorCallNode(ConstructorCallNode *node) {
    return 0;
} //TODO
int TypeChecker::visitCondOpNode(CondOpNode *node) {
    return 0;
} //TODO

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
    return 0;
} //TODO

int TypeChecker::visitAssertNode(AssertNode *node) {
    this->checkType(this->typePool->getBooleanType(), node->getExprNode());
    return 0;
}

int TypeChecker::visitBlockNode(BlockNode *node) {
    int count = 0;
    int size = node->getNodes().size();
    for(Node *targetNode : node->getNodes()) {
        this->checkTypeAcceptingVoidType(targetNode);
        if(dynamic_cast<BlockEndNode*>(targetNode) != 0 && (count != size - 1)) {
            E_Unreachable->report(node->getLineNum());
            return -1;
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

int TypeChecker::visitForInNode(ForInNode *node) {
    this->checkType(node->getExprNode());
    DSType *exprType = node->getExprNode()->getType();

    // lookup RESET
    FunctionHandle *reset = exprType->lookupMethodHandle(RESET);
    if(reset != 0) {
        FunctionType *funcType = reset->getFuncType();
        if(funcType->getParamSize() != 1 ||
                !funcType->getReturnType()->equals(this->typePool->getVoidType())) {
            reset = 0;
        }
    }

    // lookup NEXT
    FunctionHandle *next = exprType->lookupMethodHandle(NEXT);
    if(next != 0) {
        FunctionType *funcType = next->getFuncType();
        if(funcType->getParamSize() != 1 ||
                funcType->getReturnType()->equals(this->typePool->getVoidType())) {
            next = 0;
        }
    }

    // lookup HAS_NEXT
    FunctionHandle *hasNext = exprType->lookupMethodHandle(HAS_NEXT);
    if(hasNext != 0) {
        FunctionType *funcType = hasNext->getFuncType();
        if(funcType->getParamSize() != 1 ||
                !funcType->getReturnType()->equals(this->typePool->getBooleanType())) {
            hasNext = 0;
        }
    }

    if(reset == 0 || next == 0 || hasNext == 0) {
        E_NoIterator->report(node->getLineNum(), exprType->getTypeName());
    }
    node->setIteratorHandle(reset, next, hasNext);

    // add symbol entry
    this->enterLoop();
    this->symbolTable.enterScope();
    this->addEntryAndThrowIfDefined(node, node->getInitName(), next->getFuncType()->getReturnType(), false);
    this->checkTypeWithCurrentBlockScope(node->getBlockNode());
    this->symbolTable.exitScope();
    this->exitLoop();
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
    return 0;
} //TODO
int TypeChecker::visitTryNode(TryNode *node) {
    return 0;
} //TODO

int TypeChecker::visitFinallyNode(FinallyNode *node) {
    this->finallyContextStack.push_back(true);
    this->checkTypeWithNewBlockScope(node->getBlockNode());
    this->finallyContextStack.pop_back();
    return 0;
}

int TypeChecker::visitVarDeclNode(VarDeclNode *node) {
    //TODO:
    return 0;
}

int TypeChecker::visitAssignNode(AssignNode *node) {
    return 0;
} //TODO
int TypeChecker::visitFunctionNode(FunctionNode *node) {
    return 0;
} //TODO

int TypeChecker::visitEmptyNode(EmptyNode *node) {
    node->setType(this->typePool->getVoidType());
    return 0;
}

int TypeChecker::visitEmptyBlockNode(EmptyBlockNode *node) {
    return 0;	// do nothing
}
