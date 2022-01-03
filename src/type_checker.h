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

#include "lexer.h"
#include "misc/buffer.hpp"
#include "misc/hash.hpp"
#include "node.h"
#include "scope.h"
#include "tcerror.h"

namespace ydsh {

enum class CoercionKind : unsigned char {
  PERFORM_COERCION,
  INVALID_COERCION, // illegal coercion.
  NOP,              // not allow coercion
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
  FlowContext() : stacks({{0, 0, 0, 0}}) {}

  unsigned int tryCatchLevel() const { return this->stacks.back().tryLevel; }

  /**
   *
   * @return
   * finally block depth. (if 0, outside finally block)
   */
  unsigned int finallyLevel() const { return this->stacks.back().finallyLevel; }

  /**
   *
   * @return
   * loop block depth. (if 0, outside loop block)
   */
  unsigned int loopLevel() const { return this->stacks.back().loopLevel; }

  /**
   *
   * @return
   * child process depth. (if 0, parent)
   */
  unsigned int childLevel() const { return this->stacks.back().childLevel; }

  void clear() {
    this->stacks.clear();
    this->stacks += {0, 0, 0, 0};
  }

  void leave() { this->stacks.pop_back(); }

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
  FlexBuffer<JumpNode *> jumpNodes;

public:
  void addJumpNode(JumpNode *node) { this->jumpNodes.push_back(node); }

  /**
   * call after enter()
   * @return
   */
  const FlexBuffer<JumpNode *> &getJumpNodes() const { return this->jumpNodes; }
};

class FuncContext {
private:
  unsigned int voidReturnCount{0};
  const DSType *returnType;
  FlexBuffer<JumpNode *> returnNodes;

  FlowContext flow;

  std::vector<BreakGather> breakGathers;

  std::unique_ptr<FuncContext> parent;

public:
  FuncContext() : returnType(nullptr) {}

  explicit FuncContext(const DSType *type, std::unique_ptr<FuncContext> &&parent)
      : returnType(type), parent(std::move(parent)) {}

  std::unique_ptr<FuncContext> takeParent() && { return std::move(this->parent); }

  bool withinFunc() const { return static_cast<bool>(this->parent); }

  void clear() {
    this->returnType = nullptr;
    this->voidReturnCount = 0;
    this->returnNodes.clear();
    this->flow.clear();
    this->breakGathers.clear();
    this->parent = nullptr;
  }

  void addReturnNode(JumpNode *node) {
    if (node->getExprNode().getType().isVoidType()) {
      this->voidReturnCount++;
    }
    this->returnNodes.push_back(node);
  }

  /**
   * call after enter()
   * @return
   */
  const FlexBuffer<JumpNode *> &getReturnNodes() const { return this->returnNodes; }

  const DSType *getReturnType() const { return this->returnType; }

  unsigned int getVoidReturnCount() const { return this->voidReturnCount; }

  void addJumpNode(JumpNode *node) { this->breakGathers.back().addJumpNode(node); }

  const FlexBuffer<JumpNode *> &getJumpNodes() const {
    return this->breakGathers.back().getJumpNodes();
  }

  unsigned int tryCatchLevel() const { return this->flow.tryCatchLevel(); }

  /**
   *
   * @return
   * finally block depth. (if 0, outside finally block)
   */
  unsigned int finallyLevel() const { return this->flow.finallyLevel(); }

  /**
   *
   * @return
   * loop block depth. (if 0, outside loop block)
   */
  unsigned int loopLevel() const { return this->flow.loopLevel(); }

  /**
   *
   * @return
   * child process depth. (if 0, parent)
   */
  unsigned int childLevel() const { return this->flow.childLevel(); }

  auto intoLoop() {
    this->flow.enterLoop();
    this->breakGathers.emplace_back();
    return finally([&] {
      this->flow.leave();
      this->breakGathers.pop_back();
    });
  }

  auto intoChild() {
    this->flow.enterChild();
    return finally([&] { this->flow.leave(); });
  }

  auto intoTry() {
    this->flow.enterTry();
    return finally([&] { this->flow.leave(); });
  }

  auto intoFinally() {
    this->flow.enterFinally();
    return finally([&] { this->flow.leave(); });
  }
};

class CodeCompletionHandler;

class TypeChecker : protected NodeVisitor {
protected:
  TypePool &typePool;

  NameScopePtr curScope;

  int visitingDepth{0};

  bool toplevelPrinting;

  bool reachComp{false};

  std::unique_ptr<FuncContext> funcCtx;

  ObserverPtr<const Lexer> lexer;

  ObserverPtr<CodeCompletionHandler> ccHandler;

  std::vector<TypeCheckError> errors;

public:
  TypeChecker(TypePool &pool, bool toplevelPrinting, const Lexer *lex)
      : typePool(pool), toplevelPrinting(toplevelPrinting),
        funcCtx(std::make_unique<FuncContext>()), lexer(lex) {}

  ~TypeChecker() override = default;

  std::unique_ptr<Node> operator()(const DSType *prevType, std::unique_ptr<Node> &&node,
                                   NameScopePtr global);

  TypePool &getTypePool() { return this->typePool; }

  void setCodeCompletionHandler(ObserverPtr<CodeCompletionHandler> handler) {
    this->ccHandler = handler;
  }

  bool hasReachedCompNode() const { return this->reachComp; }

  const std::vector<TypeCheckError> &getErrors() const { return this->errors; }

  bool hasError() const { return !this->errors.empty(); }

protected:
  // base type check entry point

  /**
   * check node type.
   * if node type is void type, throw exception.
   * return resolved type.
   */
  const DSType &checkTypeAsExpr(Node &targetNode) {
    return this->checkType(nullptr, targetNode, &this->typePool.get(TYPE::Void));
  }

  /**
   * check node type. not allow Void and Nothing type
   * @param targetNode
   * @return
   */
  const DSType &checkTypeAsSomeExpr(Node &targetNode);

  /**
   * check node type
   *
   * if requiredType is not equivalent to node type, throw exception.
   * return resolved type.
   */
  const DSType &checkType(const DSType &requiredType, Node &targetNode) {
    return this->checkType(&requiredType, targetNode, nullptr);
  }

  /**
   * only call visitor api (not perform additional type checking)
   * @param targetNode
   * @return
   */
  const DSType &checkTypeExactly(Node &targetNode) {
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
  const DSType &checkType(const DSType *requiredType, Node &targetNode,
                          const DSType *unacceptableType) {
    CoercionKind kind = CoercionKind::NOP;
    return this->checkType(requiredType, targetNode, unacceptableType, kind);
  }

  /**
   * root method of checkType
   */
  const DSType &checkType(const DSType *requiredType, Node &targetNode,
                          const DSType *unacceptableType, CoercionKind &kind);

private:
  TypeOrError toType(TypeNode &node);

  void checkTypeWithCurrentScope(BlockNode &blockNode) {
    this->checkTypeWithCurrentScope(&this->typePool.get(TYPE::Void), blockNode);
  }

  void checkTypeWithCurrentScope(const DSType *requiredType, BlockNode &blockNode);

  /**
   * after type checking.
   * requiredType is not null.
   * if requiredType is FloatType and targetNode->getType() is IntType,
   * wrap targetNode with CastNode.
   * if requiredType is VoidType, wrap targetNode with CastNode
   */
  void checkTypeWithCoercion(const DSType &requiredType, std::unique_ptr<Node> &targetNode);

  /**
   * for int type conversion.
   * return true if allow target type to required type implicit cast.
   */
  bool checkCoercion(const DSType &requiredType, const DSType &targetType);

  void resolveCoercion(const DSType &requiredType, std::unique_ptr<Node> &targetNode) {
    targetNode = newTypedCastNode(std::move(targetNode), requiredType);
    this->resolveCastOp(cast<TypeOpNode>(*targetNode));
  }

  const DSType &resolveCoercionOfJumpValue(const FlexBuffer<JumpNode *> &jumpNodes,
                                           bool optional = true);

  /**
   *
   * @param node
   * @param symbolName
   * @param type
   * @param attribute
   * @return
   * if can not add entry, return null
   */
  const Handle *addEntry(const Node &node, const std::string &symbolName, const DSType &type,
                         HandleAttr attribute) {
    return this->addEntry(node.getToken(), symbolName, type, attribute);
  }

  const Handle *addEntry(const NameInfo &info, const DSType &type, HandleAttr attribute) {
    return this->addEntry(info.getToken(), info.getName(), type, attribute);
  }

  /**
   *
   * @param token
   * @param symbolName
   * @param type
   * @param attribute
   * @return
   * if can not add entry, return null
   */
  const Handle *addEntry(Token token, const std::string &symbolName, const DSType &type,
                         HandleAttr attribute);

  /**
   *
   * @param node
   * @return
   * if can not add entry, return null
   */
  const Handle *addUdcEntry(const UserDefinedCmdNode &node);

  bool isTopLevel() const { return this->visitingDepth == 1; }

  auto intoBlock() {
    this->curScope = this->curScope->enterScope(NameScope::BLOCK);
    return finally([&] { this->curScope = this->curScope->exitScope(); });
  }

  auto intoFunc(const DSType *returnType) {
    this->curScope = this->curScope->enterScope(NameScope::FUNC);
    this->curScope = this->curScope->enterScope(NameScope::BLOCK);
    this->funcCtx = std::make_unique<FuncContext>(returnType, std::move(this->funcCtx));
    return finally([&] {
      this->curScope = this->curScope->exitScope();
      this->curScope = this->curScope->exitScope();
      this->funcCtx = std::move(*this->funcCtx).takeParent();
    });
  }

  void reportErrorImpl(Token token, const char *kind, const char *fmt, ...)
      __attribute__((format(printf, 4, 5)));

  template <typename T, typename... Arg, typename = base_of_t<T, TCError>>
  void reportError(const Node &node, Arg &&...arg) {
    this->reportErrorImpl(node.getToken(), T::kind, T::value, std::forward<Arg>(arg)...);
  }

  template <typename T, typename... Arg, typename = base_of_t<T, TCError>>
  void reportError(Token token, Arg &&...arg) {
    this->reportErrorImpl(token, T::kind, T::value, std::forward<Arg>(arg)...);
  }

  void reportError(Token token, TypeLookupError &&e) {
    this->errors.emplace_back(token, std::move(e));
  }

  // for apply node type checking
  /**
   * check type ApplyNode and resolve callee(handle or function type).
   */
  CallableTypes resolveCallee(ApplyNode &node);

  bool checkAccessNode(AccessNode &node);

  void checkArgsNode(const CallableTypes &types, ArgsNode &node);

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
  std::unique_ptr<Node> newPrintOpNode(std::unique_ptr<Node> &&node);

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
    const DSType *type{nullptr};

  public:
    bool hasElsePattern() const { return this->elsePattern; }

    void setElsePattern(bool set) { this->elsePattern = set; }

    void setKind(CaseNode::Kind k) { this->kind = k; }

    auto getKind() const { return this->kind; }

    void setType(const DSType *t) { this->type = t; }

    const DSType *getType() const { return this->type; }

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
   * @param node
   * for error position
   * @param types
   * @param fallbackType
   * may be null
   * @return
   * if not resolve common suprt type,
   *   if fallbackType is not null, return fallbackType.
   *   if fallbackType is null, return Nothing and report error
   */
  const DSType &resolveCommonSuperType(const Node &node, std::vector<const DSType *> &&types,
                                       const DSType *fallbackType);

  /**
   * evaluate constant expression
   * @param node
   * must be typed node
   * @return
   * final evaluated value
   */
  std::unique_ptr<Node> evalConstant(const Node &node);

  /**
   * apply constant folding and generate source path list.
   * if cannot resolve path, throw error.
   * @param node
   */
  void resolvePathList(SourceListNode &node);

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
  void visitArgsNode(ArgsNode &node) override;
  void visitApplyNode(ApplyNode &node) override;
  void visitNewNode(NewNode &node) override;
  void visitEmbedNode(EmbedNode &node) override;
  void visitCmdNode(CmdNode &node) override;
  void visitCmdArgNode(CmdArgNode &node) override;
  void visitArgArrayNode(ArgArrayNode &node) override;
  void visitRedirNode(RedirNode &node) override;
  void visitWildCardNode(WildCardNode &node) override;
  void visitPipelineNode(PipelineNode &node) override;
  void visitWithNode(WithNode &node) override;
  void visitForkNode(ForkNode &node) override;
  void visitAssertNode(AssertNode &node) override;
  void visitBlockNode(BlockNode &node) override;
  void visitTypeDefNode(TypeDefNode &node) override;
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
  void visitPrefixAssignNode(PrefixAssignNode &node) override;
  void visitFunctionNode(FunctionNode &node) override;
  void visitInterfaceNode(InterfaceNode &node) override;
  void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
  void visitSourceNode(SourceNode &node) override;
  void visitSourceListNode(SourceListNode &node) override;
  void visitCodeCompNode(CodeCompNode &node) override;
  void visitErrorNode(ErrorNode &node) override;
  void visitEmptyNode(EmptyNode &node) override;
};

} // namespace ydsh

#endif // YDSH_TYPE_CHECKER_H
