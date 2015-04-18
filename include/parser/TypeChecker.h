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
#include <core/TypePool.h>
#include <core/DSType.h>
#include <core/FieldHandle.h>
#include <core/SymbolTable.h>

namespace ydsh {
namespace parser {

using namespace ydsh::core;

// for apply node type checking
class HandleOrFuncType {
private:
    bool hasHandle;
    union {
        FunctionHandle *handle;
        FunctionType *funcType;
    };

public:
    HandleOrFuncType(FunctionHandle *handle);

    HandleOrFuncType(FunctionType *funcType);

    bool treatAsHandle();

    FunctionHandle *getHandle();

    FunctionType *getFuncType();
};

class TypeChecker : public NodeVisitor {
private:
    /**
     * for type lookup
     */
    TypePool *typePool;

    SymbolTable symbolTable;

    /**
     * contains current return type of current function
     */
    DSType *curReturnType;

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
    void checkTypeRootNode(RootNode &rootNode);

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
    DSType *checkType(DSType *requiredType, Node *targetNode,
                      DSType *unacceptableType, bool allowCoercion = false);

    /**
     * after type checking,
     * if requireType is FloatType and targetNode->getType() is IntType,
     * wrap targetNode with CastNode.
     */
    Node *checkTypeAndResolveCoercion(DSType *requiredType, Node *targetNode);

    /**
     * return true if allow target type to required type implicit cast.
     * currently only support int to float cast.
     */
    bool supportCoercion(DSType *requiredType, DSType *targetType);

    /**
     * return typed CastNode(type is FloatType)
     * targetNode->getType() must be IntType.
     */
    CastNode *intToFloat(Node *targetNode);

    /**
     * create new symbol table and check type each node within block.
     * after type checking, remove current symbol table
     */
    void checkTypeWithNewBlockScope(BlockNode *blockNode);

    /**
     * check type each node within block in current block scope
     */
    void checkTypeWithCurrentBlockScope(BlockNode *blockNode);

    FieldHandle *addEntryAndThrowIfDefined(Node *node, const std::string &symbolName, DSType *type, bool readOnly);

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
     * convert TypeToken to DSType..
     */
    DSType *toType(TypeToken *typeToken);

    /**
     * check type ApplyNode and resolve callee(handle or function type).
     */
    HandleOrFuncType resolveCallee(Node *recvNode);

    /**
     * check type ApplyNode and resolve callee(handle or function type).
     */
    HandleOrFuncType resolveCallee(VarNode *recvNode);

    // helper for argument type checking
    void checkTypeArgsNode(FunctionHandle *handle, ArgsNode *argsNode, bool isFuncCall);
    void checkTypeArgsNode(FunctionType *funcType, ArgsNode *argsNode, bool isFuncCall);
    void checkTypeArgsNode(const std::vector<DSType *> &paramTypes, ArgsNode *argsNode, bool isFuncCall);
    void checkTypeArgsNode(MethodHandle *handle, ArgsNode *argsNode);

public:
    /**
     * reset symbol table when error happened
     */
    void recover();

    // visitor api
    void visitDefault(Node *node); // override

    void visitIntValueNode(IntValueNode *node); // override
    void visitLongValueNode(LongValueNode *node); // override
    void visitFloatValueNode(FloatValueNode *node); // override
    void visitStringValueNode(StringValueNode *node); // override
    void visitObjectPathNode(ObjectPathNode *node); // override
    void visitStringExprNode(StringExprNode *node); // override
    void visitArrayNode(ArrayNode *node); // override
    void visitMapNode(MapNode *node); // override
    void visitTupleNode(TupleNode *node); // override
    void visitVarNode(VarNode *node); // override
    void visitAccessNode(AccessNode *node); // override
    void visitCastNode(CastNode *node); // override
    void visitInstanceOfNode(InstanceOfNode *node); // override
    void visitBinaryOpNode(BinaryOpNode *node); // override
    void visitArgsNode(ArgsNode *node); // override
    void visitApplyNode(ApplyNode *node); // override
    void visitMethodCallNode(MethodCallNode *node); // override
    void visitNewNode(NewNode *node); // override
    void visitGroupNode(GroupNode *node); // override
    void visitCondOpNode(CondOpNode *node); // override
    void visitCmdNode(CmdNode *node); // override
    void visitCmdArgNode(CmdArgNode *node); // override
    void visitPipedCmdNode(PipedCmdNode *node); // override
    void visitCmdContextNode(CmdContextNode *node); // override
    void visitAssertNode(AssertNode *node); // override
    void visitBlockNode(BlockNode *node); // override
    void visitBreakNode(BreakNode *node); // override
    void visitContinueNode(ContinueNode *node); // override
    void visitExportEnvNode(ExportEnvNode *node); // override
    void visitImportEnvNode(ImportEnvNode *node); // override
    void visitTypeAliasNode(TypeAliasNode *node); // override
    void visitForNode(ForNode *node); // override
    void visitWhileNode(WhileNode *node); // override
    void visitDoWhileNode(DoWhileNode *node); // override
    void visitIfNode(IfNode *node); // override
    void visitReturnNode(ReturnNode *node); // override
    void visitThrowNode(ThrowNode *node); // override
    void visitCatchNode(CatchNode *node); // override
    void visitTryNode(TryNode *node); // override
    void visitVarDeclNode(VarDeclNode *node); // override
    void visitAssignNode(AssignNode *node); // override
    void visitElementSelfAssignNode(ElementSelfAssignNode *node); // override
    void visitFunctionNode(FunctionNode *node); // override
    void visitBindVarNode(BindVarNode *node); // override
    void visitEmptyNode(EmptyNode *node); // override
    void visitDummyNode(DummyNode *node); // override
    void visitRootNode(RootNode *node); // override
};

} // namespace parser
} // namespace ydsh

#endif /* PARSER_TYPECHECKER_H_ */
