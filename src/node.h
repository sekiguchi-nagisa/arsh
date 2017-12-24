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

#ifndef YDSH_NODE_H
#define YDSH_NODE_H

#include <utility>
#include <memory>
#include <type_traits>
#include <list>

#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "token_kind.h"
#include "lexer.h"
#include "type.h"
#include "handle.h"
#include "regex_wrapper.h"

namespace ydsh {

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
    OP(MethodCall) \
    OP(New) \
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
    OP(Jump) \
    OP(Catch) \
    OP(Try) \
    OP(VarDecl) \
    OP(Assign) \
    OP(ElementSelfAssign) \
    OP(Function) \
    OP(Interface) \
    OP(UserDefinedCmd) \
    OP(Empty) \
    OP(Root)

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
    DSType *type;

    Node(NodeKind kind, Token token) : nodeKind(kind), token(token), type() { }

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

    void setType(DSType &type) {
        this->type = &type;
    }

    /**
     * must not call it before type checking
     */
    DSType &getType() const {
        return *this->type;
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
    OP(DBusIface) \
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

class DBusIfaceTypeNode : public TypeNode {
private:
    /**
     * must be valid interface name.
     * ex. org.freedesktop.NetworkManager
     */
    std::string name;

public:
    DBusIfaceTypeNode(Token token, std::string &&name) :
            TypeNode(TypeNode::DBusIface, token), name(std::move(name)) { }

    ~DBusIfaceTypeNode() override = default;

    const std::string &getTokenText() const {
        return this->name;
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

TypeNode *newAnyTypeNode();

TypeNode *newVoidTypeNode();



// expression definition

#define EACH_NUMBER_NODE_KIND(OP) \
    OP(Byte) \
    OP(Int16) \
    OP(Uint16) \
    OP(Int32) \
    OP(Uint32) \
    OP(Int64) \
    OP(Uint64) \
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
    static NumberNode *newByte(Token token, unsigned char value) {
        auto *node = new  NumberNode(token, Byte);
        node->intValue = value;
        return node;
    }

    static NumberNode *newInt16(Token token, short value) {
        auto *node = new  NumberNode(token, Int16);
        node->intValue = value;
        return node;
    }

    static NumberNode *newUint16(Token token, unsigned short value) {
        auto *node = new  NumberNode(token, Uint16);
        node->intValue = value;
        return node;
    }

    static NumberNode *newInt32(Token token, int value) {
        auto *node = new  NumberNode(token, Int32);
        node->intValue = value;
        return node;
    }

    static NumberNode *newUint32(Token token, unsigned int value) {
        auto *node = new  NumberNode(token, Uint32);
        node->intValue = value;
        return node;
    }

    static NumberNode *newInt64(Token token, long value) {
        auto *node = new  NumberNode(token, Int64);
        node->longValue = value;
        return node;
    }

    static NumberNode *newUint64(Token token, unsigned long value) {
        auto *node = new  NumberNode(token, Uint64);
        node->longValue = value;
        return node;
    }

    static NumberNode *newFloat(Token token, double value) {
        auto *node = new  NumberNode(token, Float);
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
        OBJECT_PATH,
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

    bool isObjectPath() const {
        return this->getKind() == OBJECT_PATH;
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
        return std::move(re);
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
    unsigned int index;

    FieldAttributes attribute;

    AssignableNode(NodeKind kind, Token token) :
            Node(kind, token), index(0) { }

public:
    ~AssignableNode() override = default;

    void setAttribute(FieldHandle *handle) {
        this->index = handle->getFieldIndex();
        this->attribute = handle->attr();
    }

    FieldAttributes attr() const {
        return this->attribute;
    }

    unsigned int getIndex() const {
        return this->index;
    }

    void dump(NodeDumper &dumper) const override;
};

inline bool isAssignable(const Node &node) {
    return node.getNodeKind() == NodeKind::Var || node.getNodeKind() == NodeKind::Access;
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
    AdditionalOp additionalOp;

public:
    AccessNode(Node *recvNode, VarNode *nameNode) :
            AssignableNode(NodeKind::Access, recvNode->getToken()),
            recvNode(recvNode), nameNode(nameNode), additionalOp(NOP) { }

    ~AccessNode() override;

    Node *getRecvNode() const {
        return this->recvNode;
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
 * for function object apply
 */
class ApplyNode : public Node {
private:
    Node *exprNode;
    std::vector<Node *> argNodes;

public:
    ApplyNode(Node *exprNode, std::vector<Node *> &&argNodes);

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

    void dump(NodeDumper &dumper) const override;
};

class MethodCallNode : public Node {
private:
    Node *recvNode;
    std::string methodName;
    std::vector<Node *> argNodes;

    MethodHandle *handle;

    flag8_set_t attributeSet;

public:
    MethodCallNode(Node *recvNode, std::string &&methodName) :
            MethodCallNode(recvNode, std::move(methodName), std::vector<Node *>()) { }

    MethodCallNode(Node *recvNode, std::string &&methodName, std::vector<Node *> &&argNodes);

    ~MethodCallNode() override;

    void setRecvNode(Node *node) {
        this->recvNode = node;
    }

    Node *getRecvNode() const {
        return this->recvNode;
    }

    void setMethodName(std::string &&methodName) {
        this->methodName = std::move(methodName);
    }

    const std::string &getMethodName() const {
        return this->methodName;
    }

    const std::vector<Node *> &getArgNodes() const {
        return this->argNodes;
    }

    std::vector<Node *> &refArgNodes() {
        return this->argNodes;
    }

    void setAttribute(flag8_t attribute) {
        setFlag(this->attributeSet, attribute);
    }

    bool hasAttribute(flag8_t attribute) const {
        return hasFlag(this->attributeSet, attribute);
    }

    void setHandle(MethodHandle *handle) {
        this->handle = handle;
    }

    MethodHandle *getHandle() const {
        return this->handle;
    }

    void dump(NodeDumper &dumper) const override;

    static constexpr flag8_t INDEX = 1 << 0;
    static constexpr flag8_t ICALL = 1 << 1;
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
 * for unary operator call
 */
class UnaryOpNode : public Node {
private:
    TokenKind op;

    /**
     * after call this->createApplyNode(), will be null.
     */
    Node *exprNode;

    /**
     * before call this->createApplyNode(), it is null.
     */
    MethodCallNode *methodCallNode;

public:
    UnaryOpNode(unsigned int startPos, TokenKind op, Node *exprNode) :
            Node(NodeKind::UnaryOp, {startPos, 0}), op(op), exprNode(exprNode), methodCallNode(nullptr) {
        this->updateToken(exprNode->getToken());
    }

    ~UnaryOpNode() override;

    Node *getExprNode() const {
        return this->exprNode;
    }

    Node *&refExprNode() {
        return this->exprNode;
    }

    bool isUnwrapOp() const {
        return this->op == UNWRAP;
    }

    /**
     * create ApplyNode and set to this->applyNode.
     * exprNode will be null.
     */
    MethodCallNode *createApplyNode();

    /**
     * return null, before call this->createApplyNode().
     */
    MethodCallNode *getApplyNode() const {
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

    /**
     * initial value is null
     */
    Node *optNode;

public:
    BinaryOpNode(Node *leftNode, TokenKind op, Node *rightNode) :
            Node(NodeKind::BinaryOp, leftNode->getToken()),
            leftNode(leftNode), rightNode(rightNode), op(op), optNode(nullptr) {
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

    /**
     * return null, before call toMethodCall().
     */
    Node *getOptNode() const {
        return this->optNode;
    }

    void setOptNode(Node *node) {
        this->optNode = node;
    }

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

    unsigned int redirCount;

    bool inPipe;

    bool inLastPipe;

public:
    explicit CmdNode(StringNode *nameNode) :
            Node(NodeKind::Cmd, nameNode->getToken()),
            nameNode(nameNode), redirCount(0), inPipe(false), inLastPipe(false) { }

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

    void setInLastPipe(bool in) {
        this->inLastPipe = in;
    }

    bool getInLastPipe() const {
        return this->inLastPipe;
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

    void dump(NodeDumper &dumper) const override;

private:
    void addNodeImpl(Node *node);
};

class WithNode : public Node {
private:
    Node *exprNode;

    std::vector<Node *> redirNodes;

    unsigned int baseIndex;

public:
    WithNode(Node *exprNode, RedirNode *redirNode) :
            Node(NodeKind::With, exprNode->getToken()), exprNode(exprNode), baseIndex(0) {
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
public:
    enum OpKind : unsigned char {
        SUB_STR,    // "$(echo)"
        SUB_ARRAY,  // $(echo)
        BG,         // echo &
        DISOWN,     // echo &!
        COPROC,     // coproc echo
    };

private:
    OpKind opKind;
    Node *exprNode;

    ForkNode(Token token, OpKind kind, Node *exprNode) :
            Node(NodeKind::Fork, token), opKind(kind), exprNode(exprNode) { }

public:
    static ForkNode *newSubsitution(unsigned int pos, Node *exprNode, Token token, bool strExpr) {
        auto *node = new ForkNode({pos, 1}, strExpr ? SUB_STR : SUB_ARRAY, exprNode);
        node->updateToken(token);
        return node;
    }

    static ForkNode *newBackground(Node *exprNode, Token token, bool disown) {
        auto *node = new ForkNode(exprNode->getToken(), disown ? DISOWN : BG, exprNode);
        node->updateToken(token);
        return node;
    }

    static ForkNode *newCoproc(Token token, Node *exprNode) {
        auto *node = new ForkNode(token, COPROC, exprNode);
        node->updateToken(exprNode->getToken());
        return node;
    }

    ~ForkNode();

    OpKind getOpKind() const {
        return this->opKind;
    }

    Node *getExprNode() const {
        return this->exprNode;
    }

    bool isJob() const {
        switch(this->opKind) {
        case BG:
        case COPROC:
        case DISOWN:
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
    unsigned int baseIndex;
    unsigned int varSize;
    unsigned int maxVarSize;

public:
    explicit BlockNode(unsigned int startPos) :
            Node(NodeKind::Block, {startPos, 1}), baseIndex(0), varSize(0), maxVarSize(0) { }

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
    void addReturnNodeToLast(TypePool &pool, Node *exprNode);

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

    unsigned int varIndex;

    BlockNode *blockNode;

public:
    CatchNode(unsigned int startPos, std::string &&exceptionName,
              TypeNode *typeNode, BlockNode *blockNode) :
            Node(NodeKind::Catch, {startPos, 0}), exceptionName(std::move(exceptionName)),
            typeNode(typeNode != nullptr ? typeNode : newAnyTypeNode()), varIndex(0), blockNode(blockNode) {
        this->updateToken(blockNode->getToken());
    }

    ~CatchNode() override;

    const std::string &getExceptionName() const {
        return this->exceptionName;
    }

    TypeNode *getTypeNode() const {
        return this->typeNode;
    }

    void setAttribute(FieldHandle *handle);

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
    BlockNode *blockNode;

    /**
     * may be empty
     */
    std::vector<CatchNode *> catchNodes;

    /**
     * may be null
     */
    BlockNode *finallyNode;

public:
    TryNode(unsigned int startPos, BlockNode *blockNode) :
            Node(NodeKind::Try, {startPos, 0}), blockNode(blockNode), finallyNode() {
        this->updateToken(blockNode->getToken());
    }

    ~TryNode() override;

    BlockNode *getBlockNode() const {
        return this->blockNode;
    }

    void addCatchNode(CatchNode *catchNode);

    const std::vector<CatchNode *> &getCatchNodes() const {
        return this->catchNodes;
    }

    void addFinallyNode(BlockNode *finallyNode);

    /**
     * if has no finally block, return null
     */
    BlockNode *getFinallyNode() {
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
    bool global;
    Kind kind;
    unsigned int varIndex;

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

    void setAttribute(FieldHandle *handle);

    bool isGlobal() const {
        return this->global;
    }

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
    flag8_set_t attributeSet;

public:
    static constexpr flag8_t SELF_ASSIGN  = 1 << 0;
    static constexpr flag8_t FIELD_ASSIGN = 1 << 1;

    AssignNode(Node *leftNode, Node *rightNode, bool selfAssign = false) :
            Node(NodeKind::Assign, leftNode->getToken()),
            leftNode(leftNode), rightNode(rightNode), attributeSet(0) {
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
    MethodCallNode *getterNode;

    /**
     * receiver and argument are dummy node
     */
    MethodCallNode *setterNode;

    /**
     * before type checking, rightNode is BinaryOpNode.
     * after type checking, rightNode may be CastNode.
     * if rightNode is BinaryOpNode, left node is dummy node.
     */
    Node *rightNode;

public:
    ElementSelfAssignNode(MethodCallNode *leftNode, BinaryOpNode *binaryNode);
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

    MethodCallNode *getGetterNode() const {
        return this->getterNode;
    }

    MethodCallNode *getSetterNode() const {
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

class CallableNode : public Node {
protected:
    SourceInfoPtr srcInfoPtr;

    /**
     * if RootNode, name is empty.
     */
    std::string name;

    CallableNode(NodeKind kind, unsigned int startPos, SourceInfoPtr srcInfoPtr, std::string &&name) :
            Node(kind, {startPos, 0}), srcInfoPtr(std::move(srcInfoPtr)), name(std::move(name)) { }

    explicit CallableNode(NodeKind kind) : Node(kind, {0, 0}), srcInfoPtr(nullptr) { }

public:
    ~CallableNode() override = default;

    const SourceInfoPtr &getSourceInfoPtr() const {
        return this->srcInfoPtr;
    }

    void setSourceInfoPtr(const SourceInfoPtr &srcInfoPtr) {
        this->srcInfoPtr = srcInfoPtr;
    }

    const char *getSourceName() const {
        return this->srcInfoPtr->getSourceName().c_str();
    }

    const std::string &getName() const {
        return this->name;
    }
};

class FunctionNode : public CallableNode {
private:
    /**
     * for parameter definition.
     */
    std::vector<VarNode *> paramNodes;

    /**
     * type token of each parameter
     */
    std::vector<TypeNode *> paramTypeNodes;

    TypeNode *returnTypeNode;

    BlockNode *blockNode;

    /**
     * maximum number of local variable in function
     */
    unsigned int maxVarNum;

    /**
     * global variable table index of this function
     */
    unsigned int varIndex;

public:
    FunctionNode(unsigned int startPos, const SourceInfoPtr &srcInfoPtr,
                 std::string &&funcName) :
            CallableNode(NodeKind::Function, startPos, srcInfoPtr, std::move(funcName)),
            returnTypeNode(), blockNode(), maxVarNum(0), varIndex(0) { }

    ~FunctionNode() override;

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

class UserDefinedCmdNode : public CallableNode {
private:
    unsigned int udcIndex;
    BlockNode *blockNode;

    unsigned int maxVarNum;

public:
    UserDefinedCmdNode(unsigned int startPos, const SourceInfoPtr &srcInfoPtr,
                       std::string &&commandName, BlockNode *blockNode) :
            CallableNode(NodeKind::UserDefinedCmd, startPos, srcInfoPtr, std::move(commandName)),
            udcIndex(0), blockNode(blockNode), maxVarNum(0) {
        this->updateToken(blockNode->getToken());
    }

    ~UserDefinedCmdNode() override;

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

class EmptyNode : public Node {
public:
    EmptyNode() : EmptyNode({0, 0}) { }
    explicit EmptyNode(Token token) : Node(NodeKind::Empty, token) { }
    ~EmptyNode() override = default;

    void dump(NodeDumper &dumper) const override;
};

class RootNode : public CallableNode {
private:
    std::vector<Node *> nodes;

    /**
     * max number of local variable.
     */
    unsigned int maxVarNum{0};

    /**
     * max number of global variable.
     */
    unsigned int maxGVarNum{0};

public:
    RootNode() : CallableNode(NodeKind::Root) { }

    ~RootNode() override;

    void addNode(Node *node);

    const std::vector<Node *> &getNodes() const {
        return this->nodes;
    }

    std::vector<Node *> &refNodes() {
        return this->nodes;
    }

    void setMaxVarNum(unsigned int maxVarNum) {
        this->maxVarNum = maxVarNum;
    }

    unsigned int getMaxVarNum() const {
        return this->maxVarNum;
    }

    void setMaxGVarNum(unsigned int maxGVarNum) {
        this->maxGVarNum = maxGVarNum;
    }

    unsigned int getMaxGVarNum() const {
        return this->maxGVarNum;
    }

    void dump(NodeDumper &dumper) const override;
};

// helper function for node creation

const char *resolveUnaryOpName(TokenKind op);

const char *resolveBinaryOpName(TokenKind op);

TokenKind resolveAssignOp(TokenKind op);

LoopNode *createForInNode(unsigned int startPos, std::string &&varName, Node *exprNode, BlockNode *blockNode);

Node *createAssignNode(Node *leftNode, TokenKind op, Node *rightNode);

inline Node *createSuffixNode(Node *leftNode, TokenKind op, Token token) {
    return createAssignNode(leftNode, op, NumberNode::newByte(token, 1));
}

Node *createIndexNode(Node *recvNode, Node *indexNode);

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
    virtual void visitMethodCallNode(MethodCallNode &node) = 0;
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
    virtual void visitJumpNode(JumpNode &node) = 0;
    virtual void visitCatchNode(CatchNode &node) = 0;
    virtual void visitTryNode(TryNode &node) = 0;
    virtual void visitVarDeclNode(VarDeclNode &node) = 0;
    virtual void visitAssignNode(AssignNode &node) = 0;
    virtual void visitElementSelfAssignNode(ElementSelfAssignNode &node) = 0;
    virtual void visitFunctionNode(FunctionNode &node) = 0;
    virtual void visitInterfaceNode(InterfaceNode &node) = 0;
    virtual void visitUserDefinedCmdNode(UserDefinedCmdNode &node) = 0;
    virtual void visitEmptyNode(EmptyNode &node) = 0;
    virtual void visitRootNode(RootNode &node) = 0;
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
    void visitMethodCallNode(MethodCallNode &node) override { this->visitDefault(node); }
    void visitNewNode(NewNode &node) override { this->visitDefault(node); }
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
    void visitJumpNode(JumpNode &node) override { this->visitDefault(node); }
    void visitCatchNode(CatchNode &node) override { this->visitDefault(node); }
    void visitTryNode(TryNode &node) override { this->visitDefault(node); }
    void visitVarDeclNode(VarDeclNode &node) override { this->visitDefault(node); }
    void visitAssignNode(AssignNode &node) override { this->visitDefault(node); }
    void visitElementSelfAssignNode(ElementSelfAssignNode &node) override { this->visitDefault(node); }
    void visitFunctionNode(FunctionNode &node) override { this->visitDefault(node); }
    void visitInterfaceNode(InterfaceNode &node) override { this->visitDefault(node); }
    void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override { this->visitDefault(node); }
    void visitEmptyNode(EmptyNode &node) override { this->visitDefault(node); }
    void visitRootNode(RootNode &node) override { this->visitDefault(node); }
};

class NodeDumper {
private:
    FILE *fp;
    TypePool &pool;

    unsigned int indentLevel;

public:
    NodeDumper(FILE *fp, TypePool &pool) : fp(fp), pool(pool), indentLevel(0) { }

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

    template <typename T>
    using convertible_t = typename std::enable_if<std::is_convertible<T, Node *>::value, T>::type;

    template <typename T, typename = convertible_t<T *>>
    void dump(const char *fieldName, const std::vector<T *> &nodes) {
        this->dumpNodes(fieldName, reinterpret_cast<Node *const*>(nodes.data()),
                        reinterpret_cast<Node *const*>(nodes.data() + nodes.size()));
    }

    void dump(const char *fieldName, const std::list<Node *> &nodes);

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

    /**
     * entry point
     */
    void operator()(const RootNode &rootNode);

    static void dump(FILE *fp, TypePool &pool, const RootNode &rootNode);

private:
    void enterIndent() {
        this->indentLevel++;
    }

    void leaveIndent() {
        this->indentLevel--;
    }

    void indent();

    void newline() {
        fputc('\n', this->fp);
    }

    void dumpNodeHeader(const Node &node, bool inArray = false);

    void dumpNodes(const char *fieldName, Node* const* begin, Node* const* end);

    void writeName(const char *fieldName);
};

} // namespace ydsh

#endif //YDSH_NODE_H
