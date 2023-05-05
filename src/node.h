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

#include <cassert>
#include <list>
#include <memory>
#include <utility>

#include "brace.h"
#include "constant.h"
#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "misc/result.hpp"
#include "misc/rtti.hpp"
#include "misc/token.hpp"
#include "regex_wrapper.h"
#include "token_kind.h"
#include "type.h"

namespace ydsh {

#define EACH_NODE_KIND(OP)                                                                         \
  OP(Type)                                                                                         \
  OP(Number)                                                                                       \
  OP(String)                                                                                       \
  OP(StringExpr)                                                                                   \
  OP(Regex)                                                                                        \
  OP(Array)                                                                                        \
  OP(Map)                                                                                          \
  OP(Tuple)                                                                                        \
  OP(Var)                                                                                          \
  OP(Access)                                                                                       \
  OP(TypeOp)                                                                                       \
  OP(UnaryOp)                                                                                      \
  OP(BinaryOp)                                                                                     \
  OP(Args)                                                                                         \
  OP(Apply)                                                                                        \
  OP(New)                                                                                          \
  OP(Embed)                                                                                        \
  OP(Cmd)                                                                                          \
  OP(CmdArg)                                                                                       \
  OP(ArgArray)                                                                                     \
  OP(Redir)                                                                                        \
  OP(WildCard)                                                                                     \
  OP(BraceSeq)                                                                                     \
  OP(Pipeline)                                                                                     \
  OP(With)                                                                                         \
  OP(Time)                                                                                         \
  OP(Fork)                                                                                         \
  OP(Assert)                                                                                       \
  OP(Block)                                                                                        \
  OP(TypeDef)                                                                                      \
  OP(Defer)                                                                                        \
  OP(Loop)                                                                                         \
  OP(If)                                                                                           \
  OP(Case)                                                                                         \
  OP(Arm)                                                                                          \
  OP(Jump)                                                                                         \
  OP(Catch)                                                                                        \
  OP(Try)                                                                                          \
  OP(VarDecl)                                                                                      \
  OP(Assign)                                                                                       \
  OP(ElementSelfAssign)                                                                            \
  OP(PrefixAssign)                                                                                 \
  OP(Function)                                                                                     \
  OP(UserDefinedCmd)                                                                               \
  OP(FuncList)                                                                                     \
  OP(Source)                                                                                       \
  OP(SourceList)                                                                                   \
  OP(CodeComp)                                                                                     \
  OP(Error)                                                                                        \
  OP(Empty)

enum class NodeKind : unsigned char {
#define GEN_ENUM(T) T,
  EACH_NODE_KIND(GEN_ENUM)
#undef GEN_ENUM
};

struct NodeVisitor;
class NodeDumper;

class Node {
private:
  const NodeKind nodeKind;

  /**
   * maintains paren position `(` `)`
   */
  Token token;

  /**
   * actual token
   */
  Token actual;

  /**
   * initial value is null.
   */
  const DSType *type{nullptr};

protected:
  Node(NodeKind kind, Token token) : nodeKind(kind), token(token), actual(token) {}

public:
  NON_COPYABLE(Node);

  virtual ~Node() = default;

  NodeKind getNodeKind() const { return this->nodeKind; }

  bool is(NodeKind kind) const { return this->nodeKind == kind; }

  Token getToken() const { return this->token; }

  Token getActualToken() const { return this->actual; }

  unsigned int getPos() const { return this->token.pos; }

  unsigned int getActualPos() const { return this->actual.pos; }

  unsigned int getSize() const { return this->token.size; }

  void updateToken(Token t) {
    if (t.endPos() > this->token.endPos()) {
      this->token.size = t.endPos() - this->token.pos;
    }
    this->actual = this->token;
  }

  void setParenPos(unsigned int openPos, Token closeToken) {
    assert(openPos <= this->actual.pos);
    assert(this->actual.endPos() <= closeToken.endPos());
    this->token = {openPos, closeToken.endPos() - openPos};
  };

  void setType(const DSType &t) { this->type = &t; }

  /**
   * must not call it before type checking
   */
  const DSType &getType() const { return *this->type; }

  bool isUntyped() const { return this->type == nullptr; }

  virtual void dump(NodeDumper &dumper) const = 0;

  void accept(NodeVisitor &visitor);
};

template <typename T, NodeKind K, enable_when<std::is_base_of<Node, T>::value> = nullptr>
class WithRtti : public T {
protected:
  explicit WithRtti(Token token) : T(K, token) {}

public:
  static constexpr auto KIND = K;

  static bool classof(const Node *node) { return node->getNodeKind() == K; }
};

// type definition
#define EACH_TYPE_NODE_KIND(OP)                                                                    \
  OP(Base)                                                                                         \
  OP(Qualified)                                                                                    \
  OP(Reified)                                                                                      \
  OP(Func)                                                                                         \
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
  TypeNode(Kind typeKind, Token token) : WithRtti(token), typeKind(typeKind) {}

public:
  ~TypeNode() override = default;

  void dump(NodeDumper &dumper) const override;
};

class BaseTypeNode : public TypeNode {
private:
  std::string typeName;
  HandlePtr handle; // may be null if not type-alias or user-defined type

public:
  BaseTypeNode(Token token, std::string &&typeName)
      : TypeNode(TypeNode::Base, token), typeName(std::move(typeName)) {}

  ~BaseTypeNode() override = default;

  const std::string &getTokenText() const { return this->typeName; }

  void setHandle(HandlePtr hd) { this->handle = std::move(hd); }

  const HandlePtr &getHandle() const { return this->handle; }

  void dump(NodeDumper &dumper) const override;
};

class QualifiedTypeNode : public TypeNode {
private:
  std::unique_ptr<TypeNode> recvTypeNode;
  std::unique_ptr<BaseTypeNode> nameTypeNode;

public:
  QualifiedTypeNode(std::unique_ptr<TypeNode> &&recvTypeNode,
                    std::unique_ptr<BaseTypeNode> &&nameNode)
      : TypeNode(TypeNode::Qualified, recvTypeNode->getToken()),
        recvTypeNode(std::move(recvTypeNode)), nameTypeNode(std::move(nameNode)) {
    this->updateToken(this->nameTypeNode->getToken());
  }

  TypeNode &getRecvTypeNode() const { return *this->recvTypeNode; }

  BaseTypeNode &getNameTypeNode() const { return *this->nameTypeNode; }

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
                  std::vector<std::unique_ptr<TypeNode>> &&elementNodes, Token endToken)
      : TypeNode(TypeNode::Reified, templateTypeNode->getToken()),
        templateTypeNode(std::move(templateTypeNode)), elementTypeNodes(std::move(elementNodes)) {
    this->updateToken(endToken);
  }

  ReifiedTypeNode(std::unique_ptr<TypeNode> &&elementNode, std::unique_ptr<BaseTypeNode> &&tempNode)
      : TypeNode(TypeNode::Reified, elementNode->getToken()), templateTypeNode(std::move(tempNode)),
        elementTypeNodes(1) {
    this->elementTypeNodes[0] = std::move(elementNode);
    this->updateToken(this->templateTypeNode->getToken());
  }

  const std::unique_ptr<BaseTypeNode> &getTemplate() const { return this->templateTypeNode; }

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
               std::vector<std::unique_ptr<TypeNode>> paramTypeNodes, Token endToken)
      : TypeNode(TypeNode::Func, {startPos, 0}), returnTypeNode(std::move(returnTypeNode)),
        paramTypeNodes(std::move(paramTypeNodes)) {
    this->updateToken(endToken);
  }

  FuncTypeNode(unsigned int startPos, std::vector<std::unique_ptr<TypeNode>> paramTypeNodes,
               std::unique_ptr<TypeNode> &&returnTypeNode)
      : TypeNode(TypeNode::Func, {startPos, 0}), returnTypeNode(std::move(returnTypeNode)),
        paramTypeNodes(std::move(paramTypeNodes)) {
    this->updateToken(this->returnTypeNode->getToken());
  }

  const std::vector<std::unique_ptr<TypeNode>> &getParamTypeNodes() const {
    return this->paramTypeNodes;
  }

  TypeNode &getReturnTypeNode() const { return *this->returnTypeNode; }

  void dump(NodeDumper &dumper) const override;
};

class TypeOfNode : public TypeNode {
private:
  std::unique_ptr<Node> exprNode;

public:
  TypeOfNode(unsigned int startPos, std::unique_ptr<Node> &&exprNode, Token endToken)
      : TypeNode(TypeNode::TypeOf, {startPos, 0}), exprNode(std::move(exprNode)) {
    this->updateToken(endToken);
  }

  Node &getExprNode() const { return *this->exprNode; }

  void dump(NodeDumper &dumper) const override;
};

inline std::unique_ptr<TypeNode> newVoidTypeNode() {
  return std::make_unique<BaseTypeNode>(Token{0, 0}, std::string("Void"));
}

inline std::unique_ptr<TypeNode> newErrorTypeNode() {
  return std::make_unique<BaseTypeNode>(Token{0, 0}, "Error");
}

// expression definition

#define EACH_NUMBER_NODE_KIND(OP)                                                                  \
  OP(Int)                                                                                          \
  OP(Float)                                                                                        \
  OP(Signal)                                                                                       \
  OP(Bool)                                                                                         \
  OP(None) /* for dummy value */

class NumberNode : public WithRtti<Node, NodeKind::Number> {
public:
  const enum Kind : unsigned char {
#define GEN_ENUM(OP) OP,
    EACH_NUMBER_NODE_KIND(GEN_ENUM)
#undef GEN_ENUM
  } kind;

private:
  bool init{true};
  union {
    int64_t intValue;
    double floatValue;
  };

public:
  /**
   * not call directly
   * @param token
   * @param kind
   */
  NumberNode(Token token, Kind kind) : WithRtti(token), kind(kind), intValue(0) {}

  static std::unique_ptr<NumberNode> newInt(Token token, int64_t value) {
    auto node = std::make_unique<NumberNode>(token, Int);
    node->intValue = value;
    return node;
  }

  static std::unique_ptr<NumberNode> newInt(Token token) {
    auto node = std::make_unique<NumberNode>(token, Int);
    node->init = false;
    return node;
  }

  static std::unique_ptr<NumberNode> newFloat(Token token, double value) {
    auto node = std::make_unique<NumberNode>(token, Float);
    node->floatValue = value;
    return node;
  }

  static std::unique_ptr<NumberNode> newFloat(Token token) {
    auto node = std::make_unique<NumberNode>(token, Float);
    node->init = false;
    return node;
  }

  static std::unique_ptr<NumberNode> newSignal(Token token, int value) {
    auto node = std::make_unique<NumberNode>(token, Signal);
    node->intValue = value;
    return node;
  }

  /**
   * for constant expression
   * @param token
   * @param value
   * @return
   */
  static std::unique_ptr<NumberNode> newBool(Token token, bool value) {
    auto node = std::make_unique<NumberNode>(token, Bool);
    node->intValue = value ? 1 : 0;
    return node;
  }

  /**
   * for constant expression
   * @param token
   * @return
   */
  static std::unique_ptr<NumberNode> newNone(Token token) {
    auto node = std::make_unique<NumberNode>(token, None);
    return node;
  }

  ~NumberNode() override = default;

  int64_t getIntValue() const { return this->intValue; }

  double getFloatValue() const { return this->floatValue; }

  bool isInit() const { return this->init; }

  void setIntValue(int64_t v) {
    this->init = true;
    this->intValue = v;
  }

  void setFloatValue(double v) {
    this->init = true;
    this->floatValue = v;
  }

  void dump(NodeDumper &dumper) const override;

  std::unique_ptr<NumberNode> clone() const {
    auto node = std::make_unique<NumberNode>(this->getActualToken(), this->kind);
    node->setType(this->getType());
    if (this->kind == Kind::Float) {
      node->floatValue = this->getFloatValue();
    } else {
      node->intValue = this->getIntValue();
    }
    return node;
  }
};

class StringNode : public WithRtti<Node, NodeKind::String> {
public:
  enum StringKind {
    STRING,
    TILDE,
    BACKQUOTE,
  };

private:
  std::string value;
  StringKind kind;
  bool init;

public:
  /**
   * used for CommandNode. lineNum is always 0.
   */
  explicit StringNode(std::string &&value) : StringNode({0, 0}, std::move(value)) {}

  explicit StringNode(Token token) : WithRtti(token), kind(STRING), init(false) {}

  StringNode(Token token, std::string &&value, StringKind kind = STRING)
      : WithRtti(token), value(std::move(value)), kind(kind), init(true) {}

  ~StringNode() override = default;

  const std::string &getValue() const { return this->value; }

  StringKind getKind() const { return this->kind; }

  bool isTilde() const { return this->getKind() == TILDE; }

  void unsetTilde() { this->kind = STRING; }

  void dump(NodeDumper &dumper) const override;

  bool isInit() const { return this->init; }

  void setValue(std::string &&v) {
    this->init = true;
    this->value = std::move(v);
  }

  std::string takeValue() { return std::move(this->value); }
};

class StringExprNode : public WithRtti<Node, NodeKind::StringExpr> {
private:
  std::vector<std::unique_ptr<Node>> nodes;

public:
  explicit StringExprNode(unsigned int startPos) : WithRtti({startPos, 1}) {}

  ~StringExprNode() override = default;

  void addExprNode(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken()); // for here-doc
    this->nodes.push_back(std::move(node));
  }

  const std::vector<std::unique_ptr<Node>> &getExprNodes() const { return this->nodes; }

  std::vector<std::unique_ptr<Node>> &refExprNodes() { return this->nodes; }

  void dump(NodeDumper &dumper) const override;
};

class RegexNode : public WithRtti<Node, NodeKind::Regex> {
private:
  /**
   * string representation of regex.
   * TODO: delete it.
   */
  std::string reStr;

  std::string reFlag;

  PCRE re;

public:
  RegexNode(Token token, std::string &&str, std::string &&flag)
      : WithRtti(token), reStr(std::move(str)), reFlag(flag) {}

  ~RegexNode() override = default;

  const std::string &getReStr() const { return this->reStr; }

  const std::string &getReFlag() const { return this->reFlag; }

  /**
   * build regex
   * @param errorStr
   * @return
   * if failed, return false and set error message to errorStr
   */
  bool buildRegex(std::string &errorStr);

  PCRE extractRE() { return std::move(this->re); }

  void dump(NodeDumper &dumper) const override;
};

class ArrayNode : public WithRtti<Node, NodeKind::Array> {
private:
  std::vector<std::unique_ptr<Node>> nodes;

public:
  ArrayNode(unsigned int startPos, std::unique_ptr<Node> &&node) : WithRtti({startPos, 0}) {
    this->addExprNode(std::move(node));
  }

  void addExprNode(std::unique_ptr<Node> &&node) { this->nodes.push_back(std::move(node)); }

  const std::vector<std::unique_ptr<Node>> &getExprNodes() const { return this->nodes; }

  void dump(NodeDumper &dumper) const override;
};

class MapNode : public WithRtti<Node, NodeKind::Map> {
private:
  std::vector<std::unique_ptr<Node>> keyNodes;
  std::vector<std::unique_ptr<Node>> valueNodes;

public:
  MapNode(unsigned int startPos, std::unique_ptr<Node> &&keyNode, std::unique_ptr<Node> &&valueNode)
      : WithRtti({startPos, 0}) {
    this->addEntry(std::move(keyNode), std::move(valueNode));
  }

  void addEntry(std::unique_ptr<Node> &&keyNode, std::unique_ptr<Node> &&valueNode);

  const std::vector<std::unique_ptr<Node>> &getKeyNodes() const { return this->keyNodes; }

  const std::vector<std::unique_ptr<Node>> &getValueNodes() const { return this->valueNodes; }

  void dump(NodeDumper &dumper) const override;
};

class TupleNode : public WithRtti<Node, NodeKind::Tuple> {
private:
  /**
   * at least one nodes
   */
  std::vector<std::unique_ptr<Node>> nodes;

public:
  TupleNode(unsigned int startPos, std::vector<std::unique_ptr<Node>> &&nodes, Token endToken)
      : WithRtti({startPos, 0}), nodes(std::move(nodes)) {
    this->updateToken(endToken);
  }

  ~TupleNode() override = default;

  const std::vector<std::unique_ptr<Node>> &getNodes() const { return this->nodes; }

  void dump(NodeDumper &dumper) const override;
};

class NameInfo {
private:
  Token token;
  std::string name;

public:
  NameInfo(Token token, std::string &&name) : token(token), name(std::move(name)) {}

  NameInfo(Token token, const std::string &name) : NameInfo(token, std::string(name)) {}

  Token getToken() const { return this->token; }

  const std::string &getName() const { return this->name; }

  void setName(std::string &&n) { this->name = std::move(n); }

  explicit operator bool() const { return !this->name.empty(); }
};

/**
 * base class for VarNode, AccessNode
 */
class AssignableNode : public Node {
protected:
  HandlePtr handle;

  AssignableNode(NodeKind kind, Token token) : Node(kind, token) {}

public:
  static bool classof(const Node *node) {
    return node->getNodeKind() == NodeKind::Var || node->getNodeKind() == NodeKind::Access;
  }

  ~AssignableNode() override = default;

  void setHandle(HandlePtr hd) { this->handle = std::move(hd); }

  const HandlePtr &getHandle() const { return this->handle; }

  bool hasAttr(HandleAttr attr) const { return hasFlag(this->getHandle()->attr(), attr); }

  unsigned int getIndex() const { return this->getHandle()->getIndex(); }
};

class VarNode : public WithRtti<AssignableNode, NodeKind::Var> {
public:
  enum ExtraOp : unsigned int {
    NONE,
    ARGS_LEN,
    POSITIONAL_ARG,
  };

private:
  std::string varName;
  ExtraOp extraOp{NONE};
  unsigned int extraValue{0}; // for POSITIONAL_ARG

public:
  VarNode(Token token, std::string &&varName);

  ~VarNode() override = default;

  const std::string &getVarName() const { return this->varName; }

  ExtraOp getExtraOp() const { return this->extraOp; }

  void setExtraValue(unsigned int v) { this->extraValue = v; }

  unsigned int getExtraValue() const { return this->extraValue; }

  void dump(NodeDumper &dumper) const override;
};

class AccessNode : public WithRtti<AssignableNode, NodeKind::Access> {
public:
  enum AdditionalOp {
    NOP,
    DUP_RECV,
  };

private:
  std::unique_ptr<Node> recvNode;
  NameInfo nameInfo;
  AdditionalOp additionalOp{NOP};

public:
  AccessNode(std::unique_ptr<Node> &&recvNode, NameInfo &&nameInfo)
      : WithRtti(recvNode->getToken()), recvNode(std::move(recvNode)),
        nameInfo(std::move(nameInfo)) {
    this->updateToken(this->nameInfo.getToken());
  }

  Node &getRecvNode() const { return *this->recvNode; }

  std::unique_ptr<Node> &refRecvNode() { return this->recvNode; }

  const std::string &getFieldName() const { return this->nameInfo.getName(); }

  /**
   * force rewrite field name
   */
  void setFieldName(std::string &&name) { this->nameInfo.setName(std::move(name)); }

  Token getNameToken() const { return this->nameInfo.getToken(); }

  const NameInfo &getField() const { return this->nameInfo; }

  void setAdditionalOp(AdditionalOp op) { this->additionalOp = op; }

  AdditionalOp getAdditionalOp() const { return this->additionalOp; }

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
  std::unique_ptr<Node> exprNode;

  /**
   * may be null
   */
  std::unique_ptr<TypeNode> targetTypeNode;

  OpKind opKind;

public:
  TypeOpNode(std::unique_ptr<Node> &&exprNode, std::unique_ptr<TypeNode> &&type, OpKind init)
      : WithRtti(exprNode->getToken()), exprNode(std::move(exprNode)),
        targetTypeNode(std::move(type)), opKind(init) {
    if (this->targetTypeNode) {
      this->updateToken(this->targetTypeNode->getToken());
    }
  }

  /**
   * for implicit cast.
   * @param targetNode
   * @param type
   * @return
   */
  static std::unique_ptr<TypeOpNode> newTypedCastNode(std::unique_ptr<Node> &&targetNode,
                                                      const DSType &type) {
    assert(!targetNode->isUntyped());
    auto castNode =
        std::make_unique<TypeOpNode>(std::move(targetNode), nullptr, TypeOpNode::NO_CAST);
    castNode->setType(type);
    return castNode;
  }

  /**
   *
   * @param pool
   * @param node
   * must be typed
   * @return
   */
  static std::unique_ptr<Node> newPrintOpNode(const TypePool &pool, std::unique_ptr<Node> &&node);

  Node &getExprNode() const { return *this->exprNode; }

  /**
   *
   * @return
   * may be null
   */
  const std::unique_ptr<TypeNode> &getTargetTypeNode() const { return this->targetTypeNode; }

  void setOpKind(OpKind op) { this->opKind = op; }

  OpKind getOpKind() const { return this->opKind; }

  bool isCastOp() const {
    return static_cast<unsigned char>(this->opKind) <= static_cast<unsigned char>(PRINT);
  }

  bool isInstanceOfOp() const {
    return static_cast<unsigned char>(this->opKind) >= static_cast<unsigned char>(ALWAYS_FALSE);
  }

  void dump(NodeDumper &dumper) const override;
};

class ArgsNode : public WithRtti<Node, NodeKind::Args> {
private:
  std::vector<std::unique_ptr<Node>> nodes;

public:
  explicit ArgsNode(Token token) : WithRtti(token) {}

  ArgsNode() : WithRtti({0, 0}) {}

  void addNode(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->nodes.push_back(std::move(node));
  }

  const std::vector<std::unique_ptr<Node>> &getNodes() const { return this->nodes; }

  std::vector<std::unique_ptr<Node>> &refNodes() { return this->nodes; }

  void dump(NodeDumper &dumper) const override;
};

/**
 * for function object apply or method call
 */
class ApplyNode : public WithRtti<Node, NodeKind::Apply> {
public:
  enum Kind : unsigned int {
    UNRESOLVED,
    FUNC_CALL,
    METHOD_CALL,
  };

  enum Attr : unsigned int {
    DEFAULT,
    INDEX,
    UNARY,
    BINARY,
    NEW_ITER,
    ITER_NEXT,
    MAP_ITER_NEXT_KEY,
    MAP_ITER_NEXT_VALUE,
  };

private:
  std::unique_ptr<Node> exprNode;
  std::unique_ptr<ArgsNode> argsNode;

  /**
   * for method call
   */
  const MethodHandle *handle{nullptr};

  Kind kind;

  Attr attr{DEFAULT};

public:
  ApplyNode(std::unique_ptr<Node> &&exprNode, std::unique_ptr<ArgsNode> &&argsNode,
            Kind kind = UNRESOLVED)
      : WithRtti(exprNode->getToken()), exprNode(std::move(exprNode)),
        argsNode(std::move(argsNode)), kind(kind) {
    this->updateToken(this->argsNode->getToken());
  }

  static std::unique_ptr<ApplyNode> newMethodCall(std::unique_ptr<Node> &&recvNode, Token token,
                                                  std::string &&methodName);

  static std::unique_ptr<ApplyNode> newMethodCall(std::unique_ptr<Node> &&recvNode,
                                                  std::string &&methodName) {
    Token token = recvNode->getToken();
    return newMethodCall(std::move(recvNode), token, std::move(methodName));
  }

  static std::unique_ptr<ApplyNode> newIndexCall(std::unique_ptr<Node> &&recvNode, Token token,
                                                 std::unique_ptr<Node> &&indexNode) {
    auto node = newMethodCall(std::move(recvNode), token, std::string(OP_GET));
    node->setAttr(INDEX);
    node->argsNode->addNode(std::move(indexNode));
    return node;
  }

  static std::unique_ptr<ApplyNode> newIndexCall(std::unique_ptr<Node> &&recvNode, Token token,
                                                 std::unique_ptr<Node> &&indexNode,
                                                 std::unique_ptr<Node> &&rightNode) {
    auto node = ApplyNode::newMethodCall(std::move(recvNode), token, std::string(OP_SET));
    node->getArgsNode().addNode(std::move(indexNode));
    node->getArgsNode().addNode(std::move(rightNode));
    node->setAttr(INDEX);
    return node;
  }

  static std::unique_ptr<ApplyNode> newUnary(TokenKind op, Token opToken,
                                             std::unique_ptr<Node> &&recvNode) {
    auto node = newMethodCall(std::move(recvNode), opToken, resolveUnaryOpName(op));
    node->setAttr(UNARY);
    return node;
  }

  static std::unique_ptr<ApplyNode> newBinary(std::unique_ptr<Node> &&leftNode, TokenKind op,
                                              Token opToken, std::unique_ptr<Node> &&rightNode) {
    auto node = ApplyNode::newMethodCall(std::move(leftNode), opToken, resolveBinaryOpName(op));
    node->getArgsNode().addNode(std::move(rightNode));
    node->setAttr(BINARY);
    return node;
  }

  static std::unique_ptr<ApplyNode> newIter(std::unique_ptr<Node> &&recvNode) {
    auto node = ApplyNode::newMethodCall(std::move(recvNode), std::string(OP_ITER));
    node->setAttr(NEW_ITER);
    return node;
  }

  static std::unique_ptr<ApplyNode> newIterNext(std::unique_ptr<Node> &&recvNode) {
    auto node = ApplyNode::newMethodCall(std::move(recvNode), std::string(OP_NEXT));
    node->setAttr(ITER_NEXT);
    return node;
  }

  static std::unique_ptr<ApplyNode> newMapIterNextKey(std::unique_ptr<Node> &&recvNode) {
    auto node = newIter(std::move(recvNode));
    node->setAttr(MAP_ITER_NEXT_KEY);
    return node;
  }

  static std::unique_ptr<ApplyNode> newMapIterNextValue(std::unique_ptr<Node> &&recvNode) {
    auto node = newIter(std::move(recvNode));
    node->setAttr(MAP_ITER_NEXT_VALUE);
    return node;
  }

  Node &getExprNode() const { return *this->exprNode; }

  ArgsNode &getArgsNode() const { return *this->argsNode; }

  const std::string &getMethodName() const {
    assert(this->isMethodCall());
    return cast<AccessNode>(*this->exprNode).getFieldName();
  }

  void setMethodName(std::string &&name) {
    assert(this->isIndexOp());
    cast<AccessNode>(*this->exprNode).setFieldName(std::move(name));
  }

  Node &getRecvNode() const {
    assert(this->isMethodCall());
    return cast<AccessNode>(*this->exprNode).getRecvNode();
  }

  Kind getKind() const { return this->kind; }

  void setKind(Kind k) { this->kind = k; }

  Attr getAttr() const { return this->attr; }

  bool isFuncCall() const { return this->getKind() == FUNC_CALL; }

  bool isMethodCall() const { return this->getKind() == METHOD_CALL; }

  bool isIndexOp() const { return this->getAttr() == INDEX; }

  bool isBinaryOp() const { return this->getAttr() == BINARY; }

  bool isUnaryOp() const { return this->getAttr() == UNARY; }

  void setHandle(const MethodHandle *h) { this->handle = h; }

  const MethodHandle *getHandle() const { return this->handle; }

  static std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>>
  split(std::unique_ptr<ApplyNode> &&node) {
    auto first = std::move(cast<AccessNode>(*node->exprNode).refRecvNode());
    auto second = std::move(node->argsNode->refNodes()[0]);
    return {std::move(first), std::move(second)};
  }

  void dump(NodeDumper &dumper) const override;

private:
  void setAttr(Attr a) { this->attr = a; }
};

/**
 * allocate new DSObject and call constructor.
 */
class NewNode : public WithRtti<Node, NodeKind::New> {
private:
  std::unique_ptr<TypeNode> targetTypeNode; // normally not null

  std::unique_ptr<ArgsNode> argsNode;

  const MethodHandle *handle{nullptr};

public:
  NewNode(unsigned int startPos, std::unique_ptr<TypeNode> &&targetTypeNode,
          std::unique_ptr<ArgsNode> &&argsNode)
      : WithRtti({startPos, 0}), targetTypeNode(std::move(targetTypeNode)),
        argsNode(std::move(argsNode)) {
    this->updateToken(this->argsNode->getToken());
  }

  explicit NewNode(std::unique_ptr<TypeNode> &&targetTypeNode)
      : WithRtti(targetTypeNode->getToken()), targetTypeNode(std::move(targetTypeNode)),
        argsNode(std::make_unique<ArgsNode>(this->getToken())) {}

  /**
   * for optional arguments
   * @param type
   */
  explicit NewNode(const OptionType &type)
      : WithRtti({0, 0}), argsNode(std::make_unique<ArgsNode>()) {
    this->setType(type);
  }

  TypeNode *getTargetTypeNode() const { return this->targetTypeNode.get(); }

  ArgsNode &getArgsNode() const { return *this->argsNode; }

  void setHandle(const MethodHandle *h) { this->handle = h; }

  const MethodHandle *getHandle() const { return this->handle; }

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
  EmbedNode(unsigned int startPos, Kind kind, std::unique_ptr<Node> &&exprNode, Token endToken)
      : WithRtti({startPos, 1}), kind(kind), exprNode(std::move(exprNode)) {
    this->updateToken(endToken);
  }

  EmbedNode(Kind kind, std::unique_ptr<Node> &&exprNode)
      : WithRtti(exprNode->getToken()), kind(kind), exprNode(std::move(exprNode)) {}

  ~EmbedNode() override = default;

  Kind getKind() const { return this->kind; }

  Node &getExprNode() const { return *this->exprNode; }

  std::unique_ptr<Node> &refExprNode() { return this->exprNode; }

  void setHandle(const MethodHandle *h) { this->handle = h; }

  const MethodHandle *getHandle() const { return this->handle; }

  void dump(NodeDumper &dumper) const override;
};

/**
 * for unary operator call
 */
class UnaryOpNode : public WithRtti<Node, NodeKind::UnaryOp> {
private:
  TokenKind op;

  Token opToken; // for line number

  /**
   * after call this->createApplyNode(), will be null.
   */
  std::unique_ptr<Node> exprNode;

  /**
   * before call this->createApplyNode(), it is null.
   */
  std::unique_ptr<ApplyNode> methodCallNode;

public:
  UnaryOpNode(TokenKind op, Token opToken, std::unique_ptr<Node> &&exprNode)
      : WithRtti(opToken), op(op), opToken(opToken), exprNode(std::move(exprNode)) {
    this->updateToken(this->exprNode->getToken());
  }

  UnaryOpNode(std::unique_ptr<Node> &&exprNode, TokenKind op, Token opToken)
      : WithRtti(exprNode->getToken()), op(op), opToken(opToken), exprNode(std::move(exprNode)) {
    this->updateToken(opToken);
  }

  Node *getExprNode() const { return this->exprNode.get(); }

  std::unique_ptr<Node> &refExprNode() { return this->exprNode; }

  TokenKind getOp() const { return this->op; }

  bool isUnwrapOp() const { return this->op == TokenKind::UNWRAP; }

  /**
   * create ApplyNode and set to this->applyNode.
   * exprNode will be null.
   */
  ApplyNode &createApplyNode() {
    this->methodCallNode = ApplyNode::newUnary(this->op, this->opToken, std::move(this->exprNode));
    return *this->methodCallNode;
  }

  /**
   * return null, before call this->createApplyNode().
   */
  ApplyNode *getApplyNode() const { return this->methodCallNode.get(); }

  std::unique_ptr<ApplyNode> &refApplyNode() { return this->methodCallNode; }

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
  std::unique_ptr<Node> leftNode;

  /**
   * will be null.
   */
  std::unique_ptr<Node> rightNode;

  TokenKind op;

  Token opToken;

  /**
   * initial value is null
   */
  std::unique_ptr<Node> optNode;

public:
  BinaryOpNode(std::unique_ptr<Node> &&leftNode, TokenKind op, Token opToken,
               std::unique_ptr<Node> &&rightNode)
      : WithRtti(leftNode->getToken()), leftNode(std::move(leftNode)),
        rightNode(std::move(rightNode)), op(op), opToken(opToken) {
    this->updateToken(this->rightNode->getToken());
  }

  /**
   *
   * @return
   * may be null
   */
  Node *getLeftNode() const { return this->leftNode.get(); }

  std::unique_ptr<Node> &refLeftNode() { return this->leftNode; }

  /**
   *
   * @return
   * may be null
   */
  Node *getRightNode() const { return this->rightNode.get(); }

  std::unique_ptr<Node> &refRightNode() { return this->rightNode; }

  TokenKind getOp() const { return this->op; }

  /**
   *
   * @return
   * may be null
   */
  Node *getOptNode() const { return this->optNode.get(); }

  void setOptNode(std::unique_ptr<Node> &&node) { this->optNode = std::move(node); }

  void createApplyNode() {
    auto applyNode = ApplyNode::newBinary(std::move(this->leftNode), this->op, this->opToken,
                                          std::move(this->rightNode));
    this->setOptNode(std::move(applyNode));
  }

  void dump(NodeDumper &dumper) const override;
};

/**
 * for command argument
 */
class CmdArgNode : public WithRtti<Node, NodeKind::CmdArg> {
private:
  unsigned int expansionSize{0};
  bool braceExpansion{false};
  std::vector<std::unique_ptr<Node>> segmentNodes; // at-least one element

public:
  explicit CmdArgNode(Token token) : WithRtti(token) {}

  explicit CmdArgNode(std::unique_ptr<Node> &&segmentNode) : WithRtti(segmentNode->getToken()) {
    this->addSegmentNode(std::move(segmentNode));
  }

  ~CmdArgNode() override = default;

  void addSegmentNode(std::unique_ptr<Node> &&node);

  const auto &getSegmentNodes() const { return this->segmentNodes; }

  auto &refSegmentNodes() { return this->segmentNodes; }

  void dump(NodeDumper &dumper) const override;

  bool isTilde() const { return this->isTildeAt(0); }

  bool isTildeAt(unsigned int i) const {
    return isa<StringNode>(*this->segmentNodes[i]) &&
           cast<StringNode>(*this->segmentNodes[i]).isTilde();
  }

  bool isBraceExpansion() const { return this->braceExpansion; }

  void setBraceExpansion(bool set) { this->braceExpansion = set; }

  unsigned int getExpansionSize() const { return this->expansionSize; }

  void setExpansionSize(unsigned int size) { this->expansionSize = size; }
};

class ArgArrayNode : public WithRtti<Node, NodeKind::ArgArray> {
private:
  std::vector<std::unique_ptr<CmdArgNode>> argNodes;

public:
  explicit ArgArrayNode(Token token) : WithRtti(token) {}

  void addCmdArgNode(std::unique_ptr<CmdArgNode> &&argNode) {
    this->argNodes.push_back(std::move(argNode));
  }

  const std::vector<std::unique_ptr<CmdArgNode>> &getCmdArgNodes() const { return this->argNodes; }

  void dump(NodeDumper &dumper) const override;
};

class RedirNode : public WithRtti<Node, NodeKind::Redir> {
private:
  std::string fdName;
  RedirOp op;
  int newFd{-1};
  int targetFd{-1};
  std::unique_ptr<CmdArgNode> targetNode;
  NameInfo hereStart;
  Token hereEnd;

public:
  RedirNode(TokenKind opKind, Token opToken, StringRef ref, std::unique_ptr<CmdArgNode> &&node)
      : WithRtti(opToken), targetNode(std::move(node)), hereStart({0, 0}, "") {
    auto pair = resolveRedirOp(opKind, ref);
    this->fdName = std::move(pair.first);
    this->op = pair.second;
    this->updateToken(this->targetNode->getToken());
  }

  ~RedirNode() override = default;

  const std::string &getFdName() const { return this->fdName; }

  void setNewFd(int fd) { this->newFd = fd; }

  int getNewFd() const { return this->newFd; }

  RedirOp getRedirOp() const { return this->op; }

  CmdArgNode &getTargetNode() { return *this->targetNode; }

  void setTargetFd(int fd) { this->targetFd = fd; }

  int getTargetFd() const { return this->targetFd; }

  void setHereStart(NameInfo &&name) { this->hereStart = std::move(name); }

  const NameInfo &getHereStart() const { return this->hereStart; }

  void setHereEnd(Token token) { this->hereEnd = token; }

  Token getHereEnd() { return this->hereEnd; }

  void dump(NodeDumper &dumper) const override;
};

class WildCardNode : public WithRtti<Node, NodeKind::WildCard> {
public:
  const ExpandMeta meta;
  bool expand{true};
  unsigned short braceId{0}; // up 85(255/3)

  WildCardNode(Token token, ExpandMeta p) : WithRtti(token), meta(p) {}

  ~WildCardNode() override = default;

  bool isExpand() const { return this->expand; }

  void setExpand(bool set) { this->expand = set; }

  bool isBraceMeta() const {
    switch (this->meta) {
    case ExpandMeta::BRACE_OPEN:
    case ExpandMeta::BRACE_CLOSE:
    case ExpandMeta::BRACE_SEP:
      return true;
    default:
      return false;
    }
  }

  unsigned short getBraceId() const { return this->braceId; }

  void setBraceId(unsigned short id) { this->braceId = id; }

  void dump(NodeDumper &dumper) const override;
};

inline bool isExpandingWildCard(const Node &node) {
  return isa<WildCardNode>(node) && cast<WildCardNode>(node).isExpand();
}

class BraceSeqNode : public WithRtti<Node, NodeKind::BraceSeq> {
private:
  BraceRange range;

public:
  BraceSeqNode(Token token, BraceRange::Kind k) : WithRtti(token) { this->range.kind = k; }

  void setRange(BraceRange r) { this->range = r; }

  const auto &getRange() const { return this->range; }

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

  bool needFork{true};

  HandlePtr handle;

public:
  explicit CmdNode(std::unique_ptr<StringNode> &&nameNode)
      : WithRtti(nameNode->getToken()), nameNode(std::move(nameNode)) {}

  ~CmdNode() override = default;

  StringNode &getNameNode() const { return *this->nameNode; }

  void addArgNode(std::unique_ptr<CmdArgNode> &&node);

  const std::vector<std::unique_ptr<Node>> &getArgNodes() const { return this->argNodes; }

  bool hasRedir() const { return this->redirCount > 0; }

  void setNeedFork(bool in) { this->needFork = in; }

  bool getNeedFork() const { return this->needFork; }

  void addRedirNode(std::unique_ptr<RedirNode> &&node);

  void setHandle(HandlePtr hd) { this->handle = std::move(hd); }

  const HandlePtr &getHandle() const { return this->handle; }

  void dump(NodeDumper &dumper) const override;
};

class PipelineNode : public WithRtti<Node, NodeKind::Pipeline> {
private:
  std::vector<std::unique_ptr<Node>> nodes;

  unsigned int baseIndex{0}; // for indicating internal pipeline state index
  bool inFork{false};

public:
  PipelineNode(std::unique_ptr<Node> &&leftNode, std::unique_ptr<Node> &&rightNode)
      : WithRtti(leftNode->getToken()) {
    this->addNode(std::move(leftNode));
    this->addNode(std::move(rightNode));
  }

  ~PipelineNode() override = default;

  void addNode(std::unique_ptr<Node> &&node);

  const std::vector<std::unique_ptr<Node>> &getNodes() const { return this->nodes; }

  void setBaseIndex(unsigned int index) { this->baseIndex = index; }

  unsigned int getBaseIndex() const { return this->baseIndex; }

  void setInFork(bool in) { this->inFork = in; }

  bool isLastPipe() const { return !this->inFork && !isa<CmdNode>(*this->nodes.back()); }

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
  WithNode(std::unique_ptr<Node> &&exprNode, std::unique_ptr<RedirNode> &&redirNode)
      : WithRtti(exprNode->getToken()), exprNode(std::move(exprNode)) {
    this->addRedirNode(std::move(redirNode));
  }

  ~WithNode() override = default;

  Node &getExprNode() const { return *this->exprNode; }

  void addRedirNode(std::unique_ptr<RedirNode> &&node) {
    this->updateToken(node->getToken());
    this->redirNodes.push_back(std::move(node));
  }

  const std::vector<std::unique_ptr<RedirNode>> &getRedirNodes() const { return this->redirNodes; }

  void setBaseIndex(unsigned int index) { this->baseIndex = index; }

  unsigned int getBaseIndex() const { return this->baseIndex; }

  void dump(NodeDumper &dumper) const override;
};

// for time expression
class TimeNode : public WithRtti<Node, NodeKind::Time> {
private:
  std::unique_ptr<Node> exprNode;

  unsigned int baseIndex{0};

public:
  TimeNode(Token token, std::unique_ptr<Node> &&exprNode)
      : WithRtti(token), exprNode(std::move(exprNode)) {
    this->updateToken(this->exprNode->getToken());
  }

  Node &getExprNode() const { return *this->exprNode; }

  void setBaseIndex(unsigned int index) { this->baseIndex = index; }

  unsigned int getBaseIndex() const { return this->baseIndex; }

  void dump(NodeDumper &dumper) const override;
};

class ForkNode : public WithRtti<Node, NodeKind::Fork> {
private:
  ForkKind opKind;
  std::unique_ptr<Node> exprNode;

public:
  ForkNode(Token token, ForkKind kind, std::unique_ptr<Node> &&exprNode, Token endToken)
      : WithRtti(token), opKind(kind), exprNode(std::move(exprNode)) {
    this->updateToken(endToken);
    if (isa<CmdNode>(*this->exprNode)) {
      cast<CmdNode>(*this->exprNode).setNeedFork(false);
    } else if (isa<PipelineNode>(*this->exprNode)) {
      cast<PipelineNode>(*this->exprNode).setInFork(true);
    }
  }

  static auto newCmdSubstitution(unsigned int pos, std::unique_ptr<Node> &&exprNode, Token token,
                                 bool strExpr) {
    return std::make_unique<ForkNode>(Token{pos, 1}, strExpr ? ForkKind::STR : ForkKind::ARRAY,
                                      std::move(exprNode), token);
  }

  static auto newProcSubstitution(unsigned int pos, std::unique_ptr<Node> &&exprNode, Token token,
                                  bool inPipe) {
    return std::make_unique<ForkNode>(
        Token{pos, 1}, inPipe ? ForkKind::IN_PIPE : ForkKind::OUT_PIPE, std::move(exprNode), token);
  }

  static auto newBackground(std::unique_ptr<Node> &&exprNode, Token token, bool disown) {
    auto tok = exprNode->getToken();
    return std::make_unique<ForkNode>(tok, disown ? ForkKind::DISOWN : ForkKind::JOB,
                                      std::move(exprNode), token);
  }

  static auto newCoproc(Token token, std::unique_ptr<Node> &&exprNode) {
    auto end = exprNode->getToken();
    return std::make_unique<ForkNode>(token, ForkKind::COPROC, std::move(exprNode), end);
  }

  ~ForkNode() override = default;

  ForkKind getOpKind() const { return this->opKind; }

  Node &getExprNode() const { return *this->exprNode; }

  bool isJob() const {
    switch (this->opKind) {
    case ForkKind::JOB:
    case ForkKind::COPROC:
    case ForkKind::DISOWN:
      return true;
    default:
      return false;
    }
  }

  bool isCmdSub() const {
    switch (this->opKind) {
    case ForkKind::STR:
    case ForkKind::ARRAY:
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
  std::unique_ptr<Node> condNode;

  std::unique_ptr<Node> messageNode;

public:
  AssertNode(unsigned int pos, std::unique_ptr<Node> &&condNode,
             std::unique_ptr<Node> &&messageNode)
      : WithRtti({pos, 1}), condNode(std::move(condNode)), messageNode(std::move(messageNode)) {
    this->updateToken(this->messageNode->getToken());
  }

  Node &getCondNode() const { return *this->condNode; }

  std::unique_ptr<Node> &refCondNode() { return this->condNode; }

  Node &getMessageNode() const { return *this->messageNode; }

  void dump(NodeDumper &dumper) const override;
};

class BlockNode : public WithRtti<Node, NodeKind::Block> {
private:
  std::vector<std::unique_ptr<Node>> nodes;
  unsigned short baseIndex{0};
  unsigned short varSize{0};
  unsigned short maxVarSize{0};
  short firstDeferOffset{-1};

public:
  explicit BlockNode(unsigned int startPos) : WithRtti({startPos, 1}) {}

  void addNode(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->nodes.push_back(std::move(node));
  }

  void insertNodeToFirst(std::unique_ptr<Node> &&node);

  const std::vector<std::unique_ptr<Node>> &getNodes() const { return this->nodes; }

  std::vector<std::unique_ptr<Node>> &refNodes() { return this->nodes; }

  unsigned int getBaseIndex() const { return this->baseIndex; }

  void setBaseIndex(unsigned int index) { this->baseIndex = index; }

  unsigned int getVarSize() const { return this->varSize; }

  void setVarSize(unsigned int size) { this->varSize = size; }

  unsigned int getMaxVarSize() const { return this->maxVarSize; }

  void setMaxVarSize(unsigned int size) { this->maxVarSize = size; }

  void setFirstDeferOffset(unsigned short offset) { this->firstDeferOffset = offset; }

  short getFirstDeferOffset() const { return this->firstDeferOffset; }

  void dump(NodeDumper &dumper) const override;
};

class TypeDefNode : public WithRtti<Node, NodeKind::TypeDef> {
public:
  const enum Kind : unsigned char {
    ALIAS,
    ERROR_DEF,
  } kind;

private:
  NameInfo nameInfo;
  std::unique_ptr<TypeNode> targetTypeNode; // for ALIAS, ERROR_DEF

  TypeDefNode(unsigned int startPos, NameInfo &&name, std::unique_ptr<TypeNode> &&targetTypeNode,
              Kind kind)
      : WithRtti({startPos, 1}), kind(kind), nameInfo(std::move(name)),
        targetTypeNode(std::move(targetTypeNode)) {
    this->updateToken(this->targetTypeNode->getToken());
  }

public:
  static std::unique_ptr<TypeDefNode> alias(unsigned int startPos, NameInfo &&alias,
                                            std::unique_ptr<TypeNode> &&targetTypeNode) {
    return std::unique_ptr<TypeDefNode>(
        new TypeDefNode(startPos, std::move(alias), std::move(targetTypeNode), ALIAS));
  }

  static std::unique_ptr<TypeDefNode> errorDef(unsigned int startPos, NameInfo &&name,
                                               std::unique_ptr<TypeNode> &&targetTypeNode) {
    return std::unique_ptr<TypeDefNode>(
        new TypeDefNode(startPos, std::move(name), std::move(targetTypeNode), ERROR_DEF));
  }

  ~TypeDefNode() override = default;

  const NameInfo &getNameInfo() const { return this->nameInfo; }

  const std::string &getName() const { return this->nameInfo.getName(); }

  Kind getDefKind() const { return this->kind; }

  TypeNode &getTargetTypeNode() const { return *this->targetTypeNode; }

  void dump(NodeDumper &dumper) const override;
};

class DeferNode : public WithRtti<Node, NodeKind::Defer> {
private:
  std::unique_ptr<BlockNode> blockNode;

  /**
   * in top-level, always 0
   */
  unsigned int dropLocalSize{0};

public:
  DeferNode(unsigned int pos, std::unique_ptr<BlockNode> &&blockNode)
      : WithRtti({pos, 1}), blockNode(std::move(blockNode)) {
    this->updateToken(this->blockNode->getToken());
  }

  BlockNode &getBlockNode() const { return *this->blockNode; }

  unsigned int getDropLocalOffset() const { return this->blockNode->getBaseIndex(); }

  unsigned int getDropLocalSize() const { return this->dropLocalSize; }

  void setDropLocalSize(unsigned int size) { this->dropLocalSize = size; }

  void dump(NodeDumper &dumper) const override;
};

/**
 * indicating for, while, do-while statement
 */
class LoopNode : public WithRtti<Node, NodeKind::Loop> {
private:
  std::unique_ptr<Node> initNode;

  /**
   * may be null
   */
  std::unique_ptr<Node> condNode;

  std::unique_ptr<Node> iterNode;

  std::unique_ptr<BlockNode> blockNode;

  bool asDoWhile;

public:
  /**
   * initNode may be null.
   * condNode may be null.
   * iterNode may be null.
   */
  LoopNode(unsigned int startPos, std::unique_ptr<Node> &&initNode,
           std::unique_ptr<Node> &&condNode, std::unique_ptr<Node> &&iterNode,
           std::unique_ptr<BlockNode> &&blockNode, bool asDoWhile = false);

  LoopNode(unsigned int startPos, std::unique_ptr<Node> &&condNode,
           std::unique_ptr<BlockNode> &&blockNode, bool asDoWhile = false)
      : LoopNode(startPos, nullptr, std::move(condNode), nullptr, std::move(blockNode), asDoWhile) {
  }

  Node &getInitNode() const { return *this->initNode; }

  std::unique_ptr<Node> &refInitNode() { return this->initNode; }

  /**
   *
   * @return
   * may be null
   */
  Node *getCondNode() const { return this->condNode.get(); }

  std::unique_ptr<Node> &refCondNode() { return this->condNode; }

  Node &getIterNode() const { return *this->iterNode; }

  std::unique_ptr<Node> &refIterNode() { return this->iterNode; }

  BlockNode &getBlockNode() const { return *this->blockNode; }

  bool isDoWhile() const { return this->asDoWhile; }

  void dump(NodeDumper &dumper) const override;
};

class IfNode : public WithRtti<Node, NodeKind::If> {
public:
  enum IfLetKind : unsigned char {
    NOP,
    ERROR,
    UNWRAP,
  };

private:
  const bool ifLet;
  IfLetKind ifLeftKind{NOP};
  const bool elif;
  std::unique_ptr<Node> condNode;
  std::unique_ptr<Node> thenNode;
  std::unique_ptr<Node> elseNode;

public:
  /**
   * elseNode may be null
   */
  IfNode(unsigned int startPos, bool elif, std::unique_ptr<Node> &&condNode,
         std::unique_ptr<Node> &&thenNode, std::unique_ptr<Node> &&elseNode, bool ifLet);

  Node &getCondNode() const { return *this->condNode; }

  std::unique_ptr<Node> &refCondNode() { return this->condNode; }

  Node &getThenNode() const { return *this->thenNode; }

  std::unique_ptr<Node> &refThenNode() { return this->thenNode; }

  Node &getElseNode() const { return *this->elseNode; }

  std::unique_ptr<Node> &refElseNode() { return this->elseNode; }

  bool isIfLet() const { return this->ifLet; }

  IfLetKind getIfLetKind() const { return this->ifLeftKind; }

  void setIfLetKind(IfLetKind k) { this->ifLeftKind = k; }

  /**
   * must be IfLetKind::UNWRAP
   * @return
   */
  Node &getIfLetUnwrap() const;

  bool isElif() const { return this->elif; }

  void dump(NodeDumper &dumper) const override;
};

class ArmNode : public WithRtti<Node, NodeKind::Arm> {
private:
  /**
   * if represents default pattern, size is 0
   */
  std::vector<std::unique_ptr<Node>> patternNodes;

  std::vector<std::unique_ptr<Node>> constPatternNodes;

  /**
   * initial value is null.
   */
  std::unique_ptr<Node> actionNode{nullptr};

public:
  explicit ArmNode(std::unique_ptr<Node> &&patternNode) : WithRtti(patternNode->getToken()) {
    this->addPatternNode(std::move(patternNode));
  }

  explicit ArmNode(unsigned int pos) : WithRtti({pos, 1}) {}

  void setActionNode(std::unique_ptr<Node> &&node) {
    this->actionNode = std::move(node);
    this->updateToken(this->actionNode->getToken());
  }

  void addPatternNode(std::unique_ptr<Node> &&node) {
    this->patternNodes.push_back(std::move(node));
  }

  const std::vector<std::unique_ptr<Node>> &getPatternNodes() const { return this->patternNodes; }

  const auto &getConstPatternNodes() const { return this->constPatternNodes; }

  void setConstPatternNodes(std::vector<std::unique_ptr<Node>> &&c) {
    this->constPatternNodes = std::move(c);
  }

  Node &getActionNode() const { return *this->actionNode; }

  std::unique_ptr<Node> &refActionNode() { return this->actionNode; }

  bool isDefault() const { return this->patternNodes.empty(); }

  void dump(NodeDumper &dumper) const override;
};

class CaseNode : public WithRtti<Node, NodeKind::Case> {
public:
  enum Kind : unsigned int {
    MAP = 0,
    IF_ELSE = 1,
  };

private:
  std::unique_ptr<Node> exprNode;
  std::vector<std::unique_ptr<ArmNode>> armNodes;
  Kind caseKind{MAP};

public:
  CaseNode(unsigned int pos, std::unique_ptr<Node> &&exprNode)
      : WithRtti({pos, 1}), exprNode(std::move(exprNode)) {}

  Node &getExprNode() const { return *this->exprNode; }

  void addArmNode(std::unique_ptr<ArmNode> &&armNode) {
    this->armNodes.push_back(std::move(armNode));
  }

  const std::vector<std::unique_ptr<ArmNode>> &getArmNodes() const { return this->armNodes; }

  void setCaseKind(Kind k) { this->caseKind = k; }

  Kind getCaseKind() const { return this->caseKind; }

  bool hasDefault() const;

  void dump(NodeDumper &dumper) const override;
};

class JumpNode : public WithRtti<Node, NodeKind::Jump> {
public:
  enum OpKind : unsigned char {
    BREAK,
    CONTINUE,
    THROW,
    RETURN,
    RETURN_INIT, // for constructor
  };

private:
  OpKind opKind;
  unsigned char fieldOffset{0}; // for constructor
  unsigned char fieldSize{0};   // for constructor
  unsigned int tryDepth{0};
  std::unique_ptr<Node> exprNode;

  JumpNode(Token token, OpKind kind, std::unique_ptr<Node> &&exprNode);

public:
  static std::unique_ptr<JumpNode> newBreak(Token token, std::unique_ptr<Node> exprNode) {
    return std::unique_ptr<JumpNode>(new JumpNode(token, BREAK, std::move(exprNode)));
  }

  static std::unique_ptr<JumpNode> newContinue(Token token) {
    return std::unique_ptr<JumpNode>(new JumpNode(token, CONTINUE, nullptr));
  }

  /**
   *
   * @param token
   * @param exprNode
   * not null
   * @return
   */
  static std::unique_ptr<JumpNode> newThrow(Token token, std::unique_ptr<Node> &&exprNode) {
    return std::unique_ptr<JumpNode>(new JumpNode(token, THROW, std::move(exprNode)));
  }

  /**
   *
   * @param token
   * @param exprNode
   * may be null
   * @return
   */
  static std::unique_ptr<JumpNode> newReturn(Token token, std::unique_ptr<Node> &&exprNode) {
    return std::unique_ptr<JumpNode>(new JumpNode(token, RETURN, std::move(exprNode)));
  }

  static std::unique_ptr<JumpNode> newReturnInit(const DSType &resolvedType, unsigned char offset,
                                                 unsigned char size) {
    std::unique_ptr<JumpNode> node(new JumpNode(Token{0, 0}, RETURN_INIT, nullptr));
    node->getExprNode().setType(resolvedType);
    node->fieldOffset = offset;
    node->fieldSize = size;
    return node;
  }

  OpKind getOpKind() const { return this->opKind; }

  Node &getExprNode() const { return *this->exprNode; }

  std::unique_ptr<Node> &refExprNode() { return this->exprNode; }

  void setTryDepth(unsigned int depth) { this->tryDepth = depth; }

  unsigned int getTryDepth() const { return this->tryDepth; }

  unsigned char getFieldOffset() const { return this->fieldOffset; }

  unsigned char getFieldSize() const { return this->fieldSize; }

  void dump(NodeDumper &dumper) const override;
};

class CatchNode : public WithRtti<Node, NodeKind::Catch> {
private:
  NameInfo exceptionName;
  std::unique_ptr<TypeNode> typeNode;

  unsigned int varIndex{0};

  std::unique_ptr<BlockNode> blockNode;

public:
  CatchNode(unsigned int startPos, NameInfo &&exceptionName, std::unique_ptr<TypeNode> &&typeNode,
            std::unique_ptr<BlockNode> &&blockNode)
      : WithRtti({startPos, 0}), exceptionName(std::move(exceptionName)),
        typeNode(typeNode != nullptr ? std::move(typeNode) : newErrorTypeNode()),
        blockNode(std::move(blockNode)) {
    this->updateToken(this->blockNode->getToken());
  }

  const NameInfo &getNameInfo() const { return this->exceptionName; }

  TypeNode &getTypeNode() const { return *this->typeNode; }

  void setAttribute(const Handle &handle) { this->varIndex = handle.getIndex(); }

  unsigned int getVarIndex() const { return this->varIndex; }

  BlockNode &getBlockNode() const { return *this->blockNode; }

  void dump(NodeDumper &dumper) const override;
};

class TryNode : public WithRtti<Node, NodeKind::Try> {
private:
  /**
   * initial value is BlockNode
   */
  std::unique_ptr<Node> exprNode;

  /**
   * may be empty
   */
  std::vector<std::unique_ptr<Node>> catchNodes;

  /**
   * may be null
   */
  std::unique_ptr<DeferNode> finallyNode;

public:
  TryNode(unsigned int startPos, std::unique_ptr<BlockNode> &&blockNode)
      : WithRtti({startPos, 0}), exprNode(std::move(blockNode)) {
    this->updateToken(this->exprNode->getToken());
  }

  Node &getExprNode() const { return *this->exprNode; }

  std::unique_ptr<Node> &refExprNode() { return this->exprNode; }

  void addCatchNode(std::unique_ptr<CatchNode> &&catchNode) {
    this->updateToken(catchNode->getToken());
    this->catchNodes.push_back(std::move(catchNode));
  }

  const std::vector<std::unique_ptr<Node>> &getCatchNodes() const { return this->catchNodes; }

  std::vector<std::unique_ptr<Node>> &refCatchNodes() { return this->catchNodes; }

  void addFinallyNode(std::unique_ptr<DeferNode> &&node) {
    this->finallyNode = std::move(node);
    this->updateToken(this->finallyNode->getToken());
  }

  /**
   * if has no finally block, return null
   */
  DeferNode *getFinallyNode() const { return this->finallyNode.get(); }

  void dump(NodeDumper &dumper) const override;
};

class VarDeclNode : public WithRtti<Node, NodeKind::VarDecl> {
public:
  enum Kind : unsigned char {
    VAR,
    LET,
    IMPORT_ENV,
    EXPORT_ENV,
  };

private:
  const Kind kind;
  NameInfo varName;

  /**
   * may be null
   */
  std::unique_ptr<Node> exprNode;

  HandlePtr handle;

public:
  VarDeclNode(unsigned int startPos, NameInfo &&varName, std::unique_ptr<Node> &&exprNode,
              Kind kind);

  const NameInfo &getNameInfo() const { return this->varName; }

  const std::string &getVarName() const { return this->varName.getName(); }

  Kind getKind() const { return this->kind; }

  void setHandle(HandlePtr ptr) { this->handle = std::move(ptr); }

  const HandlePtr &getHandle() const { return this->handle; }

  bool isGlobal() const { return this->handle->has(HandleAttr::GLOBAL); }

  /**
   * may be null
   */
  Node *getExprNode() const { return this->exprNode.get(); }

  /**
   * for param type inference
   * @param node
   */
  void setExprNode(std::unique_ptr<Node> &&node) { this->exprNode = std::move(node); }

  unsigned int getVarIndex() const { return this->handle->getIndex(); }

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
  std::unique_ptr<Node> leftNode;

  std::unique_ptr<Node> rightNode;
  flag8_set_t attributeSet{0};

public:
  static constexpr flag8_t SELF_ASSIGN = 1u << 0u;
  static constexpr flag8_t FIELD_ASSIGN = 1u << 1u;

  AssignNode(std::unique_ptr<Node> &&leftNode, std::unique_ptr<Node> &&rightNode,
             bool selfAssign = false)
      : WithRtti(leftNode->getToken()), leftNode(std::move(leftNode)),
        rightNode(std::move(rightNode)) {
    if (selfAssign) {
      setFlag(this->attributeSet, SELF_ASSIGN);
    }
    this->updateToken(this->rightNode->getToken());
  }

  Node &getLeftNode() const { return *this->leftNode; }

  Node &getRightNode() const { return *this->rightNode; }

  std::unique_ptr<Node> &refRightNode() { return this->rightNode; }

  void setAttribute(flag8_t flag) { setFlag(this->attributeSet, flag); }

  bool isSelfAssignment() const { return hasFlag(this->attributeSet, SELF_ASSIGN); }

  bool isFieldAssign() const { return hasFlag(this->attributeSet, FIELD_ASSIGN); }

  void dump(NodeDumper &dumper) const override;
};

class ElementSelfAssignNode : public WithRtti<Node, NodeKind::ElementSelfAssign> {
private:
  std::unique_ptr<Node> recvNode;
  std::unique_ptr<Node> indexNode;

  /**
   * receiver and argument are dummy node
   */
  std::unique_ptr<ApplyNode> getterNode;

  /**
   * receiver and argument are dummy node
   */
  std::unique_ptr<ApplyNode> setterNode;

  /**
   * before type checking, rightNode is BinaryOpNode.
   * after type checking, rightNode may be CastNode.
   * if rightNode is BinaryOpNode, left node is dummy node.
   */
  std::unique_ptr<Node> rightNode;

public:
  ElementSelfAssignNode(std::unique_ptr<ApplyNode> &&leftNode,
                        std::unique_ptr<BinaryOpNode> &&binaryNode);

  Node &getRecvNode() const { return *this->recvNode; }

  Node &getIndexNode() const { return *this->indexNode; }

  /**
   * may be BinaryOpNode or CastNode
   */
  Node &getRightNode() const { return *this->rightNode; }

  std::unique_ptr<Node> &refRightNode() { return this->rightNode; }

  ApplyNode &getGetterNode() const { return *this->getterNode; }

  ApplyNode &getSetterNode() const { return *this->setterNode; }

  /**
   * add recv type of getterNode and setterNode
   */
  void setRecvType(const DSType &type);

  /**
   * add index type of getterNode and setterNode.
   */
  void setIndexType(const DSType &type);

  void dump(NodeDumper &dumper) const override;
};

/**
 * AAA=VVV BBB=WWW do-something
 */
class PrefixAssignNode : public WithRtti<Node, NodeKind::PrefixAssign> {
private:
  std::vector<std::unique_ptr<AssignNode>> declNodes;

  /**
   * may be null
   */
  std::unique_ptr<Node> exprNode;

  unsigned int baseIndex{0};

public:
  PrefixAssignNode(std::vector<std::unique_ptr<AssignNode>> &&declNodes,
                   std::unique_ptr<Node> &&exprNode)
      : WithRtti(declNodes[0]->getToken()), declNodes(std::move(declNodes)),
        exprNode(std::move(exprNode)) {
    this->updateToken(this->declNodes.back()->getToken());
    if (this->exprNode) {
      this->updateToken(this->exprNode->getToken());
    }
  }

  const std::vector<std::unique_ptr<AssignNode>> &getAssignNodes() const { return this->declNodes; }

  const std::unique_ptr<Node> &getExprNode() const { return this->exprNode; }

  std::string toEnvCtxName() const {
    std::string name = "%%env";
    name += std::to_string(this->getPos());
    return name;
  }

  void setBaseIndex(unsigned int index) { this->baseIndex = index; }

  unsigned int getBaseIndex() const { return this->baseIndex; }

  unsigned int getVarSize() const { return this->declNodes.size() + 1; }

  void dump(NodeDumper &dumper) const override;
};

class FunctionNode : public WithRtti<Node, NodeKind::Function> {
public:
  const enum Kind : unsigned char {
    FUNC,
    SINGLE_EXPR,
    CONSTRUCTOR,
  } kind;

private:
  NameInfo funcName;

  /**
   * for parameter definition
   */
  std::vector<std::unique_ptr<VarDeclNode>> paramNodes;

  /**
   * may be null
   */
  std::unique_ptr<TypeNode> returnTypeNode;

  /**
   * for method definition. may be null
   */
  std::unique_ptr<TypeNode> recvTypeNode;

  std::unique_ptr<BlockNode> blockNode;

  /**
   * maximum number of local variable in function
   */
  unsigned int maxVarNum{0};

  /**
   * handle of this function
   */
  HandlePtr handle;

  const DSType *resolvedType{nullptr};

  /**
   * captured variables for local function
   */
  std::vector<HandlePtr> captures;

public:
  FunctionNode(unsigned int startPos, NameInfo &&funcName, Kind k = FUNC)
      : WithRtti({startPos, 0}), kind(k), funcName(std::move(funcName)) {}

  ~FunctionNode() override = default;

  const NameInfo &getNameInfo() const { return this->funcName; }

  const std::string &getFuncName() const { return this->funcName.getName(); }

  void addParamNode(NameInfo &&name, std::unique_ptr<TypeNode> &&paramType) {
    unsigned int pos = name.getToken().pos;
    this->paramNodes.push_back(std::make_unique<VarDeclNode>(
        pos, std::move(name), std::move(paramType), VarDeclNode::VAR));
  }

  const std::vector<std::unique_ptr<VarDeclNode>> &getParamNodes() const {
    return this->paramNodes;
  }

  void setReturnTypeNode(std::unique_ptr<TypeNode> &&typeToken) {
    this->returnTypeNode = std::move(typeToken);
    if (this->returnTypeNode) {
      this->updateToken(this->returnTypeNode->getToken());
    }
  }

  const std::unique_ptr<TypeNode> &getReturnTypeNode() const { return this->returnTypeNode; }

  void setRecvTypeNode(std::unique_ptr<TypeNode> &&typeToken) {
    this->recvTypeNode = std::move(typeToken);
    this->updateToken(this->recvTypeNode->getToken());
  }

  const std::unique_ptr<TypeNode> &getRecvTypeNode() const { return this->recvTypeNode; }

  void setFuncBody(std::unique_ptr<Node> &&node);

  /**
   * return null before call setBlockNode()
   */
  BlockNode &getBlockNode() const { return *this->blockNode; }

  void setMaxVarNum(unsigned int num) { this->maxVarNum = num; }

  unsigned int getMaxVarNum() const { return this->maxVarNum; }

  void setHandle(HandlePtr hd) { this->handle = std::move(hd); }

  const HandlePtr &getHandle() const { return this->handle; }

  void setResolvedType(const DSType &type) { this->resolvedType = &type; }

  /**
   *
   * @return
   * may be null.
   */
  const DSType *getResolvedType() const { return this->resolvedType; }

  void addCapture(HandlePtr hd) { this->captures.push_back(std::move(hd)); }

  const auto &getCaptures() const { return this->captures; }

  bool isAnonymousFunc() const {
    return this->funcName.getName().empty() && !this->isConstructor();
  }

  bool isSingleExpr() const { return this->kind == SINGLE_EXPR; }

  bool isConstructor() const { return this->kind == CONSTRUCTOR; }

  bool isNamedFunc() const { return this->kind == FUNC && !this->funcName.getName().empty(); }

  bool isMethod() const { return this->kind == FUNC && static_cast<bool>(this->getRecvTypeNode()); }

  void dump(NodeDumper &dumper) const override;
};

class UserDefinedCmdNode : public WithRtti<Node, NodeKind::UserDefinedCmd> {
private:
  NameInfo cmdName;

  HandlePtr handle;
  unsigned int maxVarNum{0};
  std::unique_ptr<TypeNode> returnTypeNode; // may be null
  std::unique_ptr<BlockNode> blockNode;

  /**
   * captured variables for anonymous command
   */
  std::vector<HandlePtr> captures;

public:
  UserDefinedCmdNode(unsigned int startPos, NameInfo &&commandName,
                     std::unique_ptr<TypeNode> &&returnTypeNode,
                     std::unique_ptr<BlockNode> &&blockNode)
      : WithRtti({startPos, 0}), cmdName(std::move(commandName)),
        returnTypeNode(std::move(returnTypeNode)), blockNode(std::move(blockNode)) {
    this->updateToken(this->blockNode->getToken());
  }

  ~UserDefinedCmdNode() override = default;

  const NameInfo &getNameInfo() const { return this->cmdName; }

  const std::string &getCmdName() const { return this->cmdName.getName(); }

  bool isAnonymousCmd() const { return this->getCmdName().empty(); }

  void setHandle(HandlePtr &&hd) { this->handle = std::move(hd); }

  const HandlePtr &getHandle() const { return this->handle; }

  BlockNode &getBlockNode() const { return *this->blockNode; }

  const std::unique_ptr<TypeNode> &getReturnTypeNode() const { return this->returnTypeNode; }

  void setMaxVarNum(unsigned int num) { this->maxVarNum = num; }

  unsigned int getMaxVarNum() const { return this->maxVarNum; }

  void addCapture(HandlePtr hd) { this->captures.push_back(std::move(hd)); }

  const auto &getCaptures() const { return this->captures; }

  void dump(NodeDumper &dumper) const override;
};

class FuncListNode : public WithRtti<Node, NodeKind::FuncList> {
private:
  /**
   * must be named function, method, user-defined command
   */
  std::vector<std::unique_ptr<Node>> nodes;

public:
  FuncListNode(std::unique_ptr<Node> &&first, std::unique_ptr<Node> &&second)
      : WithRtti(first->getToken()) {
    this->nodes.push_back(std::move(first));
    this->nodes.push_back(std::move(second));
    this->updateToken(this->nodes.back()->getToken());
  }

  void addNode(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->nodes.push_back(std::move(node));
  }

  const std::vector<std::unique_ptr<Node>> &getNodes() const { return this->nodes; }

  void dump(NodeDumper &dumper) const override;
};

class ModType;

class SourceNode : public WithRtti<Node, NodeKind::Source> {
private:
  std::shared_ptr<const CmdArgNode> pathNode;

  /**
   * may be null
   */
  std::shared_ptr<const NameInfo> name;

  /**
   * resolved module type.
   */
  const ModType &modType;

  /**
   * resolved module path
   */
  std::shared_ptr<const std::string> pathName;

  const bool firstAppear;

  const bool inlined;

  const unsigned short srcIndex;

  /**
   * maximum number of local variable in this module
   */
  unsigned int maxVarNum{0};

public:
  SourceNode(Token token, std::shared_ptr<CmdArgNode> pathNode,
             std::shared_ptr<const NameInfo> name, const ModType &modType,
             std::shared_ptr<const std::string> pathName, bool firstAppear, bool inlined,
             unsigned short srcIndex)
      : WithRtti(token), pathNode(std::move(pathNode)), name(std::move(name)), modType(modType),
        pathName(std::move(pathName)), firstAppear(firstAppear), inlined(inlined),
        srcIndex(srcIndex) {}

  ~SourceNode() override = default;

  const CmdArgNode &getPathNode() const { return *this->pathNode; }

  Token getPathToken() const { return this->getPathNode().getToken(); }

  const auto &getNameInfo() const { return this->name; }

  const ModType &getModType() const { return this->modType; }

  const std::string &getPathName() const { return *this->pathName; }

  bool isFirstAppear() const { return this->firstAppear; }

  bool isInlined() const { return this->inlined; }

  bool isUnreachable() const { return hasFlag(this->modType.getAttr(), ModAttr::UNREACHABLE); }

  unsigned short getSrcIndex() const { return this->srcIndex; }

  void setMaxVarNum(unsigned int v) { this->maxVarNum = v; }

  unsigned int getMaxVarNum() const { return this->maxVarNum; }

  void dump(NodeDumper &dumper) const override;
};

class SourceListNode : public WithRtti<Node, NodeKind::SourceList> {
private:
  std::shared_ptr<CmdArgNode> pathNode;

  std::vector<Node *> constNodes;

  /**
   * may be null
   */
  std::shared_ptr<const NameInfo> name;

  /**
   * if true, ignore module not found error
   */
  const bool optional;

  bool inlined{false};

  unsigned int curIndex{0}; // index of currently evaluating source path

  std::vector<std::shared_ptr<const std::string>> pathList; // evaluated path list

public:
  using path_iterator = decltype(constNodes.cbegin());

  SourceListNode(unsigned int pos, std::unique_ptr<CmdArgNode> &&pathNode, bool optional)
      : WithRtti({pos, 1}), pathNode(std::move(pathNode)), optional(optional) {
    this->updateToken(this->pathNode->getToken());
  }

  ~SourceListNode() override;

  CmdArgNode &getPathNode() const { return *this->pathNode; }

  const auto &getConstNodes() const { return this->constNodes; }

  void addConstNode(std::unique_ptr<Node> &&node) { this->constNodes.push_back(node.release()); }

  void setName(Token token, std::string &&value) {
    this->updateToken(token);
    this->name = std::make_shared<const NameInfo>(token, std::move(value));
  }

  const auto &getNameInfoPtr() const { return this->name; }

  bool isOptional() const { return this->optional; }

  bool isInlined() const { return this->inlined; }

  void setInlined(bool s) { this->inlined = s; }

  bool isExpansion() const {
    return this->getPathNode().getExpansionSize() > 0 && !this->getNameInfoPtr();
  }

  unsigned int getCurIndex() const { return this->curIndex; }

  void setCurIndex(unsigned int index) { this->curIndex = index; }

  void setPathList(std::vector<std::shared_ptr<const std::string>> &&list) {
    this->pathList = std::move(list);
  }

  const std::vector<std::shared_ptr<const std::string>> &getPathList() const {
    return this->pathList;
  }

  bool hasUnconsumedPath() const { return this->curIndex < this->pathList.size(); }

  std::unique_ptr<SourceNode> create(const ModType &modType, bool first) const {
    unsigned int index = this->curIndex - 1;
    return std::make_unique<SourceNode>(this->getToken(), this->pathNode, this->name, modType,
                                        this->pathList[index], first, this->isInlined(), index);
  }

  void dump(NodeDumper &dumper) const override;
};

class CodeCompNode : public WithRtti<Node, NodeKind::CodeComp> {
public:
  enum Kind : unsigned int {
    VAR,            // complete variable names
    VAR_IN_CMD_ARG, // complete variable names (within command argument)
    MEMBER,         // complete members (field or method)
    TYPE,           // complete type name (maybe type alias)
  };

private:
  std::unique_ptr<Node> exprNode;
  const Kind kind;

public:
  CodeCompNode(Kind kind, std::unique_ptr<Node> &&exprNode, Token tok)
      : WithRtti(tok), exprNode(std::move(exprNode)), kind(kind) {}

  const std::unique_ptr<Node> &getExprNode() const { return this->exprNode; }

  Token getTypingToken() const { return this->getToken(); }

  Kind getKind() const { return this->kind; }

  void dump(NodeDumper &dumper) const override;
};

class ErrorNode : public WithRtti<Node, NodeKind::Error> {
private:
  std::unique_ptr<Node> orgNode;

public:
  explicit ErrorNode(Token token) : WithRtti(token) {}

  explicit ErrorNode(std::unique_ptr<Node> &&node)
      : WithRtti(node->getToken()), orgNode(std::move(node)) {}

  ~ErrorNode() override = default;

  const std::unique_ptr<Node> &getOrgNode() const { return this->orgNode; }

  void dump(NodeDumper &dumper) const override;
};

class EmptyNode : public WithRtti<Node, NodeKind::Empty> {
public:
  EmptyNode() : EmptyNode({0, 0}) {}
  explicit EmptyNode(Token token) : WithRtti(token) {}
  ~EmptyNode() override = default;

  void dump(NodeDumper &dumper) const override;
};

// helper function for node creation
std::unique_ptr<LoopNode> createForInNode(unsigned int startPos, NameInfo &&keyName,
                                          NameInfo &&valueName, std::unique_ptr<Node> &&exprNode,
                                          std::unique_ptr<BlockNode> &&blockNode);

std::unique_ptr<Node> createAssignNode(std::unique_ptr<Node> &&leftNode, TokenKind op, Token token,
                                       std::unique_ptr<Node> &&rightNode);

inline std::unique_ptr<Node> createSuffixNode(std::unique_ptr<Node> &&leftNode, TokenKind op,
                                              Token token) {
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
  return cast<T>(findInnerNode(T::KIND, node));
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
  virtual void visitArgsNode(ArgsNode &node) = 0;
  virtual void visitApplyNode(ApplyNode &node) = 0;
  virtual void visitEmbedNode(EmbedNode &node) = 0;
  virtual void visitNewNode(NewNode &node) = 0;
  virtual void visitForkNode(ForkNode &node) = 0;
  virtual void visitCmdNode(CmdNode &node) = 0;
  virtual void visitCmdArgNode(CmdArgNode &node) = 0;
  virtual void visitArgArrayNode(ArgArrayNode &node) = 0;
  virtual void visitRedirNode(RedirNode &node) = 0;
  virtual void visitWildCardNode(WildCardNode &node) = 0;
  virtual void visitBraceSeqNode(BraceSeqNode &node) = 0;
  virtual void visitPipelineNode(PipelineNode &node) = 0;
  virtual void visitWithNode(WithNode &node) = 0;
  virtual void visitTimeNode(TimeNode &node) = 0;
  virtual void visitAssertNode(AssertNode &node) = 0;
  virtual void visitBlockNode(BlockNode &node) = 0;
  virtual void visitTypeDefNode(TypeDefNode &node) = 0;
  virtual void visitDeferNode(DeferNode &node) = 0;
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
  virtual void visitPrefixAssignNode(PrefixAssignNode &node) = 0;
  virtual void visitFunctionNode(FunctionNode &node) = 0;
  virtual void visitUserDefinedCmdNode(UserDefinedCmdNode &node) = 0;
  virtual void visitFuncListNode(FuncListNode &node) = 0;
  virtual void visitSourceNode(SourceNode &node) = 0;
  virtual void visitSourceListNode(SourceListNode &node) = 0;
  virtual void visitCodeCompNode(CodeCompNode &node) = 0;
  virtual void visitErrorNode(ErrorNode &node) = 0;
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
  void visitArgsNode(ArgsNode &node) override { this->visitDefault(node); }
  void visitApplyNode(ApplyNode &node) override { this->visitDefault(node); }
  void visitNewNode(NewNode &node) override { this->visitDefault(node); }
  void visitEmbedNode(EmbedNode &node) override { this->visitDefault(node); }
  void visitCmdNode(CmdNode &node) override { this->visitDefault(node); }
  void visitCmdArgNode(CmdArgNode &node) override { this->visitDefault(node); }
  void visitArgArrayNode(ArgArrayNode &node) override { this->visitDefault(node); }
  void visitRedirNode(RedirNode &node) override { this->visitDefault(node); }
  void visitWildCardNode(WildCardNode &node) override { this->visitDefault(node); }
  void visitBraceSeqNode(BraceSeqNode &node) override { this->visitDefault(node); }
  void visitPipelineNode(PipelineNode &node) override { this->visitDefault(node); }
  void visitWithNode(WithNode &node) override { this->visitDefault(node); }
  void visitTimeNode(TimeNode &node) override { this->visitDefault(node); }
  void visitForkNode(ForkNode &node) override { this->visitDefault(node); }
  void visitAssertNode(AssertNode &node) override { this->visitDefault(node); }
  void visitBlockNode(BlockNode &node) override { this->visitDefault(node); }
  void visitTypeDefNode(TypeDefNode &node) override { this->visitDefault(node); }
  void visitDeferNode(DeferNode &node) override { this->visitDefault(node); }
  void visitLoopNode(LoopNode &node) override { this->visitDefault(node); }
  void visitIfNode(IfNode &node) override { this->visitDefault(node); }
  void visitCaseNode(CaseNode &node) override { this->visitDefault(node); }
  void visitArmNode(ArmNode &node) override { this->visitDefault(node); }
  void visitJumpNode(JumpNode &node) override { this->visitDefault(node); }
  void visitCatchNode(CatchNode &node) override { this->visitDefault(node); }
  void visitTryNode(TryNode &node) override { this->visitDefault(node); }
  void visitVarDeclNode(VarDeclNode &node) override { this->visitDefault(node); }
  void visitAssignNode(AssignNode &node) override { this->visitDefault(node); }
  void visitElementSelfAssignNode(ElementSelfAssignNode &node) override {
    this->visitDefault(node);
  }
  void visitPrefixAssignNode(PrefixAssignNode &node) override { this->visitDefault(node); }
  void visitFunctionNode(FunctionNode &node) override { this->visitDefault(node); }
  void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override { this->visitDefault(node); }
  void visitFuncListNode(FuncListNode &node) override { this->visitDefault(node); }
  void visitSourceNode(SourceNode &node) override { this->visitDefault(node); }
  void visitSourceListNode(SourceListNode &node) override { this->visitDefault(node); }
  void visitCodeCompNode(CodeCompNode &node) override { this->visitDefault(node); }
  void visitErrorNode(ErrorNode &node) override { this->visitDefault(node); }
  void visitEmptyNode(EmptyNode &node) override { this->visitDefault(node); }
};

class NameScope;

class NodeDumper {
private:
  FILE *fp;

  struct DumpBuffer {
    unsigned int indentLevel{0};
    std::string value;
  };

  std::list<DumpBuffer> bufs;

public:
  explicit NodeDumper(FILE *fp) : fp(fp) {}

  ~NodeDumper() = default;

  /**
   * dump field
   */
  void dump(const char *fieldName, const char *value) { this->dumpEscaped(fieldName, value); }

  void dump(const char *fieldName, const std::string &value) {
    this->dump(fieldName, value.c_str());
  }

  void dump(const char *fieldName, bool value) {
    this->dumpRaw(fieldName, value ? "true" : "false");
  }

  template <typename T, enable_when<std::is_arithmetic<T>::value> = nullptr>
  void dump(const char *fieldName, T value) {
    this->dumpRaw(fieldName, std::to_string(value).c_str());
  }

  template <typename T, enable_when<std::is_convertible<T *, Node *>::value> = nullptr>
  void dump(const char *fieldName, const std::vector<std::unique_ptr<T>> &nodes) {
    this->dumpNodesHead(fieldName);
    for (auto &e : nodes) {
      this->dumpNodesBody(*e);
    }
    this->dumpNodesTail();
  }

  template <typename T, enable_when<std::is_convertible<T *, Node *>::value> = nullptr>
  void dump(const char *fieldName, const std::vector<T *> &nodes) {
    this->dumpNodesHead(fieldName);
    for (auto &e : nodes) {
      this->dumpNodesBody(*e);
    }
    this->dumpNodesTail();
  }

  void dump(const char *fieldName, const std::vector<std::string> &values) {
    this->dumpNodesHead(fieldName);
    for (auto &e : values) {
      this->indent();
      this->append("- ");
      this->appendEscaped(e.c_str());
      this->newline();
    }
    this->dumpNodesTail();
  }

  void dump(const char *fieldName, const std::vector<NameInfo> &values) {
    this->dumpNodesHead(fieldName);
    for (auto &e : values) {
      this->indent();
      this->append("- ");
      this->enterIndent();
      this->dumpToken(e.getToken());
      this->dump("name", e.getName());
      this->leaveIndent();
    }
    this->dumpNodesTail();
  }

  /**
   * dump node with indent
   */
  void dump(const char *fieldName, const Node &node);

  void dump(const char *fieldName, const DSType &type);

  void dump(const char *fieldName, TokenKind kind);

  void dump(const char *fieldName, const Handle &handle);

  void dump(const char *fieldName, const MethodHandle &handle);

  void dump(const char *fieldName, const NameInfo &info);

  void dumpNull(const char *fieldName) { this->dumpRaw(fieldName, "null"); }

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

  void finalize(const NameScope &scope);

  explicit operator bool() const { return this->fp != nullptr; }

private:
  void dumpRaw(const char *fieldName, const char *value) {
    this->writeName(fieldName);
    this->append(' ');
    this->append(value);
    this->newline();
  }

  void dumpEscaped(const char *fieldName, const char *value) {
    this->writeName(fieldName);
    this->append(' ');
    this->appendEscaped(value);
    this->newline();
  }

  void enterIndent() { this->bufs.back().indentLevel++; }

  void leaveIndent() { this->bufs.back().indentLevel--; }

  void indent();

  void newline() { this->append('\n'); }

  std::string &buf() { return this->bufs.back().value; }

  void append(int ch);

  void append(const char *str);

  void appendEscaped(const char *value);

  void dumpNodeHeader(const Node &node, bool inArray = false);

  void dumpToken(Token token);

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

  void dumpNodesTail() { this->leaveIndent(); }

  void writeName(const char *fieldName);
};

} // namespace ydsh

#endif // YDSH_NODE_H
