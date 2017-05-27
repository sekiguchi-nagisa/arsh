/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef YDSH_TYPE_CHECKER_H
#define YDSH_TYPE_CHECKER_H

#include "node.h"
#include "type.h"
#include "handle.h"
#include "symbol_table.h"
#include "diagnosis.h"
#include "misc/buffer.hpp"

namespace ydsh {

// for apply node type checking
class HandleOrFuncType {
private:
    bool hasHandle;
    union {
        FunctionHandle *handle;
        FunctionType *funcType;
    };

public:
    explicit HandleOrFuncType(FunctionHandle *handle) : hasHandle(true), handle(handle) { }

    explicit HandleOrFuncType(FunctionType *funcType) : hasHandle(false), funcType(funcType) { }

    bool treatAsHandle() const {
        return this->hasHandle;
    }

    FunctionHandle *getHandle() const {
        return this->hasHandle ? this->handle : nullptr;
    }

    FunctionType *getFuncType() const {
        return this->hasHandle ? nullptr : this->funcType;
    }
};

enum class CoercionKind : unsigned char {
    PERFORM_COERCION,
    INVALID_COERCION,   // illegal coercion.
    NOP,                // not allow coercion
};

class FlowContext {
private:
    struct Context {
        unsigned int tryLevel;

        unsigned int finallyLevel;

        unsigned int loopLevel;
    };

    FlexBuffer<Context> stacks;

public:
    FlowContext() : stacks({{0, 0, 0}}) { }
    ~FlowContext() = default;

    unsigned int tryCatchLevel() const {
        return this->stacks.back().tryLevel;
    }

    /**
     *
     * @return
     * finally block depth. (if 0, outside finally block)
     */
    unsigned int finallyLevel() const {
        return this->stacks.back().finallyLevel;
    }

    /**
     *
     * @return
     * loop block depth. (if 0, outside loop block)
     */
    unsigned int loopLevel() const {
        return this->stacks.back().loopLevel;
    }

    void clear() {
        this->stacks.clear();
        this->stacks += {0, 0, 0};
    }

    void leave() {
        this->stacks.pop_back();
    }

    void enterTry() {
        auto v = this->stacks.back();
        v.tryLevel = this->stacks.size();
        this->stacks += v;
    }

    void enterFinally() {
        auto v = this->stacks.back();
        v.finallyLevel = this->stacks.size();
        this->stacks += v;
    }

    void enterLoop() {
        auto v = this->stacks.back();
        v.loopLevel = this->stacks.size();
        this->stacks += v;
    }
};

class TypeChecker;

class TypeGenerator {
private:
    TypePool &pool;
    TypeChecker *checker;

public:
    TypeGenerator(TypePool &pool, TypeChecker *checker = nullptr) : pool(pool), checker(checker) {}

    DSType &toType(TypeNode &node);

    DSType &resolveInterface(InterfaceNode *node);

private:
    DSType *toTypeImpl(TypeNode &node);
};


class TypeChecker : protected NodeVisitor {
private:
    friend class TypeGenerator;

    /**
     * for type lookup
     */
    TypePool &typePool;

    SymbolTable &symbolTable;

    /**
     * contains current return type of current function
     */
    DSType *curReturnType;

    int visitingDepth;

    FlowContext fctx;

    bool toplevelPrinting;

public:
    TypeChecker(TypePool &typePool, SymbolTable &symbolTable, bool toplevelPrinting) :
            typePool(typePool), symbolTable(symbolTable), curReturnType(0),
            visitingDepth(0), fctx(), toplevelPrinting(toplevelPrinting) { }

    ~TypeChecker() = default;

    /**
     * type checker entry point
     */
    void checkTypeRootNode(RootNode &rootNode) {
        rootNode.accept(*this);
    }

private:
    // base type check entry point
    DSType &toType(TypeNode *node) {
        return this->checkType(nullptr, node, nullptr);
    }

    /**
     * check node type.
     * if node type is void type, throw exception.
     * return resolved type.
     */
    DSType &checkType(Node *targetNode) {
        return this->checkType(nullptr, targetNode, &this->typePool.getVoidType());
    }

    /**
     * check node type
     *
     * if requiredType is not equivalent to node type, throw exception.
     * return resolved type.
     */
    DSType &checkType(DSType &requiredType, Node *targetNode) {
        return this->checkType(&requiredType, targetNode, nullptr);
    }

    /**
     * check node type
     * requiredType may be null
     * unacceptableType may be null
     *
     * if requiredType is not equivalent to node type, throw exception.
     * if requiredType is null, do not try matching node type
     * and if unacceptableType is equivalent to node type, throw exception.
     * return resolved type.
     */
    DSType &checkType(DSType *requiredType, Node *targetNode, DSType *unacceptableType) {
        CoercionKind kind = CoercionKind::NOP;
        return this->checkType(requiredType, targetNode, unacceptableType, kind);
    }

    /**
     * root method of checkType
     */
    DSType &checkType(DSType *requiredType, Node *targetNode,
                      DSType *unacceptableType, CoercionKind &kind);

    void checkTypeWithCurrentScope(BlockNode *blockNode);

    /**
     * after type checking.
     * requiredType is not null.
     * if requiredType is FloatType and targetNode->getType() is IntType,
     * wrap targetNode with CastNode.
     * if requiredType is VoidType, wrap targetNode with CastNode
     */
    void checkTypeWithCoercion(DSType &requiredType, Node * &targetNode);

    /**
     * for int type conversion.
     * return true if allow target type to required type implicit cast.
     */
    bool checkCoercion(const DSType &requiredType, const DSType &targetType);

    void resolveCoercion(DSType &requiredType, Node * &targetNode) {
        targetNode = this->newTypedCastNode(targetNode, requiredType);
    }

    FieldHandle *addEntryAndThrowIfDefined(Node &node, const std::string &symbolName, DSType &type, FieldAttributes attribute);

    bool isTopLevel() const {
        return this->visitingDepth == 1;
    }

    void enterLoop() {
        this->fctx.enterLoop();
    }

    void exitLoop() {
        this->fctx.leave();
    }

    void pushReturnType(DSType &returnType) {
        this->curReturnType = &returnType;
    }

    /**
     * return null, if outside of function
     */
    DSType *popReturnType() {
        DSType *returnType = this->curReturnType;
        this->curReturnType = nullptr;
        return returnType;
    }

    /**
     * return null, if outside of function
     */
    DSType *getCurrentReturnType() const {
        return this->curReturnType;
    }

    // for apply node type checking
    /**
     * check type ApplyNode and resolve callee(handle or function type).
     */
    HandleOrFuncType resolveCallee(Node &recvNode);

    /**
     * check type ApplyNode and resolve callee(handle or function type).
     */
    HandleOrFuncType resolveCallee(VarNode &recvNode);

    // helper for argument type checking
    void checkTypeArgsNode(Node &node, MethodHandle *handle, std::vector<Node *> &argNodes);

    // helper api for type cast

    /**
     *
     * @param node
     * must be typed
     * @param allowVoidCast
     */
    void resolveCastOp(TypeOpNode &node, bool allowVoidCast);

    /**
     * for implicit cast.
     * @param targetNode
     * must be typed.
     * @param type
     * @return
     */
    TypeOpNode *newTypedCastNode(Node *targetNode, DSType &type);

    /**
     *
     * @param node
     * must be typed
     * @return
     *
     */
    Node *newPrintOpNode(Node *node);

    void convertToStringExpr(BinaryOpNode &node);

    // visitor api
    void visit(Node &node) override;
    void visitTypeNode(TypeNode &node) override;
    void visitNumberNode(NumberNode &node) override;
    void visitStringNode(StringNode &node) override;
    void visitStringExprNode(StringExprNode &node) override;
    void visitRegexNode(RegexNode &node) override;
    void visitArrayNode(ArrayNode &node) override;
    void visitMapNode(MapNode &node) override;
    void visitTupleNode(TupleNode &node) override;
    void visitVarNode(VarNode &node) override;
    void visitAccessNode(AccessNode &node) override;
    void visitTypeOpNode(TypeOpNode &node) override;
    void visitUnaryOpNode(UnaryOpNode &node) override;
    void visitBinaryOpNode(BinaryOpNode &node) override;
    void visitApplyNode(ApplyNode &node) override;
    void visitMethodCallNode(MethodCallNode &node) override;
    void visitNewNode(NewNode &node) override;
    void visitTernaryNode(TernaryNode &node) override;
    void visitCmdNode(CmdNode &node) override;
    void visitCmdArgNode(CmdArgNode &node) override;
    void visitRedirNode(RedirNode &node) override;
    void visitPipelineNode(PipelineNode &node) override;
    void visitSubstitutionNode(SubstitutionNode &node) override;
    void visitAssertNode(AssertNode &node) override;
    void visitBlockNode(BlockNode &node) override;
    void visitJumpNode(JumpNode &node) override;
    void visitTypeAliasNode(TypeAliasNode &node) override;
    void visitLoopNode(LoopNode &node) override;
    void visitIfNode(IfNode &node) override;
    void visitReturnNode(ReturnNode &node) override;
    void visitThrowNode(ThrowNode &node) override;
    void visitCatchNode(CatchNode &node) override;
    void visitTryNode(TryNode &node) override;
    void visitVarDeclNode(VarDeclNode &node) override;
    void visitAssignNode(AssignNode &node) override;
    void visitElementSelfAssignNode(ElementSelfAssignNode &node) override;
    void visitFunctionNode(FunctionNode &node) override;
    void visitInterfaceNode(InterfaceNode &node) override;
    void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
    void visitEmptyNode(EmptyNode &node) override;
    void visitRootNode(RootNode &node) override;
};

} // namespace ydsh

#endif //YDSH_TYPE_CHECKER_H
