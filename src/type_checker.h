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

        unsigned int childLevel;
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

    /**
     *
     * @return
     * child process depth. (if 0, parent)
     */
    unsigned int childLevel() const {
        return this->stacks.back().childLevel;
    }

    void clear() {
        this->stacks.clear();
        this->stacks += {0, 0, 0, 0};
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

    void enterChild() {
        auto v = this->stacks.back();
        v.childLevel = this->stacks.size();
        this->stacks += v;
    }
};

/**
 * gather returnable break node (EscapeNode)
 */
class BreakGather {
private:
    struct Entry {
        FlexBuffer<JumpNode *> jumpNodes;
        Entry *next;

        explicit Entry(Entry *prev) : next(prev) {}
        ~Entry() {
            delete this->next;
        }
    } *entry;

public:
    BreakGather() : entry(nullptr) {}
    ~BreakGather() {
        this->clear();
    }

    void clear();

    void enter();

    void leave();

    void addJumpNode(JumpNode *node);

    /**
     * call after enter()
     * @return
     */
    FlexBuffer<JumpNode *> &getJumpNodes() {
        return this->entry->jumpNodes;
    }
};

class TypeChecker;

class TypeGenerator {
private:
    TypePool &pool;
    TypeChecker *checker;

public:
    explicit TypeGenerator(TypePool &pool, TypeChecker *checker = nullptr) : pool(pool), checker(checker) {}

    DSType &toType(TypeNode &node);

    DSType &resolveInterface(InterfaceNode *node);

private:
    DSType *toTypeImpl(TypeNode &node);
};


class TypeChecker {
protected:
    friend class TypeGenerator;

    /**
     * for type lookup
     */
    TypePool &typePool;

    SymbolTable &symbolTable;

    /**
     * contains current return type of current function
     */
    DSType *curReturnType{nullptr};

    int visitingDepth{0};

    FlowContext fctx;

    BreakGather breakGather;

    bool toplevelPrinting;

public:
    TypeChecker(TypePool &typePool, SymbolTable &symbolTable, bool toplevelPrinting) :
            typePool(typePool), symbolTable(symbolTable), toplevelPrinting(toplevelPrinting) { }

    ~TypeChecker() = default;

    /**
     * type checker entry point
     */
    void checkTypeRootNode(RootNode &rootNode) {
        this->visitRootNode(nullptr, rootNode);
    }

    void reset() {
        this->symbolTable.commit();
        this->typePool.commit();
        this->fctx.clear();
        this->breakGather.clear();
    }

protected:
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

    void checkTypeWithCurrentScope(BlockNode *blockNode) {
        this->checkTypeWithCurrentScope(&this->typePool.getVoidType(), blockNode);
    }

    void checkTypeWithCurrentScope(DSType *requiredType, BlockNode *blockNode);

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
    bool checkCoercion(const DSType &requiredType, DSType &targetType);

    void resolveCoercion(DSType &requiredType, Node * &targetNode) {
        targetNode = this->newTypedCastNode(targetNode, requiredType);
        this->resolveCastOp(*static_cast<TypeOpNode *>(targetNode));
    }

    DSType &resolveCoercionOfJumpValue();

    FieldHandle *addEntryAndThrowIfDefined(Node &node, const std::string &symbolName, DSType &type, FieldAttributes attribute);

    bool isTopLevel() const {
        return this->visitingDepth == 1;
    }

    void enterLoop() {
        this->fctx.enterLoop();
        this->breakGather.enter();
    }

    void exitLoop() {
        this->fctx.leave();
        this->breakGather.leave();
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
     */
    void resolveCastOp(TypeOpNode &node);

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

    void checkTypeAsBreakContinue(JumpNode &node);
    void checkTypeAsReturn(JumpNode &node);

    void dispatch(DSType *requiredType, Node &node);

    // visitor api
    void visitTypeNode(DSType *requiredType, TypeNode &node);
    void visitNumberNode(DSType *requiredType, NumberNode &node);
    void visitStringNode(DSType *requiredType, StringNode &node);
    void visitStringExprNode(DSType *requiredType, StringExprNode &node);
    void visitRegexNode(DSType *requiredType, RegexNode &node);
    void visitArrayNode(DSType *requiredType, ArrayNode &node);
    void visitMapNode(DSType *requiredType, MapNode &node);
    void visitTupleNode(DSType *requiredType, TupleNode &node);
    void visitVarNode(DSType *requiredType, VarNode &node);
    void visitAccessNode(DSType *requiredType, AccessNode &node);
    void visitTypeOpNode(DSType *requiredType, TypeOpNode &node);
    void visitUnaryOpNode(DSType *requiredType, UnaryOpNode &node);
    void visitBinaryOpNode(DSType *requiredType, BinaryOpNode &node);
    void visitApplyNode(DSType *requiredType, ApplyNode &node);
    void visitMethodCallNode(DSType *requiredType, MethodCallNode &node);
    void visitNewNode(DSType *requiredType, NewNode &node);
    void visitCmdNode(DSType *requiredType, CmdNode &node);
    void visitCmdArgNode(DSType *requiredType, CmdArgNode &node);
    void visitRedirNode(DSType *requiredType, RedirNode &node);
    void visitPipelineNode(DSType *requiredType, PipelineNode &node);
    void visitWithNode(DSType *requiredType, WithNode &node);
    void visitForkNode(DSType *requiredType, ForkNode &node);
    void visitAssertNode(DSType *requiredType, AssertNode &node);
    void visitBlockNode(DSType *requiredType, BlockNode &node);
    void visitTypeAliasNode(DSType *requiredType, TypeAliasNode &node);
    void visitLoopNode(DSType *requiredType, LoopNode &node);
    void visitIfNode(DSType *requiredType, IfNode &node);
    void visitJumpNode(DSType *requredType, JumpNode &node);
    void visitCatchNode(DSType *requiredType, CatchNode &node);
    void visitTryNode(DSType *requiredType, TryNode &node);
    void visitVarDeclNode(DSType *requiredType, VarDeclNode &node);
    void visitAssignNode(DSType *requiredType, AssignNode &node);
    void visitElementSelfAssignNode(DSType *requiredType, ElementSelfAssignNode &node);
    void visitFunctionNode(DSType *requiredType, FunctionNode &node);
    void visitInterfaceNode(DSType *requiredType, InterfaceNode &node);
    void visitUserDefinedCmdNode(DSType *requiredType, UserDefinedCmdNode &node);
    void visitEmptyNode(DSType *requiredType, EmptyNode &node);
    void visitRootNode(DSType *requiredType, RootNode &node);
};

} // namespace ydsh

#endif //YDSH_TYPE_CHECKER_H
