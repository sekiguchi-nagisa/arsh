/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_PARSER_TYPE_CHECKER_H
#define YDSH_PARSER_TYPE_CHECKER_H

#include "../ast/node.h"
#include "../core/type_pool.h"
#include "../core/type.h"
#include "../core/handle.h"
#include "../core/symbol_table.h"
#include "../core/diagnosis.h"

namespace ydsh {
namespace parser {

using namespace ydsh::core;
using namespace ydsh::ast;

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

class BlockLeavingDetector : protected BaseVisitor {
public:
    enum Action {
        RAISE,
        MARK,
    };

private:
    Action action;

public:
    BlockLeavingDetector(Action action = Action::RAISE): action(action) {}

private:
    void visitDefault(Node &node) override;
    void visitBreakNode(BreakNode &node) override;
    void visitContinueNode(ContinueNode &node) override;
    void visitBlockNode(BlockNode &node) override;
    void visitIfNode(IfNode &node) override;
    void visitTryNode(TryNode &node) override;
    void visitCatchNode(CatchNode &node) override;

public:
    void operator()(BlockNode &node);
};

class TypeChecker : protected NodeVisitor {
public:
    class TypeGenerator : public BaseVisitor {
    private:
        TypePool &pool;

        /**
         * may be null
         */
        TypeChecker *checker;

    public:
        explicit TypeGenerator(TypePool &pool)
                : pool(pool), checker(nullptr) { }

        explicit TypeGenerator(TypeChecker *checker)
                : pool(checker->typePool), checker(checker) { }

        ~TypeGenerator() = default;

        /**
         * entry point.
         * generate DSType from TypeNode.
         */
        DSType &generateTypeAndThrow(TypeNode *typeNode) throw(TypeCheckError);

        void visitDefault(Node &node) override;

        void visitBaseTypeNode(BaseTypeNode &typeNode) override;
        void visitReifiedTypeNode(ReifiedTypeNode &typeNode) override;
        void visitFuncTypeNode(FuncTypeNode &typeNode) override;
        void visitDBusIfaceTypeNode(DBusIfaceTypeNode &typeNode) override;
        void visitReturnTypeNode(ReturnTypeNode &typeNode) override;
        void visitTypeOfNode(TypeOfNode &typeNode) override;

    private:
        DSType &generateType(TypeNode *typeNode);
    };

private:
    /**
     * for type lookup
     */
    TypePool &typePool;

    SymbolTable &symbolTable;
    TypeGenerator typeGen;

    /**
     * contains current return type of current function
     */
    DSType *curReturnType;

    int visitingDepth;

    /**
     * represents loop block depth. (if 0, outside loop block)
     */
    int loopDepth;

    /**
     * represents finally block depth. (if 0, outside finally block)
     */
    int finallyDepth;

    bool toplevelPrinting;

public:
    TypeChecker(TypePool &typePool, SymbolTable &symbolTable);

    ~TypeChecker() = default;

    /**
     * type checker entry point
     */
    void checkTypeRootNode(RootNode &rootNode);

    /**
     * abort symbol table and TypePool when error happened
     */
    void recover(bool abortType = true);

    void setToplevelPrinting(bool set) {
        this->toplevelPrinting = set;
    }

    static DSType *resolveInterface(TypePool &typePool, InterfaceNode *node);
    static DSType *resolveInterface(TypePool &typePool, TypeGenerator &typeGen, InterfaceNode *node);

private:
    // base type check entry point

    /**
     * check node type.
     * if node type is void type, throw exception.
     * return resolved type.
     */
    DSType &checkType(Node *targetNode);

    /**
     * check node type
     *
     * if requiredType is not equivalent to node type, throw exception.
     * return resolved type.
     */
    DSType &checkType(DSType &requiredType, Node *targetNode);

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
    DSType &checkType(DSType *requiredType, Node *targetNode, DSType *unacceptableType);

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

    void resolveCoercion(DSType &requiredType, Node * &targetNode);

    FieldHandle *addEntryAndThrowIfDefined(Node &node, const std::string &symbolName, DSType &type, bool readOnly);

    bool isTopLevel() const {
        return this->visitingDepth == 1;
    }

    void enterLoop() {
        this->loopDepth++;
    }

    void exitLoop() {
        this->loopDepth--;
    }

    /**
     * check node inside loop.
     * if node is out of loop, throw exception
     * node is BreakNode or ContinueNode
     */
    void checkAndThrowIfOutOfLoop(Node &node);

    /**
     * check block end (return, throw) existence in function block.
     * blockNode must be typed.
     * blockNode is function block.
     * returnType is function return type.
     */
    void checkTerminalNodeExistence(BlockNode &blockNode, DSType &returnType);

    void pushReturnType(DSType &returnType);

    /**
     * return null, if outside of function
     */
    DSType *popReturnType();

    /**
     * return null, if outside of function
     */
    DSType *getCurrentReturnType() const {
        return this->curReturnType;
    }

    void checkAndThrowIfInsideFinally(BlockEndNode &node);

    // for apply node type checking

    /**
     * convert TypeToken to DSType..
     */
    DSType &toType(TypeNode *typeToken);

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

    // visitor api
    void visit(Node &node) override;
    void visitBaseTypeNode(BaseTypeNode &typeNode) override;
    void visitReifiedTypeNode(ReifiedTypeNode &typeNode) override;
    void visitFuncTypeNode(FuncTypeNode &typeNode) override;
    void visitDBusIfaceTypeNode(DBusIfaceTypeNode &typeNode) override;
    void visitReturnTypeNode(ReturnTypeNode &typeNode) override;
    void visitTypeOfNode(TypeOfNode &typeNode) override;
    void visitIntValueNode(IntValueNode &node) override;
    void visitLongValueNode(LongValueNode &node) override;
    void visitFloatValueNode(FloatValueNode &node) override;
    void visitStringValueNode(StringValueNode &node) override;
    void visitObjectPathNode(ObjectPathNode &node) override;
    void visitStringExprNode(StringExprNode &node) override;
    void visitArrayNode(ArrayNode &node) override;
    void visitMapNode(MapNode &node) override;
    void visitTupleNode(TupleNode &node) override;
    void visitVarNode(VarNode &node) override;
    void visitAccessNode(AccessNode &node) override;
    void visitCastNode(CastNode &node) override;
    void visitInstanceOfNode(InstanceOfNode &node) override;
    void visitPrintNode(PrintNode &node) override;
    void visitUnaryOpNode(UnaryOpNode &node) override;
    void visitBinaryOpNode(BinaryOpNode &node) override;
    void visitApplyNode(ApplyNode &node) override;
    void visitMethodCallNode(MethodCallNode &node) override;
    void visitNewNode(NewNode &node) override;
    void visitCondOpNode(CondOpNode &node) override;
    void visitTernaryNode(TernaryNode &node) override;
    void visitCmdNode(CmdNode &node) override;
    void visitCmdArgNode(CmdArgNode &node) override;
    void visitRedirNode(RedirNode &node) override;
    void visitTildeNode(TildeNode &node) override;
    void visitPipedCmdNode(PipedCmdNode &node) override;
    void visitSubstitutionNode(SubstitutionNode &node) override;
    void visitAssertNode(AssertNode &node) override;
    void visitBlockNode(BlockNode &node) override;
    void visitBreakNode(BreakNode &node) override;
    void visitContinueNode(ContinueNode &node) override;
    void visitExportEnvNode(ExportEnvNode &node) override;
    void visitImportEnvNode(ImportEnvNode &node) override;
    void visitTypeAliasNode(TypeAliasNode &node) override;
    void visitForNode(ForNode &node) override;
    void visitWhileNode(WhileNode &node) override;
    void visitDoWhileNode(DoWhileNode &node) override;
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
    void visitBindVarNode(BindVarNode &node) override;
    void visitEmptyNode(EmptyNode &node) override;
    void visitDummyNode(DummyNode &node) override;
    void visitRootNode(RootNode &node) override;
};

} // namespace parser
} // namespace ydsh

#endif //YDSH_PARSER_TYPE_CHECKER_H
