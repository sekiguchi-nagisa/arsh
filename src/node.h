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

#include "misc/detect.hpp"
#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "misc/token.hpp"
#include "token_kind.h"
#include "type.h"
#include "handle.h"
#include "constant.h"
#include "regex_wrapper.h"

namespace ydsh {

class SymbolTable;
class ModType;
class FieldHandle;
class MethodHandle;
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

    void updateToken(Token token) {
        if(token.pos > this->token.pos) {
            this->token.size = token.pos + token.size - this->token.pos;
        }
    }

    void setType(const DSType &type) {
        this->type = &type;
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
class TypeNode : public Node {
public:
    const enum Kind : unsigned char {
#define GEN_ENUM(OP) OP,
        EACH_TYPE_NODE_KIND(GEN_ENUM)
#undef GEN_ENUM
    } typeKind;

protected:
    TypeNode(Kind typeKind, Token token) : Node(NodeKind::Type, token), typeKind(typeKind) { }

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
    BaseTypeNode *templateTypeNode;
    std::vector<TypeNode *> elementTypeNodes;

public:
    explicit ReifiedTypeNode(BaseTypeNode *templateTypeNode) :
            TypeNode(TypeNode::Reified, templateTypeNode->getToken()),
            templateTypeNode(templateTypeNode) { }

    ~ReifiedTypeNode() override;

    void addElementTypeNode(TypeNode *typeNode);

    BaseTypeNode *getTemplate() const {
        return this->templateTypeNode;
    }

    const std::vector<TypeNode *> &getElementTypeNodes() const {
        return this->elementTypeNodes;
    }

    void dump(NodeDumper &dumper) const override;
};

class FuncTypeNode : public TypeNode {
private:
    TypeNode *returnTypeNode;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<TypeNode *> paramTypeNodes;

public:
    FuncTypeNode(unsigned int startPos, TypeNode *returnTypeNode) :
            TypeNode(TypeNode::Func, {startPos, 0}),
            returnTypeNode(returnTypeNode) { }

    ~FuncTypeNode() override;

    void addParamTypeNode(TypeNode *typeNode);

    const std::vector<TypeNode *> &getParamTypeNodes() const {
        return this->paramTypeNodes;
    }

    TypeNode *getReturnTypeNode() const {
        return this->returnTypeNode;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * for multiple return type
 */
class ReturnTypeNode : public TypeNode {
private:
    std::vector<TypeNode *> typeNodes;

public:
    explicit ReturnTypeNode(TypeNode *typeNode) :
            TypeNode(TypeNode::Return, typeNode->getToken()) {
        this->addTypeNode(typeNode);
    }

    ~ReturnTypeNode() override;

    void addTypeNode(TypeNode *typeNode);

    const std::vector<TypeNode *> &getTypeNodes() const {
        return this->typeNodes;
    }

    bool hasMultiReturn() const {
        return this->typeNodes.size() > 1;
    }

    void dump(NodeDumper &dumper) const override;
};

class TypeOfNode : public TypeNode {
private:
    Node *exprNode;

public:
    TypeOfNode(unsigned int startPos, Node *exprNode) :
            TypeNode(TypeNode::TypeOf, {startPos, 0}), exprNode(exprNode) {
        this->updateToken(exprNode->getToken());
    }

    ~TypeOfNode() override;

    Node *getExprNode() const {
        return this->exprNode;
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
    OP(Int32) \
    OP(Int64) \
    OP(Float) \
    OP(Signal)

class NumberNode : public Node {
public:
    const enum Kind : unsigned char {
#define GEN_ENUM(OP) OP,
        EACH_NUMBER_NODE_KIND(GEN_ENUM)
#undef GEN_ENUM
    } kind;

private:
    union {
        int intValue;
        long longValue;
        double floatValue;
    };

     NumberNode(Token token, Kind kind) :
            Node(NodeKind::Number, token), kind(kind), intValue(0) { }

public:
    static NumberNode *newInt32(Token token, int value) {
        auto *node = new NumberNode(token, Int32);
        node->intValue = value;
        return node;
    }

    static NumberNode *newInt64(Token token, long value) {
        auto *node = new NumberNode(token, Int64);
        node->longValue = value;
        return node;
    }

    static NumberNode *newFloat(Token token, double value) {
        auto *node = new NumberNode(token, Float);
        node->floatValue = value;
        return node;
    }

    static NumberNode *newSignal(Token token, int value) {
        auto *node = new NumberNode(token, Signal);
        node->intValue = value;
        return node;
    }

    ~NumberNode() override = default;

    int getIntValue() const {
        return this->intValue;
    }

    long getLongValue() const {
        return this->longValue;
    }

    double getFloatValue() const {
        return this->floatValue;
    }

    void dump(NodeDumper &dumper) const override;
};

class StringNode : public Node {
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
            Node(NodeKind::String, token), value(std::move(value)), kind(kind) { }

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

class StringExprNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    explicit StringExprNode(unsigned int startPos) :
            Node(NodeKind::StringExpr, {startPos, 1}) { }

    ~StringExprNode() override;

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    void setExprNode(unsigned int index, Node *exprNode) {
        this->nodes[index] = exprNode;
    }

    void dump(NodeDumper &dumper) const override;
};

class RegexNode : public Node {
private:
    /**
     * string representation of regex.
     * TODO: delete it.
     */
    std::string reStr;

    PCRE re;

public:
    RegexNode(Token token, std::string &&str, PCRE &&re) :
            Node(NodeKind::Regex, token), reStr(std::move(str)), re(std::move(re)) { }

    ~RegexNode() override = default;

    const std::string &getReStr() const {
        return this->reStr;
    }

    PCRE extractRE() {
        return std::move(this->re);
    }

    void dump(NodeDumper &dumper) const override;
};

class ArrayNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    ArrayNode(unsigned int startPos, Node *node) :
            Node(NodeKind::Array, {startPos, 0}) {
        this->addExprNode(node);
    }

    ~ArrayNode() override;

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    std::vector<Node *> &refExprNodes() {
        return this->nodes;
    }

    void dump(NodeDumper &dumper) const override;
};

class MapNode : public Node {
private:
    std::vector<Node *> keyNodes;
    std::vector<Node *> valueNodes;

public:
    MapNode(unsigned int startPos, Node *keyNode, Node *valueNode) :
            Node(NodeKind::Map, {startPos, 0}) {
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

class TupleNode : public Node {
private:
    /**
     * at least two nodes
     */
    std::vector<Node *> nodes;

public:
    TupleNode(unsigned int startPos, Node *node) :
            Node(NodeKind::Tuple, {startPos, 0}) {
        this->addNode(node);
    }

    ~TupleNode() override;

    void addNode(Node *node);

    const std::vector<Node *> &getNodes() const {
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

class VarNode : public AssignableNode {
private:
    std::string varName;

public:
    VarNode(Token token, std::string &&varName) :
            AssignableNode(NodeKind::Var, token), varName(std::move(varName)) { }

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

class AccessNode : public AssignableNode {
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
            AssignableNode(NodeKind::Access, recvNode->getToken()),
            recvNode(recvNode), nameNode(nameNode) { }

    ~AccessNode() override;

    Node *getRecvNode() const {
        return this->recvNode;
    }

    void setRecvNode(Node *recvNode) {
        this->recvNode = recvNode;
    }

    const std::string &getFieldName() const {
        return this->nameNode->getVarName();
    }

    VarNode *getNameNode() const {
        return this->nameNode;
    }

    void setAdditionalOp(AdditionalOp op) {
        this->additionalOp = op;
    }

    AdditionalOp getAdditionalOp() const {
        return this->additionalOp;
    }

    void dump(NodeDumper &dumper) const override;
};

class TypeOpNode : public Node {
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

    void setOpKind(OpKind opKind) {
        this->opKind = opKind;
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
class ApplyNode : public Node {
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
        return static_cast<AccessNode *>(this->exprNode)->getNameNode()->getVarName();
    }

    void setMethodName(std::string &&name) {
        assert(this->isIndexCall());
        static_cast<AccessNode *>(this->exprNode)->getNameNode()->setVarName(std::move(name));
    }

    Node *getRecvNode() const {
        assert(this->isMethodCall());
        return static_cast<AccessNode *>(this->exprNode)->getRecvNode();
    }

    Kind getKind() const {
        return this->kind;
    }

    void setKind(Kind kind) {
        this->kind = kind;
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

    void setHandle(const MethodHandle *handle) {
        this->handle = handle;
    }

    const MethodHandle *getHandle() const {
        return this->handle;
    }

    static std::pair<Node *, Node *> split(ApplyNode *&node) {
        auto *first = node->getRecvNode();
        static_cast<AccessNode *>(node->getExprNode())->setRecvNode(nullptr);
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
class NewNode : public Node {
private:
    TypeNode *targetTypeNode;
    std::vector<Node *> argNodes;

public:
    NewNode(unsigned int startPos, TypeNode *targetTypeNode, std::vector<Node *> &&argNodes);

    explicit NewNode(TypeNode *targetTypeNode) : NewNode(targetTypeNode->getPos(), targetTypeNode, {}) {}

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

    void dump(NodeDumper &dumper) const override;
};

/**
 * represents ${}
 * for string interpolation.
 */
class EmbedNode : public Node {
public:
    enum Kind {
        STR_EXPR,
        CMD_ARG,
    };

private:
    const Kind kind;

    Node *exprNode;

    const MethodHandle *handle{nullptr}; // for method call

public:
    EmbedNode(unsigned int startPos, Kind kind, Node *exprNode, Token endToken) :
        Node(NodeKind::Embed, {startPos, 1}), kind(kind), exprNode(exprNode) {
        this->updateToken(endToken);
    }

    EmbedNode(Kind kind, Node *exprNode) :
        Node(NodeKind::Embed, exprNode->getToken()), kind(kind), exprNode(exprNode) {}

    ~EmbedNode() override;

    Kind getKind() const {
        return this->kind;
    }

    Node *getExprNode() const {
        return this->exprNode;
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
class UnaryOpNode : public Node {
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
            Node(NodeKind::UnaryOp, opToken), op(op), opToken(opToken),
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
class BinaryOpNode : public Node {
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
            Node(NodeKind::BinaryOp, leftNode->getToken()),
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
class CmdArgNode : public Node {
private:
    std::vector<Node *> segmentNodes;

public:
    explicit CmdArgNode(Node *segmentNode) :
            Node(NodeKind::CmdArg, segmentNode->getToken()) {
        this->addSegmentNode(segmentNode);
    }

    ~CmdArgNode() override;

    void addSegmentNode(Node *node);

    const std::vector<Node *> &getSegmentNodes() const {
        return this->segmentNodes;
    }
    
    void setSegmentNode(unsigned int index, Node *segmentNode) {
        this->segmentNodes[index] = segmentNode;
    }

    void dump(NodeDumper &dumper) const override;

    /**
     * if true, ignore evaluated empty string.
     */
    bool isIgnorableEmptyString();
};

class RedirNode : public Node {
private:
    TokenKind op;
    CmdArgNode *targetNode;

public:
    RedirNode(TokenKind kind, CmdArgNode *node) :
            Node(NodeKind::Redir, node->getToken()), op(kind), targetNode(node) {}

    RedirNode(TokenKind kind, Token token) :
            RedirNode(kind, new CmdArgNode(new StringNode(token, std::string("")))) {}

    ~RedirNode() override;

    TokenKind getRedirectOP() {
        return this->op;
    }

    CmdArgNode *getTargetNode() {
        return this->targetNode;
    }

    bool isHereStr() const {
        return this->op == REDIR_HERE_STR;
    }

    void dump(NodeDumper &dumper) const override;
};

class CmdNode : public Node {
private:
    /**
     * must be StringNode.
     */
    Node *nameNode;

    /**
     * may be CmdArgNode, RedirNode
     */
    std::vector<Node *> argNodes;

    unsigned int redirCount{0};

    bool inPipe{false};

public:
    explicit CmdNode(StringNode *nameNode) :
            Node(NodeKind::Cmd, nameNode->getToken()),
            nameNode(nameNode) { }

    ~CmdNode() override;

    Node *getNameNode() const {
        return this->nameNode;
    }

    void addArgNode(CmdArgNode *node);

    const std::vector<Node *> &getArgNodes() const {
        return this->argNodes;
    }

    bool hasRedir() const {
        return this->redirCount > 0;
    }

    void setInPipe(bool inPipe) {
        this->inPipe = inPipe;
    }

    bool getInPipe() const {
        return this->inPipe;
    }

    void addRedirNode(RedirNode *node);

    void dump(NodeDumper &dumper) const override;
};

class PipelineNode : public Node {
private:
    std::vector<Node *> nodes;

    unsigned int baseIndex{0}; // for indicating internal pipeline state index

public:
    PipelineNode(Node *leftNode, Node *rightNode) :
            Node(NodeKind::Pipeline, leftNode->getToken()) {
        this->addNode(leftNode);
        this->addNode(rightNode);
    }

    ~PipelineNode() override;

    void addNode(Node *node);

    const std::vector<Node *> &getNodes() const {
        return this->nodes;
    }

    void setBaseIndex(unsigned int index) {
        this->baseIndex = index;
    }

    unsigned int getBaseIndex() const {
        return this->baseIndex;
    }

    bool isLastPipe() const {
        return !this->nodes.back()->is(NodeKind::Cmd);
    }

    void dump(NodeDumper &dumper) const override;

private:
    void addNodeImpl(Node *node);
};

class WithNode : public Node {
private:
    Node *exprNode;

    std::vector<Node *> redirNodes;

    unsigned int baseIndex{0};

public:
    WithNode(Node *exprNode, RedirNode *redirNode) :
            Node(NodeKind::With, exprNode->getToken()), exprNode(exprNode) {
        this->addRedirNode(redirNode);
    }

    ~WithNode() override;

    Node *getExprNode() const {
        return this->exprNode;
    }

    void addRedirNode(RedirNode *node) {
        this->redirNodes.push_back(node);
        this->updateToken(node->getToken());
    }

    const std::vector<Node *> &getRedirNodes() const {
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

class ForkNode : public Node {
private:
    ForkKind opKind;
    Node *exprNode;

    ForkNode(Token token, ForkKind kind, Node *exprNode) :
            Node(NodeKind::Fork, token), opKind(kind), exprNode(exprNode) { }

public:
    static ForkNode *newCmdSubstitution(unsigned int pos, Node *exprNode, Token token, bool strExpr) {
        auto *node = new ForkNode({pos, 1}, strExpr ?  ForkKind::STR : ForkKind::ARRAY, exprNode);
        node->updateToken(token);
        return node;
    }

    static ForkNode *newProcSubstitution(unsigned int pos, Node *exprNode, Token token, bool inPipe) {
        auto *node = new ForkNode({pos, 1}, inPipe ?  ForkKind::IN_PIPE : ForkKind::OUT_PIPE, exprNode);
        node->updateToken(token);
        return node;
    }

    static ForkNode *newBackground(Node *exprNode, Token token, bool disown) {
        auto *node = new ForkNode(exprNode->getToken(), disown ? ForkKind::DISOWN : ForkKind::JOB, exprNode);
        node->updateToken(token);
        return node;
    }

    static ForkNode *newCoproc(Token token, Node *exprNode) {
        auto *node = new ForkNode(token, ForkKind::COPROC, exprNode);
        node->updateToken(exprNode->getToken());
        return node;
    }

    ~ForkNode() override;

    ForkKind getOpKind() const {
        return this->opKind;
    }

    Node *getExprNode() const {
        return this->exprNode;
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

class AssertNode : public Node {
private:
    Node *condNode;

    Node *messageNode;

public:
    AssertNode(unsigned int pos, Node *condNode, Node *messageNode) :
            Node(NodeKind::Assert, {pos, 1}), condNode(condNode), messageNode(messageNode) {
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

class BlockNode : public Node {
private:
    std::vector<Node *> nodes;
    unsigned int baseIndex{0};
    unsigned int varSize{0};
    unsigned int maxVarSize{0};

public:
    explicit BlockNode(unsigned int startPos) :
            Node(NodeKind::Block, {startPos, 1}) { }

    ~BlockNode() override;

    void addNode(Node *node);

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

    /**
     * this node and exprNode must be typed.
     * this node must not be bottom type.
     */
    void addReturnNodeToLast(const SymbolTable &symbolTable, Node *exprNode);

    void dump(NodeDumper &dumper) const override;
};

class TypeAliasNode : public Node {
private:
    std::string alias;
    TypeNode *targetTypeNode;

public:
    TypeAliasNode(unsigned int startPos, std::string &&alias, TypeNode *targetTypeNode) :
            Node(NodeKind::TypeAlias, {startPos, 0}), alias(std::move(alias)), targetTypeNode(targetTypeNode) {
        this->updateToken(targetTypeNode->getToken());
    }

    ~TypeAliasNode() override;

    const std::string &getAlias() const {
        return this->alias;
    }

    TypeNode *getTargetTypeNode() const {
        return this->targetTypeNode;
    }

    void dump(NodeDumper &dumper) const override;
};

/**
 * indicating for, while, do-while statement
 */
class LoopNode : public Node {
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
     * condNide may be null.
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

class IfNode : public Node {
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

class ArmNode : public Node {
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
    explicit ArmNode(Node *patternNode) : Node(NodeKind::Arm, patternNode->getToken()) {
        this->addPatternNode(patternNode);
    }

    explicit ArmNode(unsigned int pos) : Node(NodeKind::Arm, {pos, 1}) {}

    ~ArmNode() override;

    void setActionNode(Node *node) {
        this->actionNode = node;
        this->updateToken(this->actionNode->getToken());
    }

    void addPatternNode(Node *node);

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

class CaseNode : public Node {
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
    CaseNode(unsigned int pos, Node *exprNode) : Node(NodeKind::Case, {pos, 1}), exprNode(exprNode) {}

    ~CaseNode() override;

    Node *getExprNode() const {
        return this->exprNode;
    }

    void addArmNode(ArmNode *armNode);

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

class JumpNode : public Node {
public:
    enum OpKind : unsigned int {
        BREAK,
        CONTINUE,
        THROW,
        RETURN,
    };

private:
    OpKind opKind;
    Node *exprNode;
    bool leavingBlock{false};

    JumpNode(Token token, OpKind kind, Node *exprNode);

public:
    static JumpNode *newBreak(Token token, Node *exprNode) {
        return new JumpNode(token, BREAK, exprNode);
    }

    static JumpNode *newContinue(Token token) {
        return new JumpNode(token, CONTINUE, nullptr);
    }

    /**
     *
     * @param token
     * @param exprNode
     * not null
     * @return
     */
    static JumpNode *newThrow(Token token, Node *exprNode) {
        return new JumpNode(token, THROW, exprNode);
    }

    /**
     *
     * @param token
     * @param exprNode
     * may be null
     * @return
     */
    static JumpNode *newReturn(Token token, Node *exprNode) {
        return new JumpNode(token, RETURN, exprNode);
    }

    ~JumpNode() override;


    OpKind getOpKind() const {
        return this->opKind;
    }

    Node *getExprNode() const {
        return this->exprNode;
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

class CatchNode : public Node {
private:
    std::string exceptionName;
    TypeNode *typeNode;

    unsigned int varIndex{0};

    BlockNode *blockNode;

public:
    CatchNode(unsigned int startPos, std::string &&exceptionName,
              TypeNode *typeNode, BlockNode *blockNode) :
            Node(NodeKind::Catch, {startPos, 0}), exceptionName(std::move(exceptionName)),
            typeNode(typeNode != nullptr ? typeNode : newAnyTypeNode()), blockNode(blockNode) {
        this->updateToken(blockNode->getToken());
    }

    ~CatchNode() override;

    const std::string &getExceptionName() const {
        return this->exceptionName;
    }

    TypeNode *getTypeNode() const {
        return this->typeNode;
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

class TryNode : public Node {
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
            Node(NodeKind::Try, {startPos, 0}), exprNode(blockNode) {
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

class VarDeclNode : public Node {
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
class AssignNode : public Node {
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
            Node(NodeKind::Assign, leftNode->getToken()),
            leftNode(leftNode), rightNode(rightNode) {
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

class ElementSelfAssignNode : public Node {
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

class FunctionNode : public Node {
private:
    std::string funcName;

    /**
     * for parameter definition.
     */
    std::vector<VarNode *> paramNodes;

    /**
     * type token of each parameter
     */
    std::vector<TypeNode *> paramTypeNodes;

    TypeNode *returnTypeNode{nullptr};

    BlockNode *blockNode{nullptr};

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
            Node(NodeKind::Function, {startPos, 0}), funcName(std::move(funcName)) { }

    ~FunctionNode() override;

    const std::string &getFuncName() const {
        return this->funcName;
    }

    void addParamNode(VarNode *node, TypeNode *paramType);

    const std::vector<VarNode *> &getParamNodes() const {
        return this->paramNodes;
    }

    const std::vector<TypeNode *> &getParamTypeNodes() const {
        return this->paramTypeNodes;
    }

    void setReturnTypeToken(TypeNode *typeToken) {
        this->returnTypeNode = typeToken;
        this->updateToken(typeToken->getToken());
    }

    TypeNode *getReturnTypeToken() {
        return this->returnTypeNode;
    }

    void setBlockNode(BlockNode *blockNode) {
        this->blockNode = blockNode;
        this->updateToken(blockNode->getToken());
    }

    /**
     * return null before call setBlockNode()
     */
    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void setMaxVarNum(unsigned int maxVarNum) {
        this->maxVarNum = maxVarNum;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void setVarIndex(unsigned int varIndex) {
        this->varIndex = varIndex;
    }

    unsigned int getVarIndex() const {
        return this->varIndex;
    }

    void setFuncType(FunctionType *funcType) {
        this->funcType = funcType;
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

class InterfaceNode : public Node {
private:
    std::string interfaceName;

    std::vector<FunctionNode *> methodDeclNodes;
    std::vector<VarDeclNode *> fieldDeclNodes;
    std::vector<TypeNode *> fieldTypeNodes;

public:
    InterfaceNode(unsigned int startPos, std::string &&interfaceName) :
            Node(NodeKind::Interface, {startPos, 0}), interfaceName(std::move(interfaceName)) { }

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

class UserDefinedCmdNode : public Node {
private:
    std::string cmdName;

    unsigned int udcIndex{0};
    BlockNode *blockNode;

    unsigned int maxVarNum{0};

public:
    UserDefinedCmdNode(unsigned int startPos, std::string &&commandName, BlockNode *blockNode) :
            Node(NodeKind::UserDefinedCmd, {startPos, 0}),
            cmdName(std::move(commandName)), blockNode(blockNode) {
        this->updateToken(blockNode->getToken());
    }

    ~UserDefinedCmdNode() override;

    const std::string &getCmdName() const {
        return this->cmdName;
    }

    unsigned int getUdcIndex() const {
        return this->udcIndex;
    }

    void setUdcIndex(unsigned int index) {
        this->udcIndex = index;
    }

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void setMaxVarNum(unsigned int maxVarNum) {
        this->maxVarNum = maxVarNum;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void dump(NodeDumper &dumper) const override;
};

class SourceNode : public Node {
private:
    StringNode *pathNode;

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
    SourceNode(unsigned int startPos, StringNode *pathNode, bool optional) :
            Node(NodeKind::Source, {startPos, 1}), pathNode(pathNode), optional(optional) {
        this->updateToken(pathNode->getToken());
    }

    ~SourceNode() override;

    StringNode *getPathNode() {
        return this->pathNode;
    }

    const std::string &getPathStr() const {
        return this->pathNode->getValue();
    }

    void setName(Token token, std::string &&name) {
        this->updateToken(token);
        this->name = std::move(name);
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

    void setModIndex(unsigned int index) {
        this->modIndex = index;
    }

    unsigned int getModIndex() const {
        return this->modIndex;
    }

    void setIndex(unsigned int index) {
        this->index = index;
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

class EmptyNode : public Node {
public:
    EmptyNode() : EmptyNode({0, 0}) { }
    explicit EmptyNode(Token token) : Node(NodeKind::Empty, token) { }
    ~EmptyNode() override = default;

    void dump(NodeDumper &dumper) const override;
};

// helper function for node creation

const char *resolveUnaryOpName(TokenKind op);

const char *resolveBinaryOpName(TokenKind op);

TokenKind resolveAssignOp(TokenKind op);

LoopNode *createForInNode(unsigned int startPos, std::string &&varName, Node *exprNode, BlockNode *blockNode);

Node *createAssignNode(Node *leftNode, TokenKind op, Token token, Node *rightNode);

inline Node *createSuffixNode(Node *leftNode, TokenKind op, Token token) {
    return createAssignNode(leftNode, op, token, NumberNode::newInt32(token, 1));
}

template <typename T>
struct type2info {};

#define GEN_TO_INFO(T) template <> struct type2info<T ## Node> { static constexpr auto value = NodeKind::T; };

EACH_NODE_KIND(GEN_TO_INFO)

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
    return static_cast<const T *>(findInnerNode(type2info<T>::value, node));
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

    void dump(const char *fieldName, const std::vector<Node *> &nodes) {
        this->dumpNodes(fieldName, nodes.data(), nodes.data() + nodes.size());
    }

    template <typename T, enable_when<std::is_convertible<T *, Node *>::value> = nullptr>
    void dump(const char *fieldName, const std::vector<T *> &nodes) {
        this->dumpNodes(fieldName, reinterpret_cast<Node *const*>(nodes.data()),
                        reinterpret_cast<Node *const*>(nodes.data() + nodes.size()));
    }

    /**
     * dump node with indent
     */
    void dump(const char *fieldName, const Node &node);

    void dump(const char *fieldName, const DSType &type);

    void dump(const char *fieldName, TokenKind kind);

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

    void dumpNodes(const char *fieldName, Node* const* begin, Node* const* end);

    void writeName(const char *fieldName);
};

} // namespace ydsh

#endif //YDSH_NODE_H
