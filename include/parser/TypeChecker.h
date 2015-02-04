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

#ifndef PARSER_TYPECHECKER_H_
#define PARSER_TYPECHECKER_H_

#include <ast/Node.h>
#include <ast/NodeVisitor.h>
#include <core/TypePool.h>
#include <core/DSType.h>
#include <core/FieldHandle.h>
#include <core/SymbolTable.h>

class TypeChecker: public NodeVisitor {
private:
    /**
     * for type lookup
     */
    TypePool *typePool;

    SymbolTable symbolTable;

    /**
     * contains current return type of current function
     */
    DSType* curReturnType;

    /**
     * contains state which represents for within loop block
     */
    std::vector<bool> loopContextStack;

    /**
     * contains state which represents for within finally block
     */
    std::vector<bool> finallyContextStack;

public:
    TypeChecker(TypePool *typePool);
    ~TypeChecker();

    /**
     * type checker entry point
     */
    void checkTypeRootNode(RootNode *rootNode);

private:
    // base type check entry point

    /**
     * check type.
     * if node type is void type, always success.
     * return resolved type.
     */
    DSType *checkTypeAsStatement(Node *targetNode);

    /**
     * check node type.
     * if node type is void type, throw exception.
     * return resolved type.
     */
    DSType *checkType(Node *targetNode);

    /**
     * check node type
     * requiredType is not null
     *
     * if requiredType is not equivalent to node type, throw exception.
     * return resolved type.
     */
    DSType *checkType(DSType *requiredType, Node *targetNode);

    /**
     * check node type
     * requiredType may be null
     * unacceptableType may be null
     *
     * if requiredType is not equivalent to node type, throw exception.
     * if requiredType is null, do not try matching node type
     * and if unaccepatbelType is equivalent to node type, throw exception.
     * return resolved type.
     */
    DSType *checkType(DSType *requiredType, Node *targetNode, DSType *unacceptableType);

    /**
     * create new symbol table and check type each node within block.
     * after type checking, remove current symbol table
     */
    void checkTypeWithNewBlockScope(BlockNode *blockNode);

    /**
     * check type each node within block in current block scope
     */
    void checkTypeWithCurrentBlockScope(BlockNode *blockNode);

    void addEntryAndThrowIfDefined(Node *node, const std::string &symbolName, DSType *type, bool readOnly);

    void enterLoop();
    void exitLoop();

    /**
     * check node inside loop.
     * if node is out of loop, throw exception
     * node is BreakNode or ContinueNode
     */
    void checkAndThrowIfOutOfLoop(Node *node);

    bool findBlockEnd(BlockNode *blockNode);

    /**
     * check block end (return, throw) existence in function block
     * blockNode is function block.
     * returnType is function return type.
     */
    void checkBlockEndExistence(BlockNode *blockNode, DSType *returnType);

    void pushReturnType(DSType *returnType);

    /**
     * return null, if outside of function
     */
    DSType *popReturnType();

    /**
     * return null, if outside of function
     */
    DSType *getCurrentReturnType();

    void checkAndThrowIfInsideFinally(BlockEndNode *node);

    // for apply node type checking

    /**
     * convert TypeToken to DSType.
     * after converting, delete typeToken.
     */
    DSType *toType(TypeToken *typeToken);

    // for apply node type checking
    class HandleOrFuncType {
    private:
        bool hasHandle;
        union {
            FunctionHandle *handle;
            FunctionType *funcType;
        };
    public:
        HandleOrFuncType(FunctionHandle *handle) :
            hasHandle(true), handle(handle) {
        }

        HandleOrFuncType(FunctionType *funcType) :
            hasHandle(false), funcType(funcType) {
        }

        bool treatAsHandle() {
            return this->hasHandle;
        }

        FunctionHandle *getHandle() {
            return this->hasHandle ? this->handle : 0;
        }

        FunctionType *getFuncType() {
            return this->hasHandle ? 0 : this->funcType;
        }
    };


    /**
     * check type ApplyNode and resolve callee(handle or function type).
     * recvNode must be equivalent to applyNode->getRecvNode()
     */
    HandleOrFuncType resolveCallee(Node *recvNode, ApplyNode *applyNode);

    /**
     * check type ApplyNode and resolve callee(handle or function type).
     * recvNode must be equivalent to applyNode->getRecvNode()
     */
    HandleOrFuncType resolveCallee(AccessNode *recvNode, ApplyNode *applyNode);

    /**
     * check type ApplyNode and resolve callee(handle or function type).
     * recvNode must be equivalent to applyNode->getRecvNode()
     */
    HandleOrFuncType resolveCallee(VarNode *recvNode, ApplyNode *applyNode);

    // helper for argument type checking
    void checkTypeArgsNode(FunctionHandle *handle, ArgsNode *argsNode, bool isFuncCall);
    void checkTypeArgsNode(FunctionType *funcType, ArgsNode *argsNode, bool isFuncCall);
    void checkTypeArgsNode(const std::vector<DSType*> &paramTypes, ArgsNode *argsNode, bool isFuncCall);

public:
    /**
     * reset symbol table when error happened
     */
    void recover();

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
    int visitAccessNode(AccessNode *node); // override
    int visitCastNode(CastNode *node); // override
    int visitInstanceOfNode(InstanceOfNode *node); // override
    int visitOperatorCallNode(OperatorCallNode *node); // override
    int visitArgsNode(ArgsNode *node); // override
    int visitApplyNode(ApplyNode *node); // override
    int visitNewNode(NewNode *node); // override
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
    int visitWhileNode(WhileNode *node); // override
    int visitIfNode(IfNode *node); // override
    int visitReturnNode(ReturnNode *node); // override
    int visitThrowNode(ThrowNode *node); // override
    int visitCatchNode(CatchNode *node); // override
    int visitTryNode(TryNode *node); // override
    int visitFinallyNode(FinallyNode *node); // override
    int visitVarDeclNode(VarDeclNode *node); // override
    int visitAssignNode(AssignNode *node); // override
    int visitFieldSelfAssignNode(FieldSelfAssignNode *node); // override
    int visitFunctionNode(FunctionNode *node); // override
    int visitEmptyNode(EmptyNode *node); // override
};

#endif /* PARSER_TYPECHECKER_H_ */
