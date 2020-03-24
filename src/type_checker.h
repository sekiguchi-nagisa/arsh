/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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
#include "symbol_table.h"
#include "tcerror.h"
#include "misc/buffer.hpp"
#include "misc/hash.hpp"

namespace ydsh {

// for apply node type checking
using HandleOrFuncType = Union<FunctionType *, const MethodHandle *>;

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
    FlowContext() : stacks({{0, 0, 0, 0}}) { }
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

class TypeChecker : protected NodeVisitor {
protected:
    SymbolTable &symbolTable;

    /**
     * contains current return type of current function
     */
    const DSType *curReturnType{nullptr};

    int visitingDepth{0};

    FlowContext fctx;

    BreakGather breakGather;

    bool toplevelPrinting;

public:
    TypeChecker(SymbolTable &symbolTable, bool toplevelPrinting) :
            symbolTable(symbolTable), toplevelPrinting(toplevelPrinting) { }

    ~TypeChecker() override = default;

    std::unique_ptr<Node> operator()(const DSType *prevType, std::unique_ptr<Node> &&node);

    SymbolTable &getSymbolTable() {
        return this->symbolTable;
    }

protected:
    // base type check entry point
    TypeOrError toTypeImpl(TypeNode &node);

    /**
     * check node type.
     * if node type is void type, throw exception.
     * return resolved type.
     */
    DSType &checkTypeAsExpr(Node &targetNode) {
        return this->checkType(nullptr, targetNode, &this->symbolTable.get(TYPE::Void));
    }

    /**
     * check node type. not allow Void and Nothing type
     * @param targetNode
     * @return
     */
    DSType &checkTypeAsSomeExpr(Node &targetNode);

    /**
     * check node type
     *
     * if requiredType is not equivalent to node type, throw exception.
     * return resolved type.
     */
    DSType &checkType(const DSType &requiredType, Node &targetNode) {
        return this->checkType(&requiredType, targetNode, nullptr);
    }

    /**
     * only call visitor api (not perform additional type checking)
     * @param targetNode
     * @return
     */
    DSType &checkTypeExactly(Node &targetNode) {
        return this->checkType(nullptr, targetNode, nullptr);
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
    DSType &checkType(const DSType *requiredType, Node &targetNode, const DSType *unacceptableType) {
        CoercionKind kind = CoercionKind::NOP;
        return this->checkType(requiredType, targetNode, unacceptableType, kind);
    }

    /**
     * root method of checkType
     */
    DSType &checkType(const DSType *requiredType, Node &targetNode,
                      const DSType *unacceptableType, CoercionKind &kind);

    void checkTypeWithCurrentScope(BlockNode &blockNode) {
        this->checkTypeWithCurrentScope(&this->symbolTable.get(TYPE::Void), blockNode);
    }

    void checkTypeWithCurrentScope(const DSType *requiredType, BlockNode &blockNode);

    /**
     * after type checking.
     * requiredType is not null.
     * if requiredType is FloatType and targetNode->getType() is IntType,
     * wrap targetNode with CastNode.
     * if requiredType is VoidType, wrap targetNode with CastNode
     */
    void checkTypeWithCoercion(const DSType &requiredType, Node * &targetNode);

    /**
     * for int type conversion.
     * return true if allow target type to required type implicit cast.
     */
    bool checkCoercion(const DSType &requiredType, const DSType &targetType);

    void resolveCoercion(const DSType &requiredType, Node * &targetNode) {
        targetNode = newTypedCastNode(targetNode, requiredType);
        this->resolveCastOp(*static_cast<TypeOpNode *>(targetNode));
    }

    DSType &resolveCoercionOfJumpValue();

    const FieldHandle *addEntry(const Node &node, const std::string &symbolName,
                          const DSType &type, FieldAttribute attribute);

    bool isTopLevel() const {
        return this->visitingDepth == 1;
    }

    template <typename Func>
    void inScope(Func func) {
        this->symbolTable.enterScope();

        func();

        this->symbolTable.exitScope();
    }

    template <typename Func>
    void inLoop(Func func) {
        this->fctx.enterLoop();
        this->breakGather.enter();

        func();

        this->fctx.leave();
        this->breakGather.leave();
    }

    template <typename Func>
    unsigned int inFunc(const DSType &returnType, Func func) {
        this->curReturnType = &returnType;

        this->symbolTable.enterFunc();
        this->inScope([&]{
            func();
        });
        unsigned int num = this->symbolTable.getMaxVarIndex();
        this->symbolTable.exitFunc();
        this->curReturnType = nullptr;
        return num;
    }

    /**
     * return null, if outside of function
     */
    const DSType *getCurrentReturnType() const {
        return this->curReturnType;
    }

    // for apply node type checking
    /**
     * check type ApplyNode and resolve callee(handle or function type).
     */
    HandleOrFuncType resolveCallee(ApplyNode &node);

    /**
     * check type ApplyNode and resolve callee(handle or function type).
     */
    HandleOrFuncType resolveCallee(VarNode &recvNode);

    // helper for argument type checking
    void checkTypeArgsNode(Node &node, const MethodHandle *handle, std::vector<Node *> &argNodes);

    void checkTypeAsMethodCall(ApplyNode &node, const MethodHandle *handle);

    bool checkAccessNode(AccessNode &node);

    // helper api for type cast

    /**
     *
     * @param node
     * must be typed
     */
    void resolveCastOp(TypeOpNode &node);

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

    // for case-expression
    struct PatternMap {
        virtual ~PatternMap() = default;

        virtual bool collect(const Node &constNode) = 0;
    };

    class IntPatternMap : public PatternMap {
    private:
        std::unordered_set<int64_t> set;

    public:
        bool collect(const Node &constNode) override;
    };

    class StrPatternMap : public PatternMap {
    private:
        CStringHashSet set;

    public:
        bool collect(const Node &constNode) override;
    };

    class PatternCollector {
    private:
        CaseNode::Kind kind{CaseNode::MAP};
        std::unique_ptr<PatternMap> map;
        bool elsePattern{false};
        DSType *type{nullptr};

    public:
        bool hasElsePattern() const {
            return this->elsePattern;
        }

        void setElsePattern(bool set) {
            this->elsePattern = set;
        }

        void setKind(CaseNode::Kind k) {
            this->kind = k;
        }

        auto getKind() const {
            return this->kind;
        }

        void setType(DSType *t) {
            this->type = t;
        }

        DSType *getType() const {
            return this->type;
        }

        /**
         * try to collect constant node.
         * if found duplicated constant, return false
         * @param constNode
         * @return
         */
        bool collect(const Node &constNode);
    };

    void checkPatternType(ArmNode &node, PatternCollector &collector);

    /**
     *
     * @param types
     * @return
     * if not found, return void type.
     */
    DSType &resolveCommonSuperType(const std::vector<DSType *> &types);

    /**
     *
     * @param node
     * must be typed node
     * @return
     */
    bool applyConstFolding(Node *&node) const;

    // visitor api
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
    void visitNewNode(NewNode &node) override;
    void visitEmbedNode(EmbedNode &node) override;
    void visitCmdNode(CmdNode &node) override;
    void visitCmdArgNode(CmdArgNode &node) override;
    void visitRedirNode(RedirNode &node) override;
    void visitPipelineNode(PipelineNode &node) override;
    void visitWithNode(WithNode &node) override;
    void visitForkNode(ForkNode &node) override;
    void visitAssertNode(AssertNode &node) override;
    void visitBlockNode(BlockNode &node) override;
    void visitTypeAliasNode(TypeAliasNode &node) override;
    void visitLoopNode(LoopNode &node) override;
    void visitIfNode(IfNode &node) override;
    void visitCaseNode(CaseNode &node) override;
    void visitArmNode(ArmNode &node) override;
    void visitJumpNode(JumpNode &node) override;
    void visitCatchNode(CatchNode &node) override;
    void visitTryNode(TryNode &node) override;
    void visitVarDeclNode(VarDeclNode &node) override;
    void visitAssignNode(AssignNode &node) override;
    void visitElementSelfAssignNode(ElementSelfAssignNode &node) override;
    void visitFunctionNode(FunctionNode &node) override;
    void visitInterfaceNode(InterfaceNode &node) override;
    void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
    void visitSourceNode(SourceNode &node) override;
    void visitEmptyNode(EmptyNode &node) override;
};

} // namespace ydsh

#endif //YDSH_TYPE_CHECKER_H
