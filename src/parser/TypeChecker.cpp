/*
 * TypeChecker.cpp
 *
 *  Created on: 2015/01/07
 *      Author: skgchxngsxyz-osx
 */

#include "TypeChecker.h"

TypeChecker::TypeChecker(TypePool *typePool) :
        typePool(typePool), checkedNode(0), curReturnType(0), finallyContextStack() {
}

TypeChecker::~TypeChecker() {
    this->finallyContextStack.clear();
}

RootNode *TypeChecker::checkTypeRootNode(RootNode *rootNode) {	//FIXME
    int size = rootNode->getNodes().size();
    for (int i = 0; i < size; i++) {
        this->checkTypeAcceptingVoidType(rootNode->getNodes()[i]);
    }
    return rootNode;
}

int TypeChecker::pushCheckedNode(Node *checkedNode) {	//TODO: param check
    this->checkedNode = checkedNode;
    return 0;
}

Node *TypeChecker::popCheckedNode() {
    Node *popedNode = this->checkedNode;
    this->checkedNode = 0;
    return popedNode;
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

Node *TypeChecker::checkTypeAcceptingVoidType(Node *targetNode) {
    return this->checkType(0, targetNode, 0);
}

Node *TypeChecker::checkType(Node *targetNode) {
    return this->checkType(0, targetNode, this->typePool->getVoidType());
}

Node *TypeChecker::checkType(DSType *requiredType, Node *targetNode) {
    return this->checkType(requiredType, targetNode, 0);
}

//TODO:
Node *TypeChecker::checkType(DSType *requiredType, Node *targetNode,
        DSType *unacceptableType) {
    /**
     * if target node is statement, always check type.
     */
    ExprNode *exprNode = dynamic_cast<ExprNode*>(targetNode);
    if (exprNode == 0) {
        targetNode->accept(this);
        return this->popCheckedNode();
    }

    /**
     * if target node is expr node and unresolved type,
     * try type check.
     */
    if (dynamic_cast<UnresolvedType*>(exprNode->getType()) != 0) {
        //exprNode = (ExprNode) exprNode.accept(this);
        exprNode->accept(this);
        exprNode = dynamic_cast<ExprNode*>(this->popCheckedNode());
    }

//	/**
//	 * after type checking, if type is still unresolved type,
//	 * throw exception.
//	 */
//	DSType *type = exprNode->getType();
//	if(type instanceof UnresolvedType) {
//		throw new TypeCheckException(exprNode, TypeErrorKind_ZeroArg.Unresolved);
//	}

    DSType *type = exprNode->getType();
    /**
     * do not try type matching.
     */
    if (requiredType == 0) {
        if (unacceptableType != 0 && unacceptableType->isAssignableFrom(type)) {
            //throw new TypeCheckException(exprNode, TypeErrorKind_OneArg.Unacceptable, type);	//FIXME: error report
        }
        return exprNode;
    }

    /**
     * try type matching.
     */
    if (requiredType->isAssignableFrom(type)) {
        return exprNode;
    }

    //FIXME: error report
    return 0;
}

// visitor api

int TypeChecker::visitIntValueNode(IntValueNode *node) {//TODO: int8, int16 ..etc
    node->setType(this->typePool->getIntType());
    return this->pushCheckedNode(node);
}

int TypeChecker::visitFloatValueNode(FloatValueNode *node) {
    node->setType(this->typePool->getFloatType());
    return this->pushCheckedNode(node);
}

int TypeChecker::visitBooleanValueNode(BooleanValueNode *node) {
    node->setType(this->typePool->getBooleanType());
    return this->pushCheckedNode(node);
}

int TypeChecker::visitStringValueNode(StringValueNode *node) {
    node->setType(this->typePool->getStringType());
    return this->pushCheckedNode(node);
}

int TypeChecker::visitStringExprNode(StringExprNode *node) {
    int size = node->getExprNodes().size();
    for (int i = 0; i < size; i++) {
        this->checkType(this->typePool->getStringType(),
                node->getExprNodes()[i]);
    }
    node->setType(this->typePool->getStringType());
    return this->pushCheckedNode(node);
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
    return this->pushCheckedNode(node);
}

int TypeChecker::visitEmptyBlockNode(EmptyBlockNode *node) {
    return this->pushCheckedNode(node);	// do nothing
}
