/*
 * TypeChecker.h
 *
 *  Created on: 2015/01/07
 *      Author: skgchxngsxyz-osx
 */

#ifndef PARSER_TYPECHECKER_H_
#define PARSER_TYPECHECKER_H_

#include "../core/TypePool.h"
#include "../core/DSType.h"
#include "../core/CalleeHandle.h"
#include "../ast/Node.h"
#include "../ast/NodeVisitor.h"

class TypeChecker: public NodeVisitor {
private:
    TypePool *typePool;

    /**
     * contains current return type of current function
     */
    DSType* curReturnType;

    /**
     * contains state which represents for within finally block
     */
    std::vector<bool> finallyContextStack;

public:
    TypeChecker(TypePool *typePool);
    virtual ~TypeChecker();

    /**
     * type checker entry point
     */
    void checkTypeRootNode(const std::unique_ptr<RootNode> &rootNode);

private:
    // base type check entry point

    /**
     * check type.
     * if node type is void type, always success.
     */
    void checkTypeAcceptingVoidType(Node *targetNode);

    /**
     * check node type.
     * if node type is void type, throw exception
     */
    void checkType(Node *targetNode);

    /**
     * check node type
     * requiredType is not null
     *
     * if requiredType is not equivalent to node type, throw exception.
     */
    void checkType(DSType *requiredType, Node *targetNode);

    /**
     * check node type
     * requiredType may be null
     * unacceptableType may be null
     *
     * if requiredType is not equivalent to node type, throw exception.
     * if requiredType is null, do not try matching node type
     * and if unaccepatbelType is equivalent to node type, throw exception.
     */
    void checkType(DSType *requiredType, Node *targetNode, DSType *unacceptableType);

    void pushReturnType(DSType *returnType);

    /**
     * return null, if outside of function
     */
    DSType *popReturnType();

    /**
     * return null, if outside of function
     */
    DSType *getCurrentReturnType();

public:
    // visitor api

    int visitIntValueNode(IntValueNode *node); // override
    int visitFloatValueNode(FloatValueNode *node); // override
    int visitBooleanValueNode(BooleanValueNode *node); // override
    int visitStringValueNode(StringValueNode *node); // override
    int visitStringExprNode(StringExprNode *node); // override
    int visitArrayNode(ArrayNode *node); // override
    int visitMapNode(MapNode *node); // override
    int visitPairNode(PairNode *node); // override
    int visitVarNode(VarNode *node); // override
    int visitIndexNode(IndexNode *node); // override
    int visitAccessNode(AccessNode *node); // override
    int visitCastNode(CastNode *node); // override
    int visitInstanceOfNode(InstanceOfNode *node); // override
    int visitApplyNode(ApplyNode *node); // override
    int visitConstructorCallNode(ConstructorCallNode *node); // override
    int visitCondOpNode(CondOpNode *node); // override
    int visitProcessNode(ProcessNode *node); // override
    int visitProcArgNode(ProcArgNode *node); // override
    int visitSpecialCharNode(SpecialCharNode *node); // override
    int visitTaskNode(TaskNode *node); // override
    int visitInnerTaskNode(InnerTaskNode *node); // override
    int visitAssertNode(AssertNode *node); // override
    int visitBlockNode(BlockNode *node); // override
    int visitBreakNode(BreakNode *node); // override
    int visitContinueNode(ContinueNode *node); // override
    int visitExportEnvNode(ExportEnvNode *node); // override
    int visitImportEnvNode(ImportEnvNode *node); // override
    int visitForNode(ForNode *node); // override
    int visitForInNode(ForInNode *node); // override
    int visitWhileNode(WhileNode *node); // override
    int visitIfNode(IfNode *node); // override
    int visitReturnNode(ReturnNode *node); // override
    int visitThrowNode(ThrowNode *node); // override
    int visitCatchNode(CatchNode *node); // override
    int visitTryNode(TryNode *node); // override
    int visitFinallyNode(FinallyNode *node); // override
    int visitVarDeclNode(VarDeclNode *node); // override
    int visitAssignNode(AssignNode *node); // override
    int visitFunctionNode(FunctionNode *node); // override
    int visitEmptyNode(EmptyNode *node); // override
    int visitEmptyBlockNode(EmptyBlockNode *node); // override
};

#endif /* PARSER_TYPECHECKER_H_ */
