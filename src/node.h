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
enum RedirectOP : unsigned char;

struct NodeVisitor;
class NodeDumper;

class Node {
protected:
    Token token;

    /**
     * initial value is null.
     */
    DSType *type;

public:
    NON_COPYABLE(Node);

    explicit Node(Token token) :
            token(token), type() { }

    virtual ~Node() = default;

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
    virtual void accept(NodeVisitor &visitor) = 0;
};

// type definition
/**
 * represent for parsed type.
 */
class TypeNode : public Node {
public:
    explicit TypeNode(Token token) : Node(token) { }

    virtual ~TypeNode() = default;

    virtual void dump(NodeDumper &dumper) const override = 0;
    virtual void accept(NodeVisitor &visitor) override = 0;
};

class BaseTypeNode : public TypeNode {
private:
    std::string typeName;

public:
    BaseTypeNode(Token token, std::string &&typeName) :
            TypeNode(token), typeName(std::move(typeName)) { }

    ~BaseTypeNode() = default;

    const std::string &getTokenText() const {
        return this->typeName;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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
            TypeNode(templateTypeNode->getToken()),
            templateTypeNode(templateTypeNode), elementTypeNodes() { }

    ~ReifiedTypeNode();

    void addElementTypeNode(TypeNode *typeNode);

    BaseTypeNode *getTemplate() const {
        return this->templateTypeNode;
    }

    const std::vector<TypeNode *> &getElementTypeNodes() const {
        return this->elementTypeNodes;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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
            TypeNode({startPos, 0}),
            returnTypeNode(returnTypeNode), paramTypeNodes() { }

    ~FuncTypeNode();

    void addParamTypeNode(TypeNode *typeNode);

    const std::vector<TypeNode *> &getParamTypeNodes() const {
        return this->paramTypeNodes;
    }

    TypeNode *getReturnTypeNode() const {
        return this->returnTypeNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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
            TypeNode(token), name(std::move(name)) { }

    ~DBusIfaceTypeNode() = default;

    const std::string &getTokenText() const {
        return this->name;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

/**
 * for multiple return type
 */
class ReturnTypeNode : public TypeNode {
private:
    std::vector<TypeNode *> typeNodes;

public:
    explicit ReturnTypeNode(TypeNode *typeNode);
    ~ReturnTypeNode();

    void addTypeNode(TypeNode *typeNode);

    const std::vector<TypeNode *> &getTypeNodes() const {
        return this->typeNodes;
    }

    bool hasMultiReturn() const {
        return this->typeNodes.size() > 1;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class TypeOfNode : public TypeNode {
private:
    Node *exprNode;

public:
    TypeOfNode(unsigned int startPos, Node *exprNode);
    ~TypeOfNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

TypeNode *newAnyTypeNode();

TypeNode *newVoidTypeNode();



// expression definition

class IntValueNode : public Node {
public:
    enum IntKind{
        BYTE,
        INT16,
        UINT16,
        INT32,
        UINT32,
    };

private:
    IntKind kind;
    int value;

    IntValueNode(Token token, IntKind kind, int value) :
            Node(token), kind(kind), value(value) { }

public:
    static IntValueNode *newByte(Token token, unsigned char value) {
        return new IntValueNode(token, BYTE, value);
    }

    static IntValueNode *newInt16(Token token, short value) {
        return new IntValueNode(token, INT16, value);
    }

    static IntValueNode *newUint16(Token token, unsigned short value) {
        return new IntValueNode(token, UINT16, value);
    }

    static IntValueNode *newInt32(Token token, int value) {
        return new IntValueNode(token, INT32, value);
    }

    static IntValueNode *newUint32(Token token, unsigned int value) {
        return new IntValueNode(token, UINT32, value);
    }

    ~IntValueNode() = default;

    IntKind getKind() const {
        return this->kind;
    }

    int getValue() const {
        return this->value;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class LongValueNode : public Node {
private:
    long value;
    bool unsignedValue;

    LongValueNode(Token token, long value, bool unsignedValue) :
            Node(token), value(value), unsignedValue(unsignedValue) { }

public:
    static LongValueNode *newInt64(Token token, long value) {
        return new LongValueNode(token, value, false);
    }

    static LongValueNode *newUint64(Token token, unsigned long value) {
        return new LongValueNode(token, value, true);
    }

    ~LongValueNode() = default;

    /**
     * if true, treat as unsigned int 64.
     */
    bool isUnsignedValue() const {
        return this->unsignedValue;
    }

    long getValue() const {
        return this->value;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class FloatValueNode : public Node {
private:
    double value;

public:
    FloatValueNode(Token token, double value) :
            Node(token), value(value) { }

    double getValue() const {
        return this->value;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class StringValueNode : public Node {
protected:
    std::string value;
    bool asObjPath;

public:
    /**
     * used for CommandNode. lineNum is always 0.
     */
    explicit StringValueNode(std::string &&value) :
            Node({0, 0}), value(std::move(value)), asObjPath(false) { }

    StringValueNode(Token token, std::string &&value, bool asObjPath = false) :
            Node(token), value(std::move(value)), asObjPath(asObjPath) { }

    ~StringValueNode() = default;

    const std::string &getValue() const {
        return this->value;
    }

    bool isObjectPath() const {
        return this->asObjPath;
    }

    void dump(NodeDumper &dumper) const override;
    virtual void accept(NodeVisitor &visitor) override;

    static std::string extract(StringValueNode &&node) {
        return std::move(node.value);
    }
};

class StringExprNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    explicit StringExprNode(unsigned int startPos) : Node({startPos, 1}), nodes() { }

    ~StringExprNode();

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    void setExprNode(unsigned int index, Node *exprNode) {
        this->nodes[index] = exprNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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
            Node(token), reStr(std::move(str)), re(std::move(re)) { }

    ~RegexNode() = default;

    const std::string &getReStr() const {
        return this->reStr;
    }

    PCRE extractRE() {
        return std::move(re);
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class ArrayNode : public Node {
private:
    std::vector<Node *> nodes;

public:
    ArrayNode(unsigned int startPos, Node *node);

    ~ArrayNode();

    void addExprNode(Node *node);

    const std::vector<Node *> &getExprNodes() const {
        return this->nodes;
    }

    std::vector<Node *> &refExprNodes() {
        return this->nodes;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class MapNode : public Node {
private:
    std::vector<Node *> keyNodes;
    std::vector<Node *> valueNodes;

public:
    MapNode(unsigned int startPos, Node *keyNode, Node *valueNode);

    ~MapNode();

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
    void accept(NodeVisitor &visitor) override;
};

class TupleNode : public Node {
private:
    /**
     * at least two nodes
     */
    std::vector<Node *> nodes;

public:
    TupleNode(unsigned int startPos, Node *leftNode, Node *rightNode);

    ~TupleNode();

    void addNode(Node *node);

    const std::vector<Node *> &getNodes() const {
        return this->nodes;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

/**
 * base class for VarNode, AccessNode
 */
class AssignableNode : public Node {
protected:
    unsigned int index;

    FieldAttributes attribute;

public:
    explicit AssignableNode(Token token) :
            Node(token), index(0), attribute() { }

    virtual ~AssignableNode() = default;

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

    virtual void dump(NodeDumper &dumper) const override;
};

class VarNode : public AssignableNode {
private:
    std::string varName;

public:
    VarNode(Token token, std::string &&varName) :
            AssignableNode(token), varName(std::move(varName)) { }

    ~VarNode() = default;

    const std::string &getVarName() const {
        return this->varName;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class AccessNode : public AssignableNode {
public:
    enum AdditionalOp {
        NOP,
        DUP_RECV,
    };

private:
    Node *recvNode;
    std::string fieldName;
    AdditionalOp additionalOp;

public:
    AccessNode(Node *recvNode, std::string &&fieldName) :
            AssignableNode(recvNode->getToken()),
            recvNode(recvNode), fieldName(std::move(fieldName)), additionalOp(NOP) { }

    ~AccessNode();

    Node *getRecvNode() const {
        return this->recvNode;
    }

    void setFieldName(const std::string &fieldName) {
        this->fieldName = fieldName;
    }

    const std::string &getFieldName() const {
        return this->fieldName;
    }

    void setAdditionalOp(AdditionalOp op) {
        this->additionalOp = op;
    }

    AdditionalOp getAdditionalOp() const {
        return this->additionalOp;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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

    ~ TypeOpNode();

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
    void accept(NodeVisitor &visitor) override;
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

    ~ApplyNode();

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
    void accept(NodeVisitor &visitor) override;
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

    ~MethodCallNode();

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
    void accept(NodeVisitor &visitor) override;

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

    ~NewNode();

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
    void accept(NodeVisitor &visitor) override;
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
            Node({startPos, 0}), op(op), exprNode(exprNode), methodCallNode(nullptr) {
        this->updateToken(exprNode->getToken());
    }

    ~UnaryOpNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void setExprNode(Node *exprNode) {
        this->exprNode = exprNode;
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
    void accept(NodeVisitor &visitor) override;
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
            Node(leftNode->getToken()),
            leftNode(leftNode), rightNode(rightNode), op(op), optNode(nullptr) {
        this->updateToken(rightNode->getToken());
    }

    ~BinaryOpNode();

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
    void accept(NodeVisitor &visitor) override;
};

class TernaryNode : public Node {
private:
    Node *condNode;
    Node *leftNode;
    Node *rightNode;

public:
    TernaryNode(Node *condNode, Node *leftNode, Node *rightNode) :
            Node(condNode->getToken()), condNode(condNode),
            leftNode(leftNode), rightNode(rightNode) {
        this->updateToken(this->rightNode->getToken());
    }

    ~TernaryNode();

    Node *getCondNode() {
        return this->condNode;
    }

    Node *& refCondNode() {
        return this->condNode;
    }

    Node *getLeftNode() {
        return this->leftNode;
    }

    Node *& refLeftNode() {
        return this->leftNode;
    }

    Node *getRightNode() {
        return this->rightNode;
    }

    Node *& refRightNode() {
        return this->rightNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

/**
 * for command argument
 */
class CmdArgNode : public Node {
private:
    std::vector<Node *> segmentNodes;

public:
    explicit CmdArgNode(Node *segmentNode) :
            Node(segmentNode->getToken()), segmentNodes() {
        this->addSegmentNode(segmentNode);
    }

    ~CmdArgNode();

    void addSegmentNode(Node *node);

    const std::vector<Node *> &getSegmentNodes() const {
        return this->segmentNodes;
    }
    
    void setSegmentNode(unsigned int index, Node *segmentNode) {
        this->segmentNodes[index] = segmentNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;

    /**
     * if true, ignore evaluated empty string.
     */
    bool isIgnorableEmptyString();
};

class RedirNode : public Node {
private:
    RedirectOP op;
    CmdArgNode *targetNode;

public:
    RedirNode(TokenKind kind, CmdArgNode *node);

    ~RedirNode();

    RedirectOP getRedirectOP() {
        return this->op;
    }

    CmdArgNode *getTargetNode() {
        return this->targetNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

/**
 * for tilde expansion
 * TildeNode is always first element of CmdArgNode.
 */
class TildeNode : public Node {
private:
    /**
     * starts with '/'
     */
    std::string value;

public:
    TildeNode(Token token, std::string &&value) :
            Node(token), value(std::move(value)) { }

    ~TildeNode() = default;

    const std::string &getValue() {
        return this->value;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class CmdNode : public Node {
private:
    /**
     * msy be TildeNode or StringValueNode.
     */
    Node *nameNode;

    /**
     * may be CmdArgNode, RedirNode
     */
    std::vector<Node *> argNodes;

public:
    CmdNode(Token token, std::string &&value) :
            Node(token),
            nameNode(new StringValueNode(token, std::move(value))), argNodes() { }

    explicit CmdNode(TildeNode *nameNode) :
            Node(nameNode->getToken()), nameNode(nameNode), argNodes() { }

    ~CmdNode();

    Node *getNameNode() const {
        return this->nameNode;
    }

    void addArgNode(CmdArgNode *node);

    const std::vector<Node *> &getArgNodes() const {
        return this->argNodes;
    }

    void addRedirOption(TokenKind kind, CmdArgNode *node);
    void addRedirOption(TokenKind kind, Token token);

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class PipedCmdNode : public Node {
private:
    std::vector<Node *> cmdNodes;

public:
    explicit PipedCmdNode(Node *node) :
            Node(node->getToken()), cmdNodes(1) {
        this->cmdNodes[0] = node;
    }

    ~PipedCmdNode();

    void addCmdNodes(Node *node);

    const std::vector<Node *> &getCmdNodes() const {
        return this->cmdNodes;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

/**
 * for command substitution (ex. "$(echo)", $(echo) )
 */
class SubstitutionNode : public Node {
private:
    Node *exprNode;

    /**
     * if true, this node is within string expr node
     */
    bool strExpr;

public:
    SubstitutionNode(unsigned int pos, Node *exprNode) :
            Node({pos, 1}), exprNode(exprNode), strExpr(false) { }

    ~SubstitutionNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void setStrExpr(bool strExpr) {
        this->strExpr = strExpr;
    }

    bool isStrExpr() const {
        return this->strExpr;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

// statement definition

class AssertNode : public Node {
private:
    Node *condNode;

    Node *messageNode;

public:
    AssertNode(unsigned int pos, Node *condNode, Node *messageNode) :
            Node({pos, 1}), condNode(condNode), messageNode(messageNode) {
        this->updateToken(messageNode->getToken());
    }

    ~AssertNode();

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
    void accept(NodeVisitor &visitor) override;
};

class BlockNode : public Node {
private:
    std::vector<Node *> nodes;
    unsigned int baseIndex;
    unsigned int varSize;

public:
    explicit BlockNode(unsigned int startPos) :
            Node({startPos, 1}), nodes(), baseIndex(0), varSize(0) { }

    ~BlockNode();

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

    /**
     * this node and exprNode must be typed.
     * this node must not be bottom type.
     */
    void addReturnNodeToLast(TypePool &pool, Node *exprNode);

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class JumpNode : public Node {
private:
    /**
     * if true, treat as break, otherwise, treat as continue.
     */
    bool asBreak;

    bool leavingBlock;

public:
    JumpNode(Token token, bool asBreak) :
            Node(token), asBreak(asBreak), leavingBlock(false) { }

    ~JumpNode() = default;

    bool isBreak() const {
        return this->asBreak;
    }

    void setLeavingBlock(bool leave) {
        this->leavingBlock = leave;
    }

    bool isLeavingBlock() const {
        return this->leavingBlock;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class TypeAliasNode : public Node {
private:
    std::string alias;
    TypeNode *targetTypeNode;

public:
    TypeAliasNode(unsigned int startPos, std::string &&alias, TypeNode *targetTypeNode) :
            Node({startPos, 0}), alias(std::move(alias)), targetTypeNode(targetTypeNode) {
        this->updateToken(targetTypeNode->getToken());
    }

    ~TypeAliasNode();

    const std::string &getAlias() const {
        return this->alias;
    }

    TypeNode *getTargetTypeNode() const {
        return this->targetTypeNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

/**
 * indicating for, while, do-while statement
 */
class LoopNode : public Node {
private:
    /**
     * may be empty node
     */
    Node *initNode;

    /**
     * may be empty node
     */
    Node *condNode;

    /**
     * may be empty node
     */
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

    ~LoopNode();

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
    void accept(NodeVisitor &visitor) override;
};

class IfNode : public Node {
private:
    Node *condNode;
    BlockNode *thenNode;
    Node *elseNode;

public:
    /**
     * elseNode may be null
     */
    IfNode(unsigned int startPos, Node *condNode, BlockNode *thenNode, Node *elseNode);

    ~IfNode();

    Node *getCondNode() const {
        return this->condNode;
    }

    Node *&refCondNode() {
        return this->condNode;
    }

    BlockNode *getThenNode() const {
        return this->thenNode;
    }

    Node *getElseNode() const {
        return this->elseNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class ReturnNode : public Node {
private:
    Node *exprNode;

public:
    ReturnNode(Token token, Node *exprNode);

    ~ReturnNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class ThrowNode : public Node {
private:
    Node *exprNode;

public:
    ThrowNode(unsigned int startPos, Node *exprNode) :
            Node({startPos, 0}), exprNode(exprNode) {
        this->updateToken(exprNode->getToken());
    }

    ~ThrowNode();

    Node *getExprNode() const {
        return this->exprNode;
    }

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
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
            Node({startPos, 0}), exceptionName(std::move(exceptionName)),
            typeNode(typeNode != nullptr ? typeNode : newAnyTypeNode()), varIndex(0), blockNode(blockNode) {
        this->updateToken(blockNode->getToken());
    }

    ~CatchNode();

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
    void accept(NodeVisitor &visitor) override;
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
            Node({startPos, 0}), blockNode(blockNode), catchNodes(), finallyNode() {
        this->updateToken(blockNode->getToken());
    }

    ~TryNode();

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
    void accept(NodeVisitor &visitor) override;
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

    ~VarDeclNode();

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
    void accept(NodeVisitor &visitor) override;
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
            Node(leftNode->getToken()),
            leftNode(leftNode), rightNode(rightNode), attributeSet(0) {
        if(selfAssign) {
            setFlag(this->attributeSet, SELF_ASSIGN);
        }
        this->updateToken(rightNode->getToken());
    }

    ~AssignNode();

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
    void accept(NodeVisitor &visitor) override;
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
    ~ElementSelfAssignNode();

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
    void accept(NodeVisitor &visitor) override;
};

class CallableNode : public Node {
protected:
    SourceInfoPtr srcInfoPtr;

    /**
     * if RootNode, name is empty.
     */
    std::string name;

public:
    CallableNode(unsigned int startPos, const SourceInfoPtr &srcInfoPtr, std::string &&name) :
            Node({startPos, 0}), srcInfoPtr(srcInfoPtr), name(std::move(name)) { }

    CallableNode() : Node({0, 0}), srcInfoPtr(nullptr), name() { }

    virtual ~CallableNode() = default;

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
            CallableNode(startPos, srcInfoPtr, std::move(funcName)),
            paramNodes(), paramTypeNodes(), returnTypeNode(),
            blockNode(), maxVarNum(0), varIndex(0) { }

    ~FunctionNode();

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

    TypeNode *getReturnTypeToken();

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
    void accept(NodeVisitor &visitor) override;
};

class InterfaceNode : public Node {
private:
    std::string interfaceName;

    std::vector<FunctionNode *> methodDeclNodes;
    std::vector<VarDeclNode *> fieldDeclNodes;
    std::vector<TypeNode *> fieldTypeNodes;

public:
    InterfaceNode(unsigned int startPos, std::string &&interfaceName) :
            Node({startPos, 0}), interfaceName(std::move(interfaceName)), methodDeclNodes(),
            fieldDeclNodes(), fieldTypeNodes() { }

    ~InterfaceNode();

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
    void accept(NodeVisitor &visitor) override;
};

class UserDefinedCmdNode : public CallableNode {
private:
    unsigned int udcIndex;
    BlockNode *blockNode;

    unsigned int maxVarNum;

public:
    UserDefinedCmdNode(unsigned int startPos, const SourceInfoPtr &srcInfoPtr,
                       std::string &&commandName, BlockNode *blockNode) :
            CallableNode(startPos, srcInfoPtr, std::move(commandName)),
            udcIndex(0), blockNode(blockNode), maxVarNum(0) {
        this->updateToken(blockNode->getToken());
    }

    ~UserDefinedCmdNode();

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
    void accept(NodeVisitor &visitor) override;
};

class EmptyNode : public Node {
public:
    EmptyNode() : Node({0, 0}) { }
    explicit EmptyNode(Token token) : Node(token) { }
    ~EmptyNode() = default;

    void dump(NodeDumper &dumper) const override;
    void accept(NodeVisitor &visitor) override;
};

class RootNode : public CallableNode {
private:
    std::vector<Node *> nodes;

    /**
     * max number of local variable.
     */
    unsigned int maxVarNum;

    /**
     * max number of global variable.
     */
    unsigned int maxGVarNum;

public:
    RootNode() : CallableNode(), nodes(), maxVarNum(0), maxGVarNum(0) { }

    ~RootNode();

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
    void accept(NodeVisitor &visitor) override;
};

// helper function for node creation

const char *resolveUnaryOpName(TokenKind op);

const char *resolveBinaryOpName(TokenKind op);

TokenKind resolveAssignOp(TokenKind op);

LoopNode *createForInNode(unsigned int startPos, std::string &&varName, Node *exprNode, BlockNode *blockNode);

Node *createSuffixNode(Node *leftNode, TokenKind op, Token token);

Node *createAssignNode(Node *leftNode, TokenKind op, Node *rightNode);

Node *createIndexNode(Node *recvNode, Node *indexNode);

struct NodeVisitor {
    virtual ~NodeVisitor() = default;

    virtual void visit(Node &node) { node.accept(*this); }
    virtual void visitBaseTypeNode(BaseTypeNode &node) = 0;
    virtual void visitReifiedTypeNode(ReifiedTypeNode &node) = 0;
    virtual void visitFuncTypeNode(FuncTypeNode &node) = 0;
    virtual void visitDBusIfaceTypeNode(DBusIfaceTypeNode &node) = 0;
    virtual void visitReturnTypeNode(ReturnTypeNode &node) = 0;
    virtual void visitTypeOfNode(TypeOfNode &node) = 0;
    virtual void visitIntValueNode(IntValueNode &node) = 0;
    virtual void visitLongValueNode(LongValueNode &node) = 0;
    virtual void visitFloatValueNode(FloatValueNode &node) = 0;
    virtual void visitStringValueNode(StringValueNode &node) = 0;
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
    virtual void visitTernaryNode(TernaryNode &node) = 0;
    virtual void visitCmdNode(CmdNode &node) = 0;
    virtual void visitCmdArgNode(CmdArgNode &node) = 0;
    virtual void visitRedirNode(RedirNode &node) = 0;
    virtual void visitTildeNode(TildeNode &node) = 0;
    virtual void visitPipedCmdNode(PipedCmdNode &node) = 0;
    virtual void visitSubstitutionNode(SubstitutionNode &node) = 0;
    virtual void visitAssertNode(AssertNode &node) = 0;
    virtual void visitBlockNode(BlockNode &node) = 0;
    virtual void visitJumpNode(JumpNode &node) = 0;
    virtual void visitTypeAliasNode(TypeAliasNode &node) = 0;
    virtual void visitLoopNode(LoopNode &node) = 0;
    virtual void visitIfNode(IfNode &node) = 0;
    virtual void visitReturnNode(ReturnNode &node) = 0;
    virtual void visitThrowNode(ThrowNode &node) = 0;
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
    virtual ~BaseVisitor() = default;

    virtual void visitDefault(Node &node) = 0;

    virtual void visitBaseTypeNode(BaseTypeNode &node) override { this->visitDefault(node); }
    virtual void visitReifiedTypeNode(ReifiedTypeNode &node) override { this->visitDefault(node); }
    virtual void visitFuncTypeNode(FuncTypeNode &node) override { this->visitDefault(node); }
    virtual void visitDBusIfaceTypeNode(DBusIfaceTypeNode &node) override { this->visitDefault(node); }
    virtual void visitReturnTypeNode(ReturnTypeNode &node) override { this->visitDefault(node); }
    virtual void visitTypeOfNode(TypeOfNode &node) override { this->visitDefault(node); }
    virtual void visitIntValueNode(IntValueNode &node) override { this->visitDefault(node); }
    virtual void visitLongValueNode(LongValueNode &node) override { this->visitDefault(node); }
    virtual void visitFloatValueNode(FloatValueNode &node) override { this->visitDefault(node); }
    virtual void visitStringValueNode(StringValueNode &node) override { this->visitDefault(node); }
    virtual void visitStringExprNode(StringExprNode &node) override { this->visitDefault(node); }
    virtual void visitRegexNode(RegexNode &node) override { this->visitDefault(node); }
    virtual void visitArrayNode(ArrayNode &node) override { this->visitDefault(node); }
    virtual void visitMapNode(MapNode &node) override { this->visitDefault(node); }
    virtual void visitTupleNode(TupleNode &node) override { this->visitDefault(node); }
    virtual void visitVarNode(VarNode &node) override { this->visitDefault(node); }
    virtual void visitAccessNode(AccessNode &node) override { this->visitDefault(node); }
    virtual void visitTypeOpNode(TypeOpNode &node) override { this->visitDefault(node); }
    virtual void visitUnaryOpNode(UnaryOpNode &node) override { this->visitDefault(node); }
    virtual void visitBinaryOpNode(BinaryOpNode &node) override { this->visitDefault(node); }
    virtual void visitApplyNode(ApplyNode &node) override { this->visitDefault(node); }
    virtual void visitMethodCallNode(MethodCallNode &node) override { this->visitDefault(node); }
    virtual void visitNewNode(NewNode &node) override { this->visitDefault(node); }
    virtual void visitTernaryNode(TernaryNode &node) override { this->visitDefault(node); }
    virtual void visitCmdNode(CmdNode &node) override { this->visitDefault(node); }
    virtual void visitCmdArgNode(CmdArgNode &node) override { this->visitDefault(node); }
    virtual void visitRedirNode(RedirNode &node) override { this->visitDefault(node); }
    virtual void visitTildeNode(TildeNode &node) override { this->visitDefault(node); }
    virtual void visitPipedCmdNode(PipedCmdNode &node) override { this->visitDefault(node); }
    virtual void visitSubstitutionNode(SubstitutionNode &node) override { this->visitDefault(node); }
    virtual void visitAssertNode(AssertNode &node) override { this->visitDefault(node); }
    virtual void visitBlockNode(BlockNode &node) override { this->visitDefault(node); }
    virtual void visitJumpNode(JumpNode &node) override { this->visitDefault(node); }
    virtual void visitTypeAliasNode(TypeAliasNode &node) override { this->visitDefault(node); }
    virtual void visitLoopNode(LoopNode &node) override { this->visitDefault(node); }
    virtual void visitIfNode(IfNode &node) override { this->visitDefault(node); }
    virtual void visitReturnNode(ReturnNode &node) override { this->visitDefault(node); }
    virtual void visitThrowNode(ThrowNode &node) override { this->visitDefault(node); }
    virtual void visitCatchNode(CatchNode &node) override { this->visitDefault(node); }
    virtual void visitTryNode(TryNode &node) override { this->visitDefault(node); }
    virtual void visitVarDeclNode(VarDeclNode &node) override { this->visitDefault(node); }
    virtual void visitAssignNode(AssignNode &node) override { this->visitDefault(node); }
    virtual void visitElementSelfAssignNode(ElementSelfAssignNode &node) override { this->visitDefault(node); }
    virtual void visitFunctionNode(FunctionNode &node) override { this->visitDefault(node); }
    virtual void visitInterfaceNode(InterfaceNode &node) override { this->visitDefault(node); }
    virtual void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override { this->visitDefault(node); }
    virtual void visitEmptyNode(EmptyNode &node) override { this->visitDefault(node); }
    virtual void visitRootNode(RootNode &node) override { this->visitDefault(node); }
};

} // namespace ydsh

#endif //YDSH_NODE_H
