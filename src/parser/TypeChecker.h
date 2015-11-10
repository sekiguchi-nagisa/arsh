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

#ifndef YDSH_TYPECHECKER_H
#define YDSH_TYPECHECKER_H

#include "../ast/Node.h"
#include "../core/TypePool.h"
#include "../core/DSType.h"
#include "../core/FieldHandle.h"
#include "../core/SymbolTable.h"
#include "TypeCheckError.hpp"

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
    HandleOrFuncType(FunctionHandle *handle) : hasHandle(true), handle(handle) { }

    HandleOrFuncType(FunctionType *funcType) : hasHandle(false), funcType(funcType) { }

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
    TO_VOID,            // pop evaulated value.
    INT_2_FLOAT,        // int(except for Int64, Uint64) to float
    INT_2_LONG,         // int(except for Int64, Uint64) to long(Int64, Uint64)
    INT_NOP,            // int(except for Int64, Uin64) to int(except for Int64, Uint64)
    LONG_NOP,           // long(Int64, Uint64) to long(Int64, Uint64)
    INVALID_COERCION,   // illegal coercion.
    NOP,                // not allow coercion
};

class TypeChecker : protected NodeVisitor {
public:
    class TypeGenerator : public TypeTokenVisitor {
    private:
        TypePool *pool;
        TypeChecker *checker;
        DSType *type;

    public:
        explicit TypeGenerator(TypePool *pool)
                : pool(pool), checker(nullptr), type(nullptr) { }

        explicit TypeGenerator(TypeChecker *checker)
                : pool(checker->typePool), checker(checker), type(nullptr) { }

        ~TypeGenerator() = default;

        /**
         * entry point.
         * generate DSType from TypeToken.
         */
        DSType *generateTypeAndThrow(TypeToken *token) throw(TypeCheckError);

        void visitClassTypeToken(ClassTypeToken *token);    // overrode
        void visitReifiedTypeToken(ReifiedTypeToken *token);    // override
        void visitFuncTypeToken(FuncTypeToken *token);  // override
        void visitDBusInterfaceToken(DBusInterfaceToken *token);    // override
        void visitReturnTypeToken(ReturnTypeToken *token);  // override
        void visitTypeOfToken(TypeOfToken *token); // override

    private:
        DSType *generateType(TypeToken *token);
    };

private:
    /**
     * for type lookup
     */
    TypePool *typePool;

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

    std::vector<CmdContextNode *> cmdContextStack;

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

    static DSType *resolveInterface(TypePool *typePool, InterfaceNode *node);
    static DSType *resolveInterface(TypePool *typePool, TypeGenerator &typeGen, InterfaceNode *node);

private:
    // base type check entry point

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
     * and if unacceptableType is equivalent to node type, throw exception.
     * return resolved type.
     */
    DSType *checkType(DSType *requiredType, Node *targetNode, DSType *unacceptableType);

    /**
     * root method of checkType
     */
    DSType *checkType(DSType *requiredType, Node *targetNode,
                      DSType *unacceptableType, CoercionKind &kind);

    void checkTypeWithCurrentScope(BlockNode *blockNode);

    /**
     * after type checking.
     * requiredType is not null.
     * if requiredType is FloatType and targetNode->getType() is IntType,
     * wrap targetNode with CastNode.
     * if requiredType is VoidType, wrap targetNode with CastNode
     */
    void checkTypeWithCoercion(DSType *requiredType, Node * &targetNode);

    /**
     * for int type conversion.
     * return true if allow target type to required type implicit cast.
     */
    bool checkCoercion(CoercionKind &kind, DSType *requiredType, DSType *targetType);

    void resolveCoercion(CoercionKind kind, DSType *requiredType, Node * &targetNode);

    FieldHandle *addEntryAndThrowIfDefined(Node &node, const std::string &symbolName, DSType *type, bool readOnly);

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
    void checkTerminalNodeExistence(BlockNode &blockNode, DSType *returnType);

    void pushReturnType(DSType *returnType);

    /**
     * return null, if outside of function
     */
    DSType *popReturnType();

    /**
     * return null, if outside of function
     */
    DSType *getCurrentReturnType();

    void checkAndThrowIfInsideFinally(BlockEndNode &node);

    // for apply node type checking

    /**
     * convert TypeToken to DSType..
     */
    DSType *toType(TypeToken *typeToken);

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

    // for type cast
    bool checkInt2Float(int beforePrecision, DSType *afterType);
    bool checkFloat2Int(DSType *beforeType, int afterPrecision);
    bool checkLong2Float(int beforePrecision, DSType *afterType);
    bool checkFloat2Long(DSType *beforeType, int afterPrecision);

    /**
     * check to higher precision int type.
     */
    bool checkInt2IntWidening(int beforePrecision, int afterPrecision);

    bool checkInt2IntNarrowing(int beforePrecision, int afterPrecision);

    /**
     * some precision cast
     */
    bool checkInt2Int(int beforePrecision, int afterPrecision);

    bool checkLong2Long(int beforePrecision, int afterPrecision);
    bool checkInt2Long(int beforePrecision, int afterPrecision);
    bool checkLong2Int(int beforePrecision, int afterPrecision);

    // visitor api
    void visit(Node &node); // override
    void visitIntValueNode(IntValueNode &node); // override
    void visitLongValueNode(LongValueNode &node); // override
    void visitFloatValueNode(FloatValueNode &node); // override
    void visitStringValueNode(StringValueNode &node); // override
    void visitObjectPathNode(ObjectPathNode &node); // override
    void visitStringExprNode(StringExprNode &node); // override
    void visitArrayNode(ArrayNode &node); // override
    void visitMapNode(MapNode &node); // override
    void visitTupleNode(TupleNode &node); // override
    void visitVarNode(VarNode &node); // override
    void visitAccessNode(AccessNode &node); // override
    void visitCastNode(CastNode &node); // override
    void visitInstanceOfNode(InstanceOfNode &node); // override
    void visitUnaryOpNode(UnaryOpNode &node); // override
    void visitBinaryOpNode(BinaryOpNode &node); // override
    void visitApplyNode(ApplyNode &node); // override
    void visitMethodCallNode(MethodCallNode &node); // override
    void visitNewNode(NewNode &node); // override
    void visitGroupNode(GroupNode &node); // override
    void visitCondOpNode(CondOpNode &node); // override
    void visitCmdNode(CmdNode &node); // override
    void visitCmdArgNode(CmdArgNode &node); // override
    void visitRedirNode(RedirNode &node); // override
    void visitTildeNode(TildeNode &node); // override
    void visitPipedCmdNode(PipedCmdNode &node); // override
    void visitCmdContextNode(CmdContextNode &node); // override
    void visitAssertNode(AssertNode &node); // override
    void visitBlockNode(BlockNode &node); // override
    void visitBreakNode(BreakNode &node); // override
    void visitContinueNode(ContinueNode &node); // override
    void visitExportEnvNode(ExportEnvNode &node); // override
    void visitImportEnvNode(ImportEnvNode &node); // override
    void visitTypeAliasNode(TypeAliasNode &node); // override
    void visitForNode(ForNode &node); // override
    void visitWhileNode(WhileNode &node); // override
    void visitDoWhileNode(DoWhileNode &node); // override
    void visitIfNode(IfNode &node); // override
    void visitReturnNode(ReturnNode &node); // override
    void visitThrowNode(ThrowNode &node); // override
    void visitCatchNode(CatchNode &node); // override
    void visitTryNode(TryNode &node); // override
    void visitVarDeclNode(VarDeclNode &node); // override
    void visitAssignNode(AssignNode &node); // override
    void visitElementSelfAssignNode(ElementSelfAssignNode &node); // override
    void visitFunctionNode(FunctionNode &node); // override
    void visitInterfaceNode(InterfaceNode &node); // override
    void visitUserDefinedCmdNode(UserDefinedCmdNode &node); // override
    void visitBindVarNode(BindVarNode &node); // override
    void visitEmptyNode(EmptyNode &node); // override
    void visitDummyNode(DummyNode &node); // override
    void visitRootNode(RootNode &node); // override
};

} // namespace parser
} // namespace ydsh

#endif //YDSH_TYPECHECKER_H
