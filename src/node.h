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

#ifndef YDSH_NODE_H
#define YDSH_NODE_H

#include <utility>
#include <memory>
#include <list>
#include <cassert>

#include "misc/rtti.hpp"
#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "misc/token.hpp"
#include "token_kind.h"
#include "type.h"
#include "constant.h"
#include "regex_wrapper.h"

namespace ydsh {

class SymbolTable;
class ModType;
struct NodeVisitor;
class NodeDumper;

#define EACH_NODE_KIND(OP) \
    OP(Type) \
    OP(Number) \
    OP(String) \
    OP(StringExpr) \
    OP(Regex) \
    OP(Array) \
    OP(Map) \
    OP(Tuple) \
    OP(Var) \
    OP(Access) \
    OP(TypeOp) \
    OP(UnaryOp) \
    OP(BinaryOp) \
    OP(Apply) \
    OP(New) \
    OP(Embed) \
    OP(Cmd) \
    OP(CmdArg) \
    OP(Redir) \
    OP(Pipeline) \
    OP(With) \
    OP(Fork) \
    OP(Assert) \
    OP(Block) \
    OP(TypeAlias) \
    OP(Loop) \
    OP(If) \
    OP(Case) \
    OP(Arm) \
    OP(Jump) \
    OP(Catch) \
    OP(Try) \
    OP(VarDecl) \
    OP(Assign) \
    OP(ElementSelfAssign) \
    OP(Function) \
    OP(Interface) \
    OP(UserDefinedCmd) \
    OP(Source) \
    OP(Empty)

enum class NodeKind : unsigned char {
#define GEN_ENUM(T) T,
    EACH_NODE_KIND(GEN_ENUM)
#undef GEN_ENUM
};


class Node {
protected:
    const NodeKind nodeKind;

    Token token;

    /**
     * initial value is null.
     */
    const DSType *type{nullptr};

    Node(NodeKind kind, Token token) : nodeKind(kind), token(token) { }

public:
    NON_COPYABLE(Node);

    virtual ~Node() = default;

    NodeKind getNodeKind() const {
        return this->nodeKind;
    }

    bool is(NodeKind kind) const {
        return this->nodeKind == kind;
    }

    Token getToken() const {
        return this->token;
    }

    unsigned int getPos() const {
        return this->token.pos;
    }

    void setPos(unsigned int pos) {
        this->token.pos = pos;
    }

    unsigned int getSize() const {
        return this->token.size;
    }

    void updateToken(Token t) {
        if(t.pos > this->token.pos) {
            this->token.size = t.pos + t.size - this->token.pos;
        }
    }

    void setType(const DSType &t) {
        this->type = &t;
    }

    /**
     * must not call it before type checking
     */
    DSType &getType() const {
        return const_cast<DSType&>(*this->type);
    }

    bool isUntyped() const {
        return this->type == nullptr;
    }

    virtual void dump(NodeDumper &dumper) const = 0;

    void accept(NodeVisitor &visitor);
};

template <typename T, NodeKind K, enable_when<std::is_base_of<Node, T>::value> = nullptr>
class WithRtti : public T {
protected:
    explicit WithRtti(Token token) : T(K, token) {}

public:
    static constexpr auto KIND = K;

    static bool classof(const Node *node) {
        return node->getNodeKind() == K;
    }
};


// type definition
#define EACH_TYPE_NODE_KIND(OP) \
    OP(Base) \
    OP(Reified) \
    OP(Func) \
    OP(Return) \
    OP(TypeOf)

/**
 * represent for parsed type.
 */
class TypeNode : public WithRtti<Node, NodeKind::Type> {
public:
    const enum Kind : unsigned char {
#define GEN_ENUM(OP) OP,
        EACH_TYPE_NODE_KIND(GEN_ENUM)
#undef GEN_ENUM
    } typeKind;

protected:
    TypeNode(Kind typeKind, Token token) : WithRtti(token), typeKind(typeKind) { }

public:
    ~TypeNode() override = default;

    void dump(NodeDumper &dumper) const override;
};

class BaseTypeNode : public TypeNode {
private:
    std::string typeName;

public:
    BaseTypeNode(Token token, std::string &&typeName) :
            TypeNode(TypeNode::Base, token), typeName(std::move(typeName)) { }

    ~BaseTypeNode() override = default;

    const std::string &getTokenText() const {
        return this->typeName;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * for reified type and tuple type
 */
class ReifiedTypeNode : public TypeNode {
private:
    std::unique_ptr<BaseTypeNode> templateTypeNode;
    std::vector<std::unique_ptr<TypeNode>> elementTypeNodes;

public:
    ReifiedTypeNode(std::unique_ptr<BaseTypeNode> &&templateTypeNode,
            std::vector<std::unique_ptr<TypeNode>> &&elementNodes, Token endToken) :
            TypeNode(TypeNode::Reified, templateTypeNode->getToken()),
            templateTypeNode(std::move(templateTypeNode)), elementTypeNodes(std::move(elementNodes)) {
        this->updateToken(endToken);
    }

    ReifiedTypeNode(std::unique_ptr<TypeNode> &&elementNode, std::unique_ptr<BaseTypeNode> &&tempNode) :
            TypeNode(TypeNode::Reified, elementNode->getToken()),
            templateTypeNode(std::move(tempNode)), elementTypeNodes(1) {
        this->elementTypeNodes[0] = std::move(elementNode);
        this->updateToken(this->templateTypeNode->getToken());
    }

    const std::unique_ptr<BaseTypeNode> &getTemplate() const {
        return this->templateTypeNode;
    }

    const std::vector<std::unique_ptr<TypeNode>> &getElementTypeNodes() const {
        return this->elementTypeNodes;
    }

    void dump(NodeDumper &dumper) const override;
};

class FuncTypeNode : public TypeNode {
private:
    std::unique_ptr<TypeNode> returnTypeNode;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<std::unique_ptr<TypeNode>> paramTypeNodes;

public:
    FuncTypeNode(unsigned int startPos, std::unique_ptr<TypeNode> &&returnTypeNode,
                std::vector<std::unique_ptr<TypeNode>> paramTypeNodes, Token endToken) :
            TypeNode(TypeNode::Func, {startPos, 0}),
            returnTypeNode(std::move(returnTypeNode)), paramTypeNodes(std::move(paramTypeNodes)) {
        this->updateToken(endToken);
    }

    FuncTypeNode(unsigned int startPos, std::vector<std::unique_ptr<TypeNode>> paramTypeNodes,
                std::unique_ptr<TypeNode> &&returnTypeNode) :
            TypeNode(TypeNode::Func, {startPos, 0}),
            returnTypeNode(std::move(returnTypeNode)), paramTypeNodes(std::move(paramTypeNodes)) {
        this->updateToken(this->returnTypeNode->getToken());
    }

    const std::vector<std::unique_ptr<TypeNode>> &getParamTypeNodes() const {
        return this->paramTypeNodes;
    }

    const std::unique_ptr<TypeNode> &getReturnTypeNode() const {
        return this->returnTypeNode;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * for multiple return type
 */
class ReturnTypeNode : public TypeNode {
private:
    std::vector<std::unique_ptr<TypeNode>> typeNodes;

public:
    explicit ReturnTypeNode(std::unique_ptr<TypeNode> &&typeNode) :
            TypeNode(TypeNode::Return, typeNode->getToken()) {
        this->addTypeNode(std::move(typeNode));
    }

    void addTypeNode(std::unique_ptr<TypeNode> &&typeNode) {
        this->updateToken(typeNode->getToken());
        this->typeNodes.push_back(std::move(typeNode));
    }

    const std::vector<std::unique_ptr<TypeNode>> &getTypeNodes() const {
        return this->typeNodes;
    }

    bool hasMultiReturn() const {
        return this->typeNodes.size() > 1;
    }

    void dump(NodeDumper &dumper) const override;
};

class TypeOfNode : public TypeNode {
private:
    std::unique_ptr<Node> exprNode;

public:
    TypeOfNode(unsigned int startPos, std::unique_ptr<Node> &&exprNode, Token endToken) :
            TypeNode(TypeNode::TypeOf, {startPos, 0}), exprNode(std::move(exprNode)) {
        this->updateToken(endToken);
    }

    Node &getExprNode() const {
        return *this->exprNode;
    }

    void dump(NodeDumper &dumper) const override;
};

inline TypeNode *newAnyTypeNode() {
    return new BaseTypeNode({0, 0}, std::string("Any"));
}

inline TypeNode *newVoidTypeNode() {
    return new BaseTypeNode({0, 0}, std::string("Void"));
}


// expression definition

#define EACH_NUMBER_NODE_KIND(OP) \
    OP(Int) \
    OP(Float) \
    OP(Signal)

class NumberNode : public WithRtti<Node, NodeKind::Number> {
public:
    const enum Kind : unsigned char {
#define GEN_ENUM(OP) OP,
        EACH_NUMBER_NODE_KIND(GEN_ENUM)
#undef GEN_ENUM
    } kind;

private:
    union {
        int64_t intValue;
        double floatValue;
    };

public:
    NumberNode(Token token, Kind kind) : WithRtti(token), kind(kind), intValue(0) { }

    static std::unique_ptr<NumberNode> newInt(Token token, int64_t value) {
        auto node = std::make_unique<NumberNode>(token, Int);
        node->intValue = value;
        return node;
    }

    static std::unique_ptr<NumberNode> newFloat(Token token, double value) {
        auto node = std::make_unique<NumberNode>(token, Float);
        node->floatValue = value;
        return node;
    }

    static std::unique_ptr<NumberNode> newSignal(Token token, int value) {
        auto node = std::make_unique<NumberNode>(token, Signal);
        node->intValue = value;
        return node;
    }

    ~NumberNode() override = default;

    int64_t getIntValue() const {
        return this->intValue;
    }

    double getFloatValue() const {
        return this->floatValue;
    }

    void dump(NodeDumper &dumper) const override;
};

class StringNode : public WithRtti<Node, NodeKind::String> {
public:
    enum StringKind {
        STRING,
        TILDE,
    };

private:
    std::string value;
    StringKind kind;

public:
    /**
     * used for CommandNode. lineNum is always 0.
     */
    explicit StringNode(std::string &&value) :
            StringNode({0, 0}, std::move(value)) { }

    StringNode(Token token, std::string &&value, StringKind kind = STRING) :
            WithRtti(token), value(std::move(value)), kind(kind) { }

    ~StringNode() override = default;

    const std::string &getValue() const {
        return this->value;
    }

    StringKind getKind() const {
        return this->kind;
    }

    bool isTilde() const {
        return this->getKind() == TILDE;
    }

    void dump(NodeDumper &dumper) const override;

    static std::string extract(StringNode &&node) {
        return std::move(node.value);
    }
};

class StringExprNode : public WithRtti<Node, NodeKind::StringExpr> {
private:
    std::vector<std::unique_ptr<Node>> nodes;

public:
    explicit StringExprNode(unsigned int startPos) : WithRtti({startPos, 1}) { }

    ~StringExprNode() override = default;

    void addExprNode(std::unique_ptr<Node> &&node) {
        this->nodes.push_back(std::move(node));
    }

    const std::vector<std::unique_ptr<Node>> &getExprNodes() const {
        return this->nodes;
    }

    std::vector<std::unique_ptr<Node>> &refExprNodes() {
        return this->nodes;
    }

    void dump(NodeDumper &dumper) const override;
};

class RegexNode : public WithRtti<Node, NodeKind::Regex> {
private:
    /**
     * string representation of regex.
     * TODO: delete it.
     */
    std::string reStr;

    PCRE re;

public:
    RegexNode(Token token, std::string &&str, PCRE &&re) :
            WithRtti(token), reStr(std::move(str)), re(std::move(re)) { }

    ~RegexNode() override = default;

    const std::string &getReStr() const {
        return this->reStr;
    }

    PCRE extractRE() {
        return std::move(this->re);
    }

    void dump(NodeDumper &dumper) const override;
};

class ArrayNode : public WithRtti<Node, NodeKind::Array> {
private:
    std::vector<Node *> nodes;

public:
    ArrayNode(unsigned int startPos, Node *node) : WithRtti({startPos, 0}) {
        this->addExprNode(node);
    }

    ~ArrayNode() override;

    void addExprNode(Node *node) {
        this->nodes.push_back(node);
    }

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    std::vector<Node *> &refExprNodes() {
        return this->nodes;
    }

    void dump(NodeDumper &dumper) const override;
};

class MapNode : public WithRtti<Node, NodeKind::Map> {
private:
    std::vector<Node *> keyNodes;
    std::vector<Node *> valueNodes;

public:
    MapNode(unsigned int startPos, Node *keyNode, Node *valueNode) :
            WithRtti({startPos, 0}) {
        this->addEntry(keyNode, valueNode);
    }

    ~MapNode() override;

    void addEntry(Node *keyNode, Node *valueNode);

    const std::vector<Node *> &getKeyNodes() const {
        return this->keyNodes;
    }

    std::vector<Node *> &refKeyNodes() {
        return this->keyNodes;
    }

    const std::vector<Node *> &getValueNodes() const {
        return this->valueNodes;
    }

    std::vector<Node *> &refValueNodes() {
        return this->valueNodes;
    }

    void dump(NodeDumper &dumper) const override;
};

class TupleNode : public WithRtti<Node, NodeKind::Tuple> {
private:
    /**
     * at least one nodes
     */
    std::vector<std::unique_ptr<Node>> nodes;

public:
    TupleNode(unsigned int startPos, std::vector<std::unique_ptr<Node>> &&nodes, Token endToken) :
            WithRtti({startPos, 0}), nodes(std::move(nodes)) {
        this->updateToken(endToken);
    }

    ~TupleNode() override = default;

    const std::vector<std::unique_ptr<Node>> &getNodes() const {
        return this->nodes;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * base class for VarNode, AccessNode
 */
class AssignableNode : public Node {
protected:
    unsigned int index{0};

    FieldAttribute attribute{};

    AssignableNode(NodeKind kind, Token token) :
            Node(kind, token) { }

public:
    ~AssignableNode() override = default;

    void setAttribute(const FieldHandle &handle) {
        this->index = handle.getIndex();
        this->attribute = handle.attr();
    }

    FieldAttribute attr() const {
        return this->attribute;
    }

    unsigned int getIndex() const {
        return this->index;
    }

    void dump(NodeDumper &dumper) const override;
};

inline bool isAssignable(const Node &node) {
    return node.is(NodeKind::Var) || node.is(NodeKind::Access);
}

class VarNode : public WithRtti<AssignableNode, NodeKind::Var> {
private:
    std::string varName;

public:
    VarNode(Token token, std::string &&varName) :
            WithRtti(token), varName(std::move(varName)) { }

    ~VarNode() override = default;

    const std::string &getVarName() const {
        return this->varName;
    }

    /**
     * force rewrite varName
     * @param name
     */
    void setVarName(std::string &&name) {
        this->varName = std::move(name);
    }

    void dump(NodeDumper &dumper) const override;
};

class AccessNode : public WithRtti<AssignableNode, NodeKind::Access> {
public:
    enum AdditionalOp {
        NOP,
        DUP_RECV,
    };

private:
    Node *recvNode;
    VarNode *nameNode;
    AdditionalOp additionalOp{NOP};

public:
    AccessNode(Node *recvNode, VarNode *nameNode) :
            WithRtti(recvNode->getToken()), recvNode(recvNode), nameNode(nameNode) { }

    ~AccessNode() override;

    Node &getRecvNode() const {
        return *this->recvNode;
    }

    void setRecvNode(Node *node) {
        this->recvNode = node;
    }

    const std::string &getFieldName() const {
        return this->nameNode->getVarName();
    }

    VarNode &getNameNode() const {
        return *this->nameNode;
    }

    void setAdditionalOp(AdditionalOp op) {
        this->additionalOp = op;
    }

    AdditionalOp getAdditionalOp() const {
        return this->additionalOp;
    }

    void dump(NodeDumper &dumper) const override;
};

class TypeOpNode : public WithRtti<Node, NodeKind::TypeOp> {
public:
    /**
     * do not change definition order
     */
    enum OpKind : unsigned char {
        // cast
        NO_CAST,
        TO_VOID,
        NUM_CAST,
        TO_STRING,
        TO_BOOL,
        CHECK_CAST,
        CHECK_UNWRAP,
        PRINT,

        // instance of
        ALWAYS_FALSE,
        ALWAYS_TRUE,
        INSTANCEOF,
    };

private:
    Node *exprNode;

    /**
     * may be tagged pointer
     */
    TypeNode *targetTypeNode;

    OpKind opKind;

public:
     TypeOpNode(Node *exprNode, TypeNode *type, OpKind init, bool dupTypeToken = false);

    ~TypeOpNode() override;

    Node *getExprNode() const {
        return this->exprNode;
    }

    TypeNode *getTargetTypeNode() const;

    void setOpKind(OpKind op) {
        this->opKind = op;
    }

    OpKind getOpKind() const {
        return this->opKind;
    }

    bool isCastOp() const {
        return static_cast<unsigned char>(this->opKind) <= static_cast<unsigned char>(PRINT);
    }

    bool isInstanceOfOp() const {
        return static_cast<unsigned char>(this->opKind) >= static_cast<unsigned char>(ALWAYS_FALSE);
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * for implicit cast.
 * @param targetNode
 * must be typed.
 * @param type
 * @return
 */
TypeOpNode *newTypedCastNode(Node *targetNode, const DSType &type);

/**
 * for function object apply or method call
 */
class ApplyNode : public WithRtti<Node, NodeKind::Apply> {
public:
    enum Kind : unsigned int {
        UNRESOLVED,
        FUNC_CALL,
        METHOD_CALL,
        INDEX_CALL, // special case of method call
    };

private:
    Node *exprNode;
    std::vector<Node *> argNodes;

    /**
     * for method call
     */
    const MethodHandle *handle{nullptr};

    Kind kind;

public:
    ApplyNode(Node *exprNode, std::vector<Node *> &&argNodes, Kind kind = UNRESOLVED);

    static ApplyNode *newMethodCall(Node *recvNode, Token token, std::string &&methodName);

    static ApplyNode *newMethodCall(Node *recvNode, std::string &&methodName) {
        return newMethodCall(recvNode, recvNode->getToken(), std::move(methodName));
    }

    static ApplyNode *newIndexCall(Node *recvNode, Token token, Node *indexNode) {
        auto *node = newMethodCall(recvNode, token, std::string(OP_GET));
        node->setKind(INDEX_CALL);
        node->argNodes.push_back(indexNode);
        return node;
    }

    ~ApplyNode() override;

    Node *getExprNode() const {
        return this->exprNode;
    }

    const std::vector<Node *> &getArgNodes() {
        return this->argNodes;
    }

    std::vector<Node *> &refArgNodes() {
        return this->argNodes;
    }

    const std::string &getMethodName() const {
        assert(this->isMethodCall());
        return cast<AccessNode>(this->exprNode)->getNameNode().getVarName();
    }

    void setMethodName(std::string &&name) {
        assert(this->isIndexCall());
        cast<AccessNode>(this->exprNode)->getNameNode().setVarName(std::move(name));
    }

    Node *getRecvNode() const {
        assert(this->isMethodCall());
        return &cast<AccessNode>(this->exprNode)->getRecvNode();
    }

    Kind getKind() const {
        return this->kind;
    }

    void setKind(Kind k) {
        this->kind = k;
    }

    bool isFuncCall() const {
        return this->getKind() == FUNC_CALL;
    }

    bool isMethodCall() const {
        return this->getKind() == METHOD_CALL || this->isIndexCall();
    }

    bool isIndexCall() const {
        return this->getKind() == INDEX_CALL;
    }

    void setHandle(const MethodHandle *h) {
        this->handle = h;
    }

    const MethodHandle *getHandle() const {
        return this->handle;
    }

    static std::pair<Node *, Node *> split(ApplyNode *&node) {
        auto *first = node->getRecvNode();
        cast<AccessNode>(node->getExprNode())->setRecvNode(nullptr);
        auto *second = node->getArgNodes()[0];
        node->refArgNodes()[0] = nullptr;
        delete node;
        node = nullptr;
        return {first, second};
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * allocate new DSObject and call constructor.
 */
class NewNode : public WithRtti<Node, NodeKind::New> {
private:
    TypeNode *targetTypeNode;
    std::vector<Node *> argNodes;

    const MethodHandle *handle{nullptr};

public:
    NewNode(unsigned int startPos, TypeNode *targetTypeNode, std::vector<Node *> &&argNodes);

    explicit NewNode(TypeNode *targetTypeNode) :
            WithRtti(targetTypeNode->getToken()), targetTypeNode(targetTypeNode) {}

    ~NewNode() override;

    TypeNode *getTargetTypeNode() const {
        return this->targetTypeNode;
    }

    const std::vector<Node *> &getArgNodes() const {
        return this->argNodes;
    }

    std::vector<Node *> &refArgNodes() {
        return this->argNodes;
    }

    void setHandle(const MethodHandle *h) {
        this->handle = h;
    }

    const MethodHandle *getHandle() const {
        return this->handle;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * represents ${}
 * for string interpolation.
 */
class EmbedNode : public WithRtti<Node, NodeKind::Embed> {
public:
    enum Kind {
        STR_EXPR,
        CMD_ARG,
    };

private:
    const Kind kind;

    std::unique_ptr<Node> exprNode;

    const MethodHandle *handle{nullptr}; // for method call

public:
    EmbedNode(unsigned int startPos, Kind kind, std::unique_ptr<Node> &&exprNode, Token endToken) :
        WithRtti({startPos, 1}), kind(kind), exprNode(std::move(exprNode)) {
        this->updateToken(endToken);
    }

    EmbedNode(Kind kind, std::unique_ptr<Node> &&exprNode) :
        WithRtti(exprNode->getToken()), kind(kind), exprNode(std::move(exprNode)) {}

    ~EmbedNode() override = default;

    Kind getKind() const {
        return this->kind;
    }

    Node &getExprNode() const {
        return *this->exprNode;
    }

    void setHandle(const MethodHandle *h) {
        this->handle = h;
    }

    const MethodHandle *getHandle() const {
        return this->handle;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * for unary operator call
 */
class UnaryOpNode : public WithRtti<Node, NodeKind::UnaryOp> {
private:
    TokenKind op;

    Token opToken;  // for line number

    /**
     * after call this->createApplyNode(), will be null.
     */
    Node *exprNode;

    /**
     * before call this->createApplyNode(), it is null.
     */
    ApplyNode *methodCallNode;

public:
    UnaryOpNode(TokenKind op, Token opToken, Node *exprNode) :
            WithRtti(opToken), op(op), opToken(opToken),
            exprNode(exprNode), methodCallNode(nullptr) {
        this->updateToken(exprNode->getToken());
    }

    UnaryOpNode(Node *exprNode, TokenKind op, Token opToken) :
            UnaryOpNode(op, exprNode->getToken(), exprNode) {
        this->updateToken(opToken);
    }

    ~UnaryOpNode() override;

    Node *getExprNode() const {
        return this->exprNode;
    }

    Node *&refExprNode() {
        return this->exprNode;
    }

    TokenKind getOp() const {
        return this->op;
    }

    bool isUnwrapOp() const {
        return this->op == UNWRAP;
    }

    /**
     * create ApplyNode and set to this->applyNode.
     * exprNode will be null.
     */
    ApplyNode *createApplyNode();

    /**
     * return null, before call this->createApplyNode().
     */
    ApplyNode *getApplyNode() const {
        return this->methodCallNode;
    }

    ApplyNode *&refApplyNode() {
        return this->methodCallNode;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * binary operator call.
 */
class BinaryOpNode : public WithRtti<Node, NodeKind::BinaryOp> {
private:
    /**
     * will be null.
     */
    Node *leftNode;

    /**
     * will be null.
     */
    Node *rightNode;

    TokenKind op;

    Token opToken;

    /**
     * initial value is null
     */
    Node *optNode;

public:
    BinaryOpNode(Node *leftNode, TokenKind op, Token opToken, Node *rightNode) :
            WithRtti(leftNode->getToken()),
            leftNode(leftNode), rightNode(rightNode), op(op), opToken(opToken), optNode(nullptr) {
        this->updateToken(rightNode->getToken());
    }

    ~BinaryOpNode() override;

    Node *getLeftNode() const {
        return this->leftNode;
    }

    Node *&refLeftNode() {
        return this->leftNode;
    }

    Node *getRightNode() const {
        return this->rightNode;
    }

    Node *&refRightNode() {
        return this->rightNode;
    }

    TokenKind getOp() const {
        return this->op;
    }

    Node *getOptNode() const {
        return this->optNode;
    }

    void setOptNode(Node *node) {
        this->optNode = node;
    }

    void createApplyNode();

    void dump(NodeDumper &dumper) const override;
};

/**
 * for command argument
 */
class CmdArgNode : public WithRtti<Node, NodeKind::CmdArg> {
private:
    std::vector<std::unique_ptr<Node>> segmentNodes;

public:
    explicit CmdArgNode(std::unique_ptr<Node> &&segmentNode) :
            WithRtti(segmentNode->getToken()) {
        this->addSegmentNode(std::move(segmentNode));
    }

    ~CmdArgNode() override = default;

    void addSegmentNode(std::unique_ptr<Node> &&node);

    const std::vector<std::unique_ptr<Node>> &getSegmentNodes() const {
        return this->segmentNodes;
    }

    void dump(NodeDumper &dumper) const override;

    /**
     * if true, ignore evaluated empty string.
     */
    bool isIgnorableEmptyString() const;
};

class RedirNode : public WithRtti<Node, NodeKind::Redir> {
private:
    TokenKind op;
    std::unique_ptr<CmdArgNode> targetNode;

public:
    RedirNode(TokenKind kind, std::unique_ptr<CmdArgNode> &&node) :
            WithRtti(node->getToken()), op(kind), targetNode(std::move(node)) {}

    RedirNode(TokenKind kind, Token token) :
            RedirNode(kind, std::make_unique<CmdArgNode>(std::make_unique<StringNode>(token, std::string("")))) {}

    ~RedirNode() override = default;

    TokenKind getRedirectOP() const {
        return this->op;
    }

    CmdArgNode &getTargetNode() {
        return *this->targetNode;
    }

    bool isHereStr() const {
        return this->op == REDIR_HERE_STR;
    }

    void dump(NodeDumper &dumper) const override;
};

class CmdNode : public WithRtti<Node, NodeKind::Cmd> {
private:
    std::unique_ptr<StringNode> nameNode;

    /**
     * may be CmdArgNode, RedirNode
     */
    std::vector<std::unique_ptr<Node>> argNodes;

    unsigned int redirCount{0};

    bool inPipe{false};

public:
    explicit CmdNode(std::unique_ptr<StringNode> &&nameNode) :
            WithRtti(nameNode->getToken()), nameNode(std::move(nameNode)) { }

    ~CmdNode() override = default;

    StringNode &getNameNode() const {
        return *this->nameNode;
    }

    void addArgNode(std::unique_ptr<CmdArgNode> &&node);

    const std::vector<std::unique_ptr<Node>> &getArgNodes() const {
        return this->argNodes;
    }

    bool hasRedir() const {
        return this->redirCount > 0;
    }

    void setInPipe(bool in) {
        this->inPipe = in;
    }

    bool getInPipe() const {
        return this->inPipe;
    }

    void addRedirNode(std::unique_ptr<RedirNode> &&node);

    void dump(NodeDumper &dumper) const override;
};

class PipelineNode : public WithRtti<Node, NodeKind::Pipeline> {
private:
    std::vector<std::unique_ptr<Node>> nodes;

    unsigned int baseIndex{0}; // for indicating internal pipeline state index

public:
    PipelineNode(std::unique_ptr<Node> &&leftNode, std::unique_ptr<Node> &&rightNode) :
            WithRtti(leftNode->getToken()) {
        this->addNode(std::move(leftNode));
        this->addNode(std::move(rightNode));
    }

    ~PipelineNode() override = default;

    void addNode(std::unique_ptr<Node> &&node);

    const std::vector<std::unique_ptr<Node>> &getNodes() const {
        return this->nodes;
    }

    void setBaseIndex(unsigned int index) {
        this->baseIndex = index;
    }

    unsigned int getBaseIndex() const {
        return this->baseIndex;
    }

    bool isLastPipe() const {
        return !isa<CmdNode>(*this->nodes.back());
    }

    void dump(NodeDumper &dumper) const override;

private:
    void addNodeImpl(std::unique_ptr<Node> &&node);
};

class WithNode : public WithRtti<Node, NodeKind::With> {
private:
    std::unique_ptr<Node> exprNode;

    std::vector<std::unique_ptr<RedirNode>> redirNodes;

    unsigned int baseIndex{0};

public:
    WithNode(std::unique_ptr<Node> &&exprNode, std::unique_ptr<RedirNode> &&redirNode) :
            WithRtti(exprNode->getToken()), exprNode(std::move(exprNode)) {
        this->addRedirNode(std::move(redirNode));
    }

    ~WithNode() override = default;

    Node &getExprNode() const {
        return *this->exprNode;
    }

    void addRedirNode(std::unique_ptr<RedirNode> &&node) {
        this->updateToken(node->getToken());
        this->redirNodes.push_back(std::move(node));
    }

    const std::vector<std::unique_ptr<RedirNode>> &getRedirNodes() const {
        return this->redirNodes;
    }

    void setBaseIndex(unsigned int index) {
        this->baseIndex = index;
    }

    unsigned int getBaseIndex() const {
        return this->baseIndex;
    }

    void dump(NodeDumper &dumper) const override;
};

class ForkNode : public WithRtti<Node, NodeKind::Fork> {
private:
    ForkKind opKind;
    std::unique_ptr<Node> exprNode;

public:
    ForkNode(Token token, ForkKind kind, std::unique_ptr<Node> &&exprNode, Token endToken) :
            WithRtti(token), opKind(kind), exprNode(std::move(exprNode)) {
        this->updateToken(endToken);
    }

    static auto newCmdSubstitution(unsigned int pos,
            std::unique_ptr<Node> &&exprNode, Token token, bool strExpr) {
        return std::make_unique<ForkNode>(Token{pos, 1},
                strExpr ?  ForkKind::STR : ForkKind::ARRAY, std::move(exprNode), token);
    }

    static auto newProcSubstitution(unsigned int pos,
            std::unique_ptr<Node> &&exprNode, Token token, bool inPipe) {
        return std::make_unique<ForkNode>(Token{pos, 1},
                inPipe ?  ForkKind::IN_PIPE : ForkKind::OUT_PIPE, std::move(exprNode), token);
    }

    static auto newBackground(std::unique_ptr<Node> &&exprNode, Token token, bool disown) {
        auto tok = exprNode->getToken();
        return std::make_unique<ForkNode>(tok, disown ? ForkKind::DISOWN : ForkKind::JOB, std::move(exprNode), token);
    }

    static auto newCoproc(Token token, std::unique_ptr<Node> &&exprNode) {
        auto end = exprNode->getToken();
        return std::make_unique<ForkNode>(token, ForkKind::COPROC, std::move(exprNode), end);
    }

    ~ForkNode() override = default;

    ForkKind getOpKind() const {
        return this->opKind;
    }

    Node &getExprNode() const {
        return *this->exprNode;
    }

    bool isJob() const {
        switch(this->opKind) {
        case ForkKind::JOB:
        case ForkKind::COPROC:
        case ForkKind::DISOWN:
            return true;
        default:
            return false;
        }
    }

    void dump(NodeDumper &dumper) const override;
};

// statement definition

class AssertNode : public WithRtti<Node, NodeKind::Assert> {
private:
    Node *condNode;

    Node *messageNode;

public:
    AssertNode(unsigned int pos, Node *condNode, Node *messageNode) :
            WithRtti({pos, 1}), condNode(condNode), messageNode(messageNode) {
        this->updateToken(messageNode->getToken());
    }

    ~AssertNode() override;

    Node *getCondNode() const {
        return this->condNode;
    }

    Node *&refCondNode() {
        return this->condNode;
    }

    Node *getMessageNode() const {
        return this->messageNode;
    }

    void dump(NodeDumper &dumper) const override;
};

class BlockNode : public WithRtti<Node, NodeKind::Block> {
private:
    std::vector<Node *> nodes;
    unsigned int baseIndex{0};
    unsigned int varSize{0};
    unsigned int maxVarSize{0};

public:
    explicit BlockNode(unsigned int startPos) : WithRtti({startPos, 1}) { }

    ~BlockNode() override;

    void addNode(Node *node) {
        this->nodes.push_back(node);
    }

    void insertNodeToFirst(Node *node);

    const std::vector<Node *> &getNodes() const {
        return this->nodes;
    }

    std::vector<Node *> &refNodes() {
        return this->nodes;
    }

    unsigned int getBaseIndex() const {
        return this->baseIndex;
    }

    void setBaseIndex(unsigned int index) {
        this->baseIndex = index;
    }

    unsigned int getVarSize() const {
        return this->varSize;
    }

    void setVarSize(unsigned int size) {
        this->varSize = size;
    }

    unsigned int getMaxVarSize() const {
        return this->maxVarSize;
    }

    void setMaxVarSize(unsigned int size) {
        this->maxVarSize = size;
    }

    void dump(NodeDumper &dumper) const override;
};

class TypeAliasNode : public WithRtti<Node, NodeKind::TypeAlias> {
private:
    std::string alias;
    std::unique_ptr<TypeNode> targetTypeNode;

public:
    TypeAliasNode(unsigned int startPos, std::string &&alias, std::unique_ptr<TypeNode>  &&targetTypeNode) :
            WithRtti({startPos, 0}), alias(std::move(alias)),
            targetTypeNode(std::move(targetTypeNode)) {
        this->updateToken(this->targetTypeNode->getToken());
    }

    ~TypeAliasNode() override = default;

    const std::string &getAlias() const {
        return this->alias;
    }

    TypeNode &getTargetTypeNode() const {
        return *this->targetTypeNode;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * indicating for, while, do-while statement
 */
class LoopNode : public WithRtti<Node, NodeKind::Loop> {
private:
    Node *initNode;

    /**
     * may be null
     */
    Node *condNode;

    Node *iterNode;

    BlockNode *blockNode;

    bool asDoWhile;

public:
    /**
     * initNode may be null.
     * condNode may be null.
     * iterNode may be null.
     */
    LoopNode(unsigned int startPos, Node *initNode, Node *condNode, Node *iterNode, BlockNode *blockNode, bool asDoWhile = false);

    LoopNode(unsigned int startPos, Node *condNode, BlockNode *blockNode, bool asDoWhile = false) :
            LoopNode(startPos, nullptr, condNode, nullptr, blockNode, asDoWhile) {}

    ~LoopNode() override;

    Node *getInitNode() const {
        return this->initNode;
    }

    Node *&refInitNode() {
        return this->initNode;
    }

    Node *getCondNode() const {
        return this->condNode;
    }

    Node *&refCondNode() {
        return this->condNode;
    }

    Node *getIterNode() const {
        return this->iterNode;
    }

    Node * &refIterNode() {
        return this->iterNode;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    bool isDoWhile() const {
        return this->asDoWhile;
    }

    void dump(NodeDumper &dumper) const override;
};

class IfNode : public WithRtti<Node, NodeKind::If> {
private:
    Node *condNode;
    Node *thenNode;
    Node *elseNode;

public:
    /**
     * elseNode may be null
     */
    IfNode(unsigned int startPos, Node *condNode, Node *thenNode, Node *elseNode);

    ~IfNode() override;

    Node *getCondNode() const {
        return this->condNode;
    }

    Node *&refCondNode() {
        return this->condNode;
    }

    Node *getThenNode() const {
        return this->thenNode;
    }

    Node *&refThenNode() {
        return this->thenNode;
    }

    Node *getElseNode() const {
        return this->elseNode;
    }

    Node *&refElseNode() {
        return this->elseNode;
    }

    void dump(NodeDumper &dumper) const override;
};

class ArmNode : public WithRtti<Node, NodeKind::Arm> {
private:
    /**
     * if represents default pattern, size is 0
     */
    std::vector<Node *> patternNodes;

    /**
     * initial value is null.
     */
    Node *actionNode{nullptr};

public:
    explicit ArmNode(Node *patternNode) : WithRtti(patternNode->getToken()) {
        this->addPatternNode(patternNode);
    }

    explicit ArmNode(unsigned int pos) : WithRtti({pos, 1}) {}

    ~ArmNode() override;

    void setActionNode(Node *node) {
        this->actionNode = node;
        this->updateToken(this->actionNode->getToken());
    }

    void addPatternNode(ydsh::Node *node) {
        this->patternNodes.push_back(node);
    }

    const std::vector<Node *> &getPatternNodes() const {
        return this->patternNodes;
    }

    std::vector<Node *> &refPatternNodes() {
        return this->patternNodes;
    }

    Node *getActionNode() const {
        return this->actionNode;
    }

    Node *&refActionNode() {
        return this->actionNode;
    }

    bool isDefault() const {
        return this->patternNodes.empty();
    }

    void dump(NodeDumper &dumper) const override;
};

class CaseNode : public WithRtti<Node, NodeKind::Case> {
public:
    enum Kind : unsigned int {
        MAP = 0,
        IF_ELSE = 1,
    };

private:
    Node *exprNode;
    std::vector<ArmNode *> armNodes;
    Kind caseKind{MAP};

public:
    CaseNode(unsigned int pos, Node *exprNode) : WithRtti({pos, 1}), exprNode(exprNode) {}

    ~CaseNode() override;

    Node *getExprNode() const {
        return this->exprNode;
    }

    void addArmNode(ArmNode *armNode) {
        this->armNodes.push_back(armNode);
    }

    const std::vector<ArmNode *> &getArmNodes() const {
        return this->armNodes;
    }

    void setCaseKind(Kind k) {
        this->caseKind = k;
    }

    Kind getCaseKind() const {
        return this->caseKind;
    }

    bool hasDefault() const;

    void dump(NodeDumper &dumper) const override;
};

class JumpNode : public WithRtti<Node, NodeKind::Jump> {
public:
    enum OpKind : unsigned int {
        BREAK_,
        CONTINUE_,
        THROW_,
        RETURN_,
    };

private:
    OpKind opKind;
    Node *exprNode;
    bool leavingBlock{false};

    JumpNode(Token token, OpKind kind, Node *exprNode);

public:
    static std::unique_ptr<JumpNode> newBreak(Token token, Node *exprNode) {
        return std::unique_ptr<JumpNode>(new JumpNode(token, BREAK_, exprNode));
    }

    static std::unique_ptr<JumpNode> newContinue(Token token) {
        return std::unique_ptr<JumpNode>(new JumpNode(token, CONTINUE_, nullptr));
    }

    /**
     *
     * @param token
     * @param exprNode
     * not null
     * @return
     */
    static std::unique_ptr<JumpNode> newThrow(Token token, Node *exprNode) {
        return std::unique_ptr<JumpNode>(new JumpNode(token, THROW_, exprNode));
    }

    /**
     *
     * @param token
     * @param exprNode
     * may be null
     * @return
     */
    static std::unique_ptr<JumpNode> newReturn(Token token, Node *exprNode) {
        return std::unique_ptr<JumpNode>(new JumpNode(token, RETURN_, exprNode));
    }

    ~JumpNode() override;


    OpKind getOpKind() const {
        return this->opKind;
    }

    Node &getExprNode() const {
        return *this->exprNode;
    }

    Node *&refExprNode() {
        return this->exprNode;
    }

    void setLeavingBlock(bool leave) {
        this->leavingBlock = leave;
    }

    bool isLeavingBlock() const {
        return this->leavingBlock;
    }

    void dump(NodeDumper &dumper) const override;
};

class CatchNode : public WithRtti<Node, NodeKind::Catch> {
private:
    std::string exceptionName;
    TypeNode *typeNode;

    unsigned int varIndex{0};

    BlockNode *blockNode;

public:
    CatchNode(unsigned int startPos, std::string &&exceptionName,
              TypeNode *typeNode, BlockNode *blockNode) :
            WithRtti({startPos, 0}), exceptionName(std::move(exceptionName)),
            typeNode(typeNode != nullptr ? typeNode : newAnyTypeNode()), blockNode(blockNode) {
        this->updateToken(blockNode->getToken());
    }

    ~CatchNode() override;

    const std::string &getExceptionName() const {
        return this->exceptionName;
    }

    TypeNode &getTypeNode() const {
        return *this->typeNode;
    }

    void setAttribute(const FieldHandle &handle) {
        this->varIndex = handle.getIndex();
    }

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void dump(NodeDumper &dumper) const override;
};

class TryNode : public WithRtti<Node, NodeKind::Try> {
private:
    /**
     * initial value is BlockNode
     */
    Node *exprNode;

    /**
     * may be empty
     */
    std::vector<Node *> catchNodes;

    /**
     * may be null
     */
    Node *finallyNode{nullptr};

public:
    TryNode(unsigned int startPos, BlockNode *blockNode) :
            WithRtti({startPos, 0}), exprNode(blockNode) {
        this->updateToken(blockNode->getToken());
    }

    ~TryNode() override;

    Node *getExprNode() const {
        return this->exprNode;
    }

    Node *&refExprNode() {
        return this->exprNode;
    }

    void addCatchNode(CatchNode *catchNode);

    const std::vector<Node *> &getCatchNodes() const {
        return this->catchNodes;
    }

    std::vector<Node *> &refCatchNodes() {
        return this->catchNodes;
    }

    void addFinallyNode(BlockNode *finallyNode);

    /**
     * if has no finally block, return null
     */
    Node *getFinallyNode() const {
        return this->finallyNode;
    }

    Node *&refFinallyNode() {
        return this->finallyNode;
    }

    void dump(NodeDumper &dumper) const override;
};

class VarDeclNode : public WithRtti<Node, NodeKind::VarDecl> {
public:
    enum Kind : unsigned char {
        VAR,
        CONST,
        IMPORT_ENV,
        EXPORT_ENV,
    };

private:
    std::string varName;
    bool global{false};
    Kind kind;
    unsigned int varIndex{0};

    /**
     * may be null
     */
    Node *exprNode;

public:
    VarDeclNode(unsigned int startPos, std::string &&varName, Node *exprNode, Kind kind);

    ~VarDeclNode() override;

    const std::string &getVarName() const {
        return this->varName;
    }

    Kind getKind() const {
        return this->kind;
    }

    bool isReadOnly() const {
        return this->getKind() == CONST;
    }

    void setAttribute(const FieldHandle &handle);

    bool isGlobal() const {
        return this->global;
    }

    /**
     * may be null
     */
    Node *getExprNode() const {
        return this->exprNode;
    }

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * for assignment, self assignment or named parameter
 * assignment is statement.
 * so, after type checking, type is always VoidType
 */
class AssignNode : public WithRtti<Node, NodeKind::Assign> {
private:
    /**
     * must be VarNode or AccessNode
     */
    Node *leftNode;

    Node *rightNode;
    flag8_set_t attributeSet{0};

public:
    static constexpr flag8_t SELF_ASSIGN  = 1u << 0u;
    static constexpr flag8_t FIELD_ASSIGN = 1u << 1u;

    AssignNode(Node *leftNode, Node *rightNode, bool selfAssign = false) :
            WithRtti(leftNode->getToken()), leftNode(leftNode), rightNode(rightNode) {
        if(selfAssign) {
            setFlag(this->attributeSet, SELF_ASSIGN);
        }
        this->updateToken(rightNode->getToken());
    }

    ~AssignNode() override;

    Node *getLeftNode() const {
        return this->leftNode;
    }

    Node *getRightNode() const {
        return this->rightNode;
    }

    Node * &refRightNode() {
        return this->rightNode;
    }

    void setAttribute(flag8_t flag) {
        setFlag(this->attributeSet, flag);
    }

    bool isSelfAssignment() const {
        return hasFlag(this->attributeSet, SELF_ASSIGN);
    }

    bool isFieldAssign() const {
        return hasFlag(this->attributeSet, FIELD_ASSIGN);
    }

    void dump(NodeDumper &dumper) const override;
};

class ElementSelfAssignNode : public WithRtti<Node, NodeKind::ElementSelfAssign> {
private:
    Node *recvNode;
    Node *indexNode;

    /**
     * receiver and argument are dummy node
     */
    ApplyNode *getterNode;

    /**
     * receiver and argument are dummy node
     */
    ApplyNode *setterNode;

    /**
     * before type checking, rightNode is BinaryOpNode.
     * after type checking, rightNode may be CastNode.
     * if rightNode is BinaryOpNode, left node is dummy node.
     */
    Node *rightNode;

public:
    ElementSelfAssignNode(ApplyNode *leftNode, BinaryOpNode *binaryNode);
    ~ElementSelfAssignNode() override;

    Node *getRecvNode() const {
        return this->recvNode;
    }

    Node *getIndexNode() const {
        return this->indexNode;
    }

    /**
     * may be BinaryOpNode or CastNode
     */
    Node *getRightNode() const {
        return this->rightNode;
    }

    Node * &refRightNode() {
        return this->rightNode;
    }

    ApplyNode *getGetterNode() const {
        return this->getterNode;
    }

    ApplyNode *getSetterNode() const {
        return this->setterNode;
    }

    /**
     * add recv type of getterNode and setterNode
     */
    void setRecvType(DSType &type);

    /**
     * add index type of getterNode and setterNode.
     */
    void setIndexType(DSType &type);

    void dump(NodeDumper &dumper) const override;
};

class FunctionNode : public WithRtti<Node, NodeKind::Function> {
private:
    std::string funcName;

    /**
     * for parameter definition.
     */
    std::vector<std::unique_ptr<VarNode>> paramNodes;

    /**
     * type token of each parameter
     */
    std::vector<std::unique_ptr<TypeNode>> paramTypeNodes;

    std::unique_ptr<TypeNode> returnTypeNode;

    std::unique_ptr<BlockNode> blockNode;

    /**
     * maximum number of local variable in function
     */
    unsigned int maxVarNum{0};

    /**
     * global variable table index of this function
     */
    unsigned int varIndex{0};

    FunctionType *funcType{nullptr};

public:
    FunctionNode(unsigned int startPos, std::string &&funcName) :
            WithRtti({startPos, 0}), funcName(std::move(funcName)) { }

    ~FunctionNode() override = default;

    const std::string &getFuncName() const {
        return this->funcName;
    }

    void addParamNode(std::unique_ptr<VarNode> &&node, std::unique_ptr<TypeNode> &&paramType) {
        this->paramNodes.push_back(std::move(node));
        this->paramTypeNodes.push_back(std::move(paramType));
    }

    const std::vector<std::unique_ptr<VarNode>> &getParamNodes() const {
        return this->paramNodes;
    }

    const std::vector<std::unique_ptr<TypeNode>> &getParamTypeNodes() const {
        return this->paramTypeNodes;
    }

    void setReturnTypeToken(std::unique_ptr<TypeNode> &&typeToken) {
        this->returnTypeNode = std::move(typeToken);
        this->updateToken(this->returnTypeNode->getToken());
    }

    TypeNode &getReturnTypeToken() {
        return *this->returnTypeNode;
    }

    void setBlockNode(std::unique_ptr<BlockNode> &&node) {
        this->blockNode = std::move(node);
        this->updateToken(this->blockNode->getToken());
    }

    /**
     * return null before call setBlockNode()
     */
    BlockNode &getBlockNode() const {
        return *this->blockNode;
    }

    void setMaxVarNum(unsigned int num) {
        this->maxVarNum = num;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void setVarIndex(unsigned int index) {
        this->varIndex = index;
    }

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    void setFuncType(FunctionType *type) {
        this->funcType = type;
    }

    /**
     *
     * @return
     * may be null
     */
    FunctionType *getFuncType() const {
        return this->funcType;
    }

    void dump(NodeDumper &dumper) const override;
};

class InterfaceNode : public WithRtti<Node, NodeKind::Interface> {
private:
    std::string interfaceName;

    std::vector<FunctionNode *> methodDeclNodes;
    std::vector<VarDeclNode *> fieldDeclNodes;
    std::vector<TypeNode *> fieldTypeNodes;

public:
    InterfaceNode(unsigned int startPos, std::string &&interfaceName) :
            WithRtti({startPos, 0}), interfaceName(std::move(interfaceName)) { }

    ~InterfaceNode() override;

    const std::string &getInterfaceName() const {
        return this->interfaceName;
    }

    void addMethodDeclNode(FunctionNode *methodDeclNode);

    const std::vector<FunctionNode *> &getMethodDeclNodes() const {
        return this->methodDeclNodes;
    }

    /**
     * initNode of node is null.
     */
    void addFieldDecl(VarDeclNode *node, TypeNode *typeNode);

    const std::vector<VarDeclNode *> &getFieldDeclNodes() const {
        return this->fieldDeclNodes;
    }

    const std::vector<TypeNode *> &getFieldTypeNodes() const {
        return this->fieldTypeNodes;
    }

    void dump(NodeDumper &dumper) const override;
};

class UserDefinedCmdNode : public WithRtti<Node, NodeKind::UserDefinedCmd> {
private:
    std::string cmdName;

    unsigned int udcIndex{0};
    std::unique_ptr<BlockNode> blockNode;

    unsigned int maxVarNum{0};

public:
    UserDefinedCmdNode(unsigned int startPos, std::string &&commandName,
            std::unique_ptr<BlockNode> &&blockNode) :
            WithRtti({startPos, 0}),
            cmdName(std::move(commandName)), blockNode(std::move(blockNode)) {
        this->updateToken(this->blockNode->getToken());
    }

    ~UserDefinedCmdNode() override = default;

    const std::string &getCmdName() const {
        return this->cmdName;
    }

    unsigned int getUdcIndex() const {
        return this->udcIndex;
    }

    void setUdcIndex(unsigned int index) {
        this->udcIndex = index;
    }

    BlockNode &getBlockNode() const {
        return *this->blockNode;
    }

    void setMaxVarNum(unsigned int num) {
        this->maxVarNum = num;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void dump(NodeDumper &dumper) const override;
};

class SourceNode : public WithRtti<Node, NodeKind::Source> {
private:
    std::unique_ptr<StringNode> pathNode;

    /**
     * may be empty string
     */
    std::string name;

    /**
     * resolved module type.
     */
    ModType *modType{nullptr};

    bool firstAppear{false};

    bool nothing{false};

    /**
     * if true, ignore module not found error
     */
    bool optional{false};

    unsigned int modIndex{0};

    /**
     * global variable index of module object
     */
    unsigned int index{0};

    /**
     * maximum number of local variable in this module
     */
    unsigned int maxVarNum{0};

public:
    SourceNode(unsigned int startPos, std::unique_ptr<StringNode> &&pathNode, bool optional) :
            WithRtti({startPos, 1}), pathNode(std::move(pathNode)), optional(optional) {
        this->updateToken(this->pathNode->getToken());
    }

    ~SourceNode() override = default;

    StringNode &getPathNode() const {
        return *this->pathNode;
    }

    const std::string &getPathStr() const {
        return this->pathNode->getValue();
    }

    void setName(Token token, std::string &&value) {
        this->updateToken(token);
        this->name = std::move(value);
    }

    const std::string &getName() const {
        return this->name;
    }

    void setModType(ModType &type) {
        this->modType = &type;
    }

    ModType *getModType() {
        return this->modType;
    }

    void setFirstAppear(bool b) {
        this->firstAppear = b;
    }

    bool isFirstAppear() const {
        return this->firstAppear;
    }

    void setNothing(bool set) {
        this->nothing = set;
    }

    bool isNothing() const {
        return this->nothing;
    }

    bool isOptional() const {
        return this->optional;
    }

    void setModIndex(unsigned int value) {
        this->modIndex = value;
    }

    unsigned int getModIndex() const {
        return this->modIndex;
    }

    void setIndex(unsigned int value) {
        this->index = value;
    }

    unsigned int getIndex() const {
        return this->index;
    }

    void setMaxVarNum(unsigned int v) {
        this->maxVarNum = v;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void dump(NodeDumper &dumper) const override;
};

class EmptyNode : public WithRtti<Node, NodeKind::Empty> {
public:
    EmptyNode() : EmptyNode({0, 0}) { }
    explicit EmptyNode(Token token) : WithRtti(token) { }
    ~EmptyNode() override = default;

    void dump(NodeDumper &dumper) const override;
};

// helper function for node creation

const char *resolveUnaryOpName(TokenKind op);

const char *resolveBinaryOpName(TokenKind op);

TokenKind resolveAssignOp(TokenKind op);

std::unique_ptr<LoopNode> createForInNode(unsigned int startPos, std::string &&varName, Node *exprNode, BlockNode *blockNode);

std::unique_ptr<Node> createAssignNode(std::unique_ptr<Node> &&leftNode,
        TokenKind op, Token token, std::unique_ptr<Node> &&rightNode);

inline std::unique_ptr<Node> createSuffixNode(std::unique_ptr<Node> &&leftNode, TokenKind op, Token token) {
    return createAssignNode(std::move(leftNode), op, token, NumberNode::newInt(token, 1));
}

/**
 *
 * @param kind
 * @param node
 * may be TypeOpNode
 * @return
 */
const Node *findInnerNode(NodeKind kind, const Node *node);

template <typename T>
inline const T *findInnerNode(const Node *node) {
    return static_cast<const T *>(findInnerNode(T::KIND, node));
}


struct NodeVisitor {
    virtual ~NodeVisitor() = default;

    virtual void visit(Node &node) { node.accept(*this); }
    virtual void visitTypeNode(TypeNode &node) = 0;
    virtual void visitNumberNode(NumberNode &node) = 0;
    virtual void visitStringNode(StringNode &node) = 0;
    virtual void visitStringExprNode(StringExprNode &node) = 0;
    virtual void visitRegexNode(RegexNode &node) = 0;
    virtual void visitArrayNode(ArrayNode &node) = 0;
    virtual void visitMapNode(MapNode &node) = 0;
    virtual void visitTupleNode(TupleNode &node) = 0;
    virtual void visitVarNode(VarNode &node) = 0;
    virtual void visitAccessNode(AccessNode &node) = 0;
    virtual void visitTypeOpNode(TypeOpNode &node) = 0;
    virtual void visitUnaryOpNode(UnaryOpNode &node) = 0;
    virtual void visitBinaryOpNode(BinaryOpNode &node) = 0;
    virtual void visitApplyNode(ApplyNode &node) = 0;
    virtual void visitEmbedNode(EmbedNode &node) = 0;
    virtual void visitNewNode(NewNode &node) = 0;
    virtual void visitForkNode(ForkNode &node) = 0;
    virtual void visitCmdNode(CmdNode &node) = 0;
    virtual void visitCmdArgNode(CmdArgNode &node) = 0;
    virtual void visitRedirNode(RedirNode &node) = 0;
    virtual void visitPipelineNode(PipelineNode &node) = 0;
    virtual void visitWithNode(WithNode &node) = 0;
    virtual void visitAssertNode(AssertNode &node) = 0;
    virtual void visitBlockNode(BlockNode &node) = 0;
    virtual void visitTypeAliasNode(TypeAliasNode &node) = 0;
    virtual void visitLoopNode(LoopNode &node) = 0;
    virtual void visitIfNode(IfNode &node) = 0;
    virtual void visitCaseNode(CaseNode &node) = 0;
    virtual void visitArmNode(ArmNode &node) = 0;
    virtual void visitJumpNode(JumpNode &node) = 0;
    virtual void visitCatchNode(CatchNode &node) = 0;
    virtual void visitTryNode(TryNode &node) = 0;
    virtual void visitVarDeclNode(VarDeclNode &node) = 0;
    virtual void visitAssignNode(AssignNode &node) = 0;
    virtual void visitElementSelfAssignNode(ElementSelfAssignNode &node) = 0;
    virtual void visitFunctionNode(FunctionNode &node) = 0;
    virtual void visitInterfaceNode(InterfaceNode &node) = 0;
    virtual void visitUserDefinedCmdNode(UserDefinedCmdNode &node) = 0;
    virtual void visitSourceNode(SourceNode &node) = 0;
    virtual void visitEmptyNode(EmptyNode &node) = 0;
};

struct BaseVisitor : public NodeVisitor {
    ~BaseVisitor() override = default;

    virtual void visitDefault(Node &node) = 0;

    void visitTypeNode(TypeNode &node) override { this->visitDefault(node); }
    void visitNumberNode(NumberNode &node) override { this->visitDefault(node); }
    void visitStringNode(StringNode &node) override { this->visitDefault(node); }
    void visitStringExprNode(StringExprNode &node) override { this->visitDefault(node); }
    void visitRegexNode(RegexNode &node) override { this->visitDefault(node); }
    void visitArrayNode(ArrayNode &node) override { this->visitDefault(node); }
    void visitMapNode(MapNode &node) override { this->visitDefault(node); }
    void visitTupleNode(TupleNode &node) override { this->visitDefault(node); }
    void visitVarNode(VarNode &node) override { this->visitDefault(node); }
    void visitAccessNode(AccessNode &node) override { this->visitDefault(node); }
    void visitTypeOpNode(TypeOpNode &node) override { this->visitDefault(node); }
    void visitUnaryOpNode(UnaryOpNode &node) override { this->visitDefault(node); }
    void visitBinaryOpNode(BinaryOpNode &node) override { this->visitDefault(node); }
    void visitApplyNode(ApplyNode &node) override { this->visitDefault(node); }
    void visitNewNode(NewNode &node) override { this->visitDefault(node); }
    void visitEmbedNode(EmbedNode &node) override { this->visitDefault(node); }
    void visitCmdNode(CmdNode &node) override { this->visitDefault(node); }
    void visitCmdArgNode(CmdArgNode &node) override { this->visitDefault(node); }
    void visitRedirNode(RedirNode &node) override { this->visitDefault(node); }
    void visitPipelineNode(PipelineNode &node) override { this->visitDefault(node); }
    void visitWithNode(WithNode &node) override { this->visitDefault(node); }
    void visitForkNode(ForkNode &node) override { this->visitDefault(node); }
    void visitAssertNode(AssertNode &node) override { this->visitDefault(node); }
    void visitBlockNode(BlockNode &node) override { this->visitDefault(node); }
    void visitTypeAliasNode(TypeAliasNode &node) override { this->visitDefault(node); }
    void visitLoopNode(LoopNode &node) override { this->visitDefault(node); }
    void visitIfNode(IfNode &node) override { this->visitDefault(node); }
    void visitCaseNode(CaseNode &node) override { this->visitDefault(node); }
    void visitArmNode(ArmNode &node) override { this->visitDefault(node); }
    void visitJumpNode(JumpNode &node) override { this->visitDefault(node); }
    void visitCatchNode(CatchNode &node) override { this->visitDefault(node); }
    void visitTryNode(TryNode &node) override { this->visitDefault(node); }
    void visitVarDeclNode(VarDeclNode &node) override { this->visitDefault(node); }
    void visitAssignNode(AssignNode &node) override { this->visitDefault(node); }
    void visitElementSelfAssignNode(ElementSelfAssignNode &node) override { this->visitDefault(node); }
    void visitFunctionNode(FunctionNode &node) override { this->visitDefault(node); }
    void visitInterfaceNode(InterfaceNode &node) override { this->visitDefault(node); }
    void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override { this->visitDefault(node); }
    void visitSourceNode(SourceNode &node) override { this->visitDefault(node); }
    void visitEmptyNode(EmptyNode &node) override { this->visitDefault(node); }
};

class NodeDumper {
private:
    FILE *fp;
    const SymbolTable &symbolTable;

    struct DumpBuffer {
        unsigned int indentLevel{0};
        std::string value;
    };

    std::list<DumpBuffer> bufs;

public:
    NodeDumper(FILE *fp, const SymbolTable &symbolTable) : fp(fp), symbolTable(symbolTable) { }

    ~NodeDumper() = default;

    /**
     * dump field
     */
    void dump(const char *fieldName, const char *value);

    void dump(const char *fieldName, const std::string &value) {
        this->dump(fieldName, value.c_str());
    }

    template <typename T, enable_when<std::is_convertible<T *, Node *>::value> = nullptr>
    void dump(const char *fieldName, const std::vector<std::unique_ptr<T>> &nodes) {
        this->dumpNodesHead(fieldName);
        for(auto &e : nodes) {
            this->dumpNodesBody(*e);
        }
        this->dumpNodesTail();
    }

    template <typename T, enable_when<std::is_convertible<T *, Node *>::value> = nullptr>
    void dump(const char *fieldName, const std::vector<T *> &nodes) {
        this->dumpNodesHead(fieldName);
        for(auto &e : nodes) {
            this->dumpNodesBody(*e);
        }
        this->dumpNodesTail();
    }

    /**
     * dump node with indent
     */
    void dump(const char *fieldName, const Node &node);

    void dump(const char *fieldName, const DSType &type);

    void dump(const char *fieldName, TokenKind kind);

    void dump(const char *fieldName, const MethodHandle &handle);

    void dumpNull(const char *fieldName);

    /**
     * dump node without indent
     */
    void dump(const Node &node);

    void enterModule(const char *sourceName, const char *header = nullptr);

    void leaveModule();

    /**
     * entry point
     */
    void initialize(const std::string &sourceName, const char *header) {
        this->enterModule(sourceName.c_str(), header);
    }

    void operator()(const Node &node);

    void finalize();

    explicit operator bool() const {
        return this->fp != nullptr;
    }

private:
    void enterIndent() {
        this->bufs.back().indentLevel++;
    }

    void leaveIndent() {
        this->bufs.back().indentLevel--;
    }

    void indent();

    void newline() {
        this->append('\n');
    }

    void append(int ch);

    void append(const char *str);

    void appendAs(const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));

    void dumpNodeHeader(const Node &node, bool inArray = false);

    void dumpNodesHead(const char *fieldName) {
        this->writeName(fieldName);
        this->newline();
        this->enterIndent();
    }

    void dumpNodesBody(const Node &node) {
        this->indent();
        this->append("- ");
        this->dumpNodeHeader(node, true);
        this->enterIndent();
        node.dump(*this);
        this->leaveIndent();
    }

    void dumpNodesTail() {
        this->leaveIndent();
    }

    void writeName(const char *fieldName);
};

} // namespace ydsh

#endif //YDSH_NODE_H
