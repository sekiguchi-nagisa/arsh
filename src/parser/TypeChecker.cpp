/*
 * TypeChecker.cpp
 *
 *  Created on: 2015/01/07
 *      Author: skgchxngsxyz-osx
 */

#include "TypeChecker.h"

TypeChecker::TypeChecker(TypePool *typePool) :
        typePool(typePool), curReturnType(0), finallyContextStack() {
}

TypeChecker::~TypeChecker() {
    this->finallyContextStack.clear();
}

void TypeChecker::checkTypeRootNode(const std::unique_ptr<RootNode> &rootNode) {	//FIXME
    int size = rootNode->getNodes().size();
    for(int i = 0; i < size; i++) {
        this->checkTypeAcceptingVoidType(rootNode->getNodes()[i].get());
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
        //throw new TypeCheckException(exprNode, TypeErrorKind_ZeroArg.Unresolved);
        return;
    }

    /**
     * do not try type matching.
     */
    if(requiredType == 0) {
        if(unacceptableType != 0 && unacceptableType->isAssignableFrom(type)) {
            //throw new TypeCheckException(exprNode, TypeErrorKind_OneArg.Unacceptable, type);	//FIXME: error report
        }
        return;
    }

    /**
     * try type matching.
     */
    if(requiredType->isAssignableFrom(type)) {
        return;
    }

    //FIXME: error report
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
    int size = node->getExprNodes().size();
    for(int i = 0; i < size; i++) {
        this->checkType(this->typePool->getStringType(), node->getExprNodes()[i].get());
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
    return 0;
} //TODO
int TypeChecker::visitIndexNode(IndexNode *node) {
    return 0;
} //TODO
int TypeChecker::visitAccessNode(AccessNode *node) {
    return 0;
} //TODO
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
    return 0;
} //TODO
int TypeChecker::visitProcArgNode(ProcArgNode *node) {
    return 0;
} //TODO
int TypeChecker::visitSpecialCharNode(SpecialCharNode *node) {
    return 0;
} //TODO
int TypeChecker::visitTaskNode(TaskNode *node) {
    return 0;
} //TODO
int TypeChecker::visitInnerTaskNode(InnerTaskNode *node) {
    return 0;
} //TODO
int TypeChecker::visitAssertNode(AssertNode *node) {
    return 0;
} //TODO
int TypeChecker::visitBlockNode(BlockNode *node) {
    return 0;
} //TODO
int TypeChecker::visitBreakNode(BreakNode *node) {
    return 0;
} //TODO
int TypeChecker::visitContinueNode(ContinueNode *node) {
    return 0;
} //TODO
int TypeChecker::visitExportEnvNode(ExportEnvNode *node) {
    return 0;
} //TODO
int TypeChecker::visitImportEnvNode(ImportEnvNode *node) {
    return 0;
} //TODO
int TypeChecker::visitForNode(ForNode *node) {
    return 0;
} //TODO
int TypeChecker::visitForInNode(ForInNode *node) {
    return 0;
} //TODO
int TypeChecker::visitWhileNode(WhileNode *node) {
    return 0;
} //TODO
int TypeChecker::visitIfNode(IfNode *node) {
    return 0;
} //TODO
int TypeChecker::visitReturnNode(ReturnNode *node) {
    return 0;
} //TODO
int TypeChecker::visitThrowNode(ThrowNode *node) {
    return 0;
} //TODO
int TypeChecker::visitCatchNode(CatchNode *node) {
    return 0;
} //TODO
int TypeChecker::visitTryNode(TryNode *node) {
    return 0;
} //TODO
int TypeChecker::visitFinallyNode(FinallyNode *node) {
    return 0;
} //TODO
int TypeChecker::visitVarDeclNode(VarDeclNode *node) {
    return 0;
} //TODO
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
