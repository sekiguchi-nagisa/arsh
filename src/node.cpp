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

#include "node.h"
#include "misc/format.hpp"
#include "misc/num_util.hpp"
#include "scope.h"

// helper macro for node dumper
#define NAME(f) #f

#define DUMP(field) dumper.dump(NAME(field), field)
#define DUMP_PTR(field)                                                                            \
  do {                                                                                             \
    if ((field) == nullptr) {                                                                      \
      dumper.dumpNull(NAME(field));                                                                \
    } else {                                                                                       \
      dumper.dump(NAME(field), *(field));                                                          \
    }                                                                                              \
  } while (false)

// not directly use it.
#define GEN_ENUM_STR(ENUM)                                                                         \
  case ENUM:                                                                                       \
    ___str__ = #ENUM;                                                                              \
    break;

#define DUMP_ENUM(val, EACH_ENUM)                                                                  \
  do {                                                                                             \
    const char *___str__ = nullptr;                                                                \
    switch (val) { EACH_ENUM(GEN_ENUM_STR) }                                                       \
    dumper.dump(NAME(val), ___str__);                                                              \
  } while (false)

// not directly use it.
#define GEN_FLAG_STR(FLAG)                                                                         \
  if ((___set__ & (FLAG))) {                                                                       \
    if (___count__++ > 0) {                                                                        \
      ___str__ += " | ";                                                                           \
    }                                                                                              \
    ___str__ += #FLAG;                                                                             \
  }

#define DUMP_BITSET(val, EACH_FLAG)                                                                \
  do {                                                                                             \
    unsigned int ___count__ = 0;                                                                   \
    std::string ___str__;                                                                          \
    auto ___set__ = val;                                                                           \
    EACH_FLAG(GEN_FLAG_STR)                                                                        \
    dumper.dump(NAME(val), ___str__);                                                              \
  } while (false)

namespace ydsh {

// ##################
// ##     Node     ##
// ##################

void Node::accept(NodeVisitor &visitor) {
  switch (this->nodeKind) {
#define DISPATCH(K)                                                                                \
  case NodeKind::K:                                                                                \
    visitor.visit##K##Node(*cast<K##Node>(this));                                                  \
    break;
    EACH_NODE_KIND(DISPATCH)
#undef DISPATCH
  }
}

// ######################
// ##     TypeNode     ##
// ######################

void TypeNode::dump(NodeDumper &dumper) const { DUMP_ENUM(typeKind, EACH_TYPE_NODE_KIND); }

// ##########################
// ##     BaseTypeNode     ##
// ##########################

void BaseTypeNode::dump(NodeDumper &dumper) const {
  TypeNode::dump(dumper);
  DUMP(typeName);
  DUMP_PTR(handle);
}

// ###############################
// ##     QualifiedTypeNode     ##
// ###############################

void QualifiedTypeNode::dump(NodeDumper &dumper) const {
  TypeNode::dump(dumper);
  DUMP_PTR(recvTypeNode);
  DUMP_PTR(nameTypeNode);
}

// #############################
// ##     ReifiedTypeNode     ##
// #############################

void ReifiedTypeNode::dump(NodeDumper &dumper) const {
  TypeNode::dump(dumper);
  DUMP_PTR(templateTypeNode);
  DUMP(elementTypeNodes);
}

// ##########################
// ##     FuncTypeNode     ##
// ##########################

void FuncTypeNode::dump(NodeDumper &dumper) const {
  TypeNode::dump(dumper);
  DUMP_PTR(returnTypeNode);
  DUMP(paramTypeNodes);
}

// ########################
// ##     TypeOfNode     ##
// ########################

void TypeOfNode::dump(NodeDumper &dumper) const {
  TypeNode::dump(dumper);
  DUMP_PTR(exprNode);
}

// ########################
// ##     NumberNode     ##
// ########################

void NumberNode::dump(NodeDumper &dumper) const {
  DUMP_ENUM(kind, EACH_NUMBER_NODE_KIND);
  DUMP(init);
  switch (this->kind) {
  case Int:
  case Signal:
  case Bool:
  case None:
    DUMP(intValue);
    break;
  case Float:
    DUMP(floatValue);
    break;
  }
}

// #######################
// ##     StringNode    ##
// #######################

void StringNode::dump(NodeDumper &dumper) const {
#define EACH_ENUM(OP)                                                                              \
  OP(STRING)                                                                                       \
  OP(TILDE)                                                                                        \
  OP(BACKQUOTE)

  DUMP_ENUM(kind, EACH_ENUM);

#undef EACH_ENUM

  DUMP(init);
  DUMP(value);
}

// ############################
// ##     StringExprNode     ##
// ############################

void StringExprNode::dump(NodeDumper &dumper) const { DUMP(nodes); }

// #######################
// ##     RegexNode     ##
// #######################

bool RegexNode::buildRegex(std::string &errorStr) {
  if (auto flag = PCRE::parseCompileFlag(this->reFlag, errorStr); flag.hasValue()) {
    this->re = PCRE::compile(StringRef(this->reStr), flag.unwrap(), errorStr);
  }
  return static_cast<bool>(this->re);
}

void RegexNode::dump(NodeDumper &dumper) const {
  DUMP(reStr);
  DUMP(reFlag);
}

// #######################
// ##     ArrayNode     ##
// #######################

void ArrayNode::dump(NodeDumper &dumper) const { DUMP(nodes); }

// #####################
// ##     MapNode     ##
// #####################

void MapNode::addEntry(std::unique_ptr<Node> &&keyNode, std::unique_ptr<Node> &&valueNode) {
  this->keyNodes.push_back(std::move(keyNode));
  this->valueNodes.push_back(std::move(valueNode));
}

void MapNode::dump(NodeDumper &dumper) const {
  DUMP(keyNodes);
  DUMP(valueNodes);
}

// #######################
// ##     TupleNode     ##
// #######################

void TupleNode::dump(NodeDumper &dumper) const { DUMP(nodes); }

// #####################
// ##     VarNode     ##
// #####################

VarNode::VarNode(ydsh::Token token, std::string &&varName)
    : WithRtti(token), varName(std::move(varName)) {
  assert(!this->varName.empty());
  char ch = this->varName[0];
  if (ch == '#') {
    this->extraOp = ARGS_LEN;
  } else if (isDecimal(ch)) {
    this->extraOp = POSITIONAL_ARG;
  }
}

void VarNode::dump(NodeDumper &dumper) const {
  DUMP(varName);
  DUMP_PTR(handle);

#define EACH_ENUM(OP)                                                                              \
  OP(NONE)                                                                                         \
  OP(ARGS_LEN)                                                                                     \
  OP(POSITIONAL_ARG)

  DUMP_ENUM(extraOp, EACH_ENUM);
#undef EACH_ENUM

  DUMP(extraValue);
}

// ########################
// ##     AccessNode     ##
// ########################

void AccessNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(recvNode);
  DUMP(nameInfo);
  DUMP_PTR(handle);

#define EACH_ENUM(OP)                                                                              \
  OP(NOP)                                                                                          \
  OP(DUP_RECV)

  DUMP_ENUM(additionalOp, EACH_ENUM);
#undef EACH_ENUM
}

// ########################
// ##     TypeOpNode     ##
// ########################

std::unique_ptr<Node> TypeOpNode::newPrintOpNode(const TypePool &pool,
                                                 std::unique_ptr<Node> &&node) {
  assert(!node->isUntyped());
  if (!node->getType().isVoidType() && !node->getType().isNothingType()) {
    auto castNode = newTypedCastNode(std::move(node), pool.get(TYPE::Void));
    castNode->setOpKind(TypeOpNode::PRINT);
    node = std::move(castNode);
  }
  return std::move(node);
}

void TypeOpNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(exprNode);
  DUMP_PTR(targetTypeNode);

#define EACH_ENUM(OP)                                                                              \
  OP(NO_CAST)                                                                                      \
  OP(TO_VOID)                                                                                      \
  OP(NUM_CAST)                                                                                     \
  OP(TO_STRING)                                                                                    \
  OP(TO_BOOL)                                                                                      \
  OP(CHECK_CAST)                                                                                   \
  OP(CHECK_UNWRAP)                                                                                 \
  OP(PRINT)                                                                                        \
  OP(ALWAYS_FALSE)                                                                                 \
  OP(ALWAYS_TRUE)                                                                                  \
  OP(INSTANCEOF)

  DUMP_ENUM(opKind, EACH_ENUM);
#undef EACH_ENUM
}

// ######################
// ##     ArgsNode     ##
// ######################

void ArgsNode::dump(NodeDumper &dumper) const {
  DUMP(nodes); // FIXME:
}

// #######################
// ##     ApplyNode     ##
// #######################

std::unique_ptr<ApplyNode> ApplyNode::newMethodCall(std::unique_ptr<Node> &&recvNode, Token token,
                                                    std::string &&methodName) {
  auto exprNode =
      std::make_unique<AccessNode>(std::move(recvNode), NameInfo(token, std::move(methodName)));
  return std::make_unique<ApplyNode>(std::move(exprNode), std::make_unique<ArgsNode>(),
                                     METHOD_CALL);
}

void ApplyNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(exprNode);
  DUMP_PTR(argsNode);
  DUMP_PTR(handle);

#define EACH_ENUM(OP)                                                                              \
  OP(UNRESOLVED)                                                                                   \
  OP(FUNC_CALL)                                                                                    \
  OP(METHOD_CALL)

  DUMP_ENUM(kind, EACH_ENUM);
#undef EACH_ENUM

#define EACH_ENUM(OP)                                                                              \
  OP(DEFAULT)                                                                                      \
  OP(INDEX)                                                                                        \
  OP(UNARY)                                                                                        \
  OP(BINARY)                                                                                       \
  OP(NEW_ITER)                                                                                     \
  OP(ITER_NEXT)                                                                                    \
  OP(MAP_ITER_NEXT_KEY)                                                                            \
  OP(MAP_ITER_NEXT_VALUE)

  DUMP_ENUM(attr, EACH_ENUM);
#undef EACH_ENUM
}

// #####################
// ##     NewNode     ##
// #####################

void NewNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(targetTypeNode);
  DUMP_PTR(argsNode);
  DUMP_PTR(handle);
}

// #######################
// ##     EmbedNode     ##
// #######################

void EmbedNode::dump(ydsh::NodeDumper &dumper) const {
#define EACH_ENUM(OP)                                                                              \
  OP(STR_EXPR)                                                                                     \
  OP(CMD_ARG)

  DUMP_ENUM(kind, EACH_ENUM);
#undef EACH_ENUM

  DUMP_PTR(exprNode);
  DUMP_PTR(handle);
}

// #########################
// ##     UnaryOpNode     ##
// #########################

void UnaryOpNode::dump(NodeDumper &dumper) const {
  DUMP(op);
  DUMP_PTR(exprNode);
  DUMP_PTR(methodCallNode);
}

// ##########################
// ##     BinaryOpNode     ##
// ##########################

void BinaryOpNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(leftNode);
  DUMP_PTR(rightNode);
  DUMP(op);
  DUMP_PTR(optNode);
}

// ########################
// ##     CmdArgNode     ##
// ########################

void CmdArgNode::addSegmentNode(std::unique_ptr<Node> &&node) {
  this->updateToken(node->getToken());
  if (isa<StringNode>(*node) && cast<StringNode>(*node).isTilde()) {
    if (!this->segmentNodes.empty() && isa<WildCardNode>(*this->segmentNodes.back()) &&
        cast<WildCardNode>(*this->segmentNodes.back()).isBraceMeta()) {
      Token t = {node->getPos(), 0};
      auto tilde = std::make_unique<WildCardNode>(t, ExpandMeta::BRACE_TILDE);
      tilde->setExpand(false);
      this->segmentNodes.push_back(std::move(tilde));
    }
  }
  this->segmentNodes.push_back(std::move(node));
}

void CmdArgNode::dump(NodeDumper &dumper) const {
  DUMP(expansionSize);
  DUMP(braceExpansion);
  DUMP(segmentNodes);
}

// ##########################
// ##     ArgArrayNode     ##
// ##########################

void ArgArrayNode::dump(NodeDumper &dumper) const { DUMP(argNodes); }

// #######################
// ##     RedirNode     ##
// #######################

void RedirNode::dump(NodeDumper &dumper) const {
  DUMP(fdName);
  DUMP(newFd);

#define EACH_ENUM(OP)                                                                              \
  OP(RedirOp::NOP)                                                                                 \
  OP(RedirOp::REDIR_IN)                                                                            \
  OP(RedirOp::REDIR_OUT)                                                                           \
  OP(RedirOp::CLOBBER_OUT)                                                                         \
  OP(RedirOp::APPEND_OUT)                                                                          \
  OP(RedirOp::REDIR_OUT_ERR)                                                                       \
  OP(RedirOp::CLOBBER_OUT_ERR)                                                                     \
  OP(RedirOp::APPEND_OUT_ERR)                                                                      \
  OP(RedirOp::DUP_FD)                                                                              \
  OP(RedirOp::HERE_DOC)                                                                            \
  OP(RedirOp::HERE_STR)

  DUMP_ENUM(op, EACH_ENUM);
#undef EACH_ENUM

  DUMP_PTR(targetNode);
  DUMP(targetFd);
  DUMP(hereStart);
  dumper.dump(NAME(hereEnd), this->hereEnd.str());
}

// ##########################
// ##     WildCardNode     ##
// ##########################

void WildCardNode::dump(NodeDumper &dumper) const {
  dumper.dump("meta", toString(meta));
  DUMP(expand);
  DUMP(braceId);
}

// ##########################
// ##     BraceSeqNode     ##
// ##########################

void BraceSeqNode::dump(NodeDumper &dumper) const {
  DUMP(range.begin);
  DUMP(range.end);
  DUMP(range.step);
  DUMP(range.digits);

#define EACH_ENUM(OP)                                                                              \
  OP(BraceRange::Kind::INT)                                                                        \
  OP(BraceRange::Kind::CHAR)                                                                       \
  OP(BraceRange::Kind::UNINIT_INT)                                                                 \
  OP(BraceRange::Kind::UNINIT_CHAR)                                                                \
  OP(BraceRange::Kind::OUT_OF_RANGE)                                                               \
  OP(BraceRange::Kind::OUT_OF_RANGE_STEP)

  DUMP_ENUM(range.kind, EACH_ENUM);
#undef EACH_ENUM
}

// #####################
// ##     CmdNode     ##
// #####################

void CmdNode::addArgNode(std::unique_ptr<CmdArgNode> &&node) {
  this->updateToken(node->getToken());
  this->argNodes.push_back(std::move(node));
}

void CmdNode::addRedirNode(std::unique_ptr<RedirNode> &&node) {
  this->updateToken(node->getToken());
  this->argNodes.push_back(std::move(node));
  this->redirCount++;
}

void CmdNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(nameNode);
  DUMP(argNodes);
  DUMP(redirCount);
  DUMP(needFork);
  DUMP_PTR(handle);
}

// ##########################
// ##     PipelineNode     ##
// ##########################

void PipelineNode::addNode(std::unique_ptr<Node> &&node) {
  if (isa<PipelineNode>(*node)) {
    auto &pipe = cast<PipelineNode>(*node);
    for (auto &e : pipe.nodes) {
      this->addNodeImpl(std::move(e));
    }
    this->updateToken(pipe.getToken());
  } else {
    this->addNodeImpl(std::move(node));
  }
}

void PipelineNode::dump(NodeDumper &dumper) const {
  DUMP(nodes);
  DUMP(baseIndex);
  DUMP(inFork);
}

void PipelineNode::addNodeImpl(std::unique_ptr<Node> &&node) {
  if (isa<CmdNode>(*node)) {
    cast<CmdNode>(*node).setNeedFork(false);
  }
  this->updateToken(node->getToken());
  this->nodes.push_back(std::move(node));
}

// ######################
// ##     WithNode     ##
// ######################

void WithNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(exprNode);
  DUMP(redirNodes);
  DUMP(baseIndex);
}

// ######################
// ##     TimeNode     ##
// ######################

void TimeNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(exprNode);
  DUMP(baseIndex);
}

// ######################
// ##     ForkNode     ##
// ######################

void ForkNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(exprNode);

#define EACH_ENUM(OP)                                                                              \
  OP(ForkKind::NONE)                                                                               \
  OP(ForkKind::PIPE_FAIL)                                                                          \
  OP(ForkKind::STR)                                                                                \
  OP(ForkKind::ARRAY)                                                                              \
  OP(ForkKind::IN_PIPE)                                                                            \
  OP(ForkKind::OUT_PIPE)                                                                           \
  OP(ForkKind::JOB)                                                                                \
  OP(ForkKind::DISOWN)                                                                             \
  OP(ForkKind::COPROC)

  DUMP_ENUM(opKind, EACH_ENUM);
#undef EACH_ENUM
}

// ########################
// ##     AssertNode     ##
// ########################

void AssertNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(condNode);
  DUMP_PTR(messageNode);
}

// #######################
// ##     BlockNode     ##
// #######################

void BlockNode::insertNodeToFirst(std::unique_ptr<Node> &&node) {
  this->nodes.insert(this->nodes.begin(), std::move(node));
}

void BlockNode::dump(NodeDumper &dumper) const {
  DUMP(nodes);
  DUMP(baseIndex);
  DUMP(varSize);
  DUMP(maxVarSize);
  DUMP(firstDeferOffset);
}

// #########################
// ##     TypeDefNode     ##
// #########################

void TypeDefNode::dump(NodeDumper &dumper) const {
  DUMP(nameInfo);
  DUMP_PTR(targetTypeNode);

#define EACH_ENUM(OP)                                                                              \
  OP(ALIAS)                                                                                        \
  OP(ERROR_DEF)

  DUMP_ENUM(kind, EACH_ENUM);
#undef EACH_ENUM
}

// #######################
// ##     DeferNode     ##
// #######################

void DeferNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(blockNode);
  DUMP(dropLocalSize);
}

// ######################
// ##     LoopNode     ##
// ######################

LoopNode::LoopNode(unsigned int startPos, std::unique_ptr<Node> &&initNode,
                   std::unique_ptr<Node> &&condNode, std::unique_ptr<Node> &&iterNode,
                   std::unique_ptr<BlockNode> &&blockNode, bool asDoWhile)
    : WithRtti({startPos, 0}), initNode(std::move(initNode)), condNode(std::move(condNode)),
      iterNode(std::move(iterNode)), blockNode(std::move(blockNode)), asDoWhile(asDoWhile) {
  if (this->initNode == nullptr) {
    this->initNode = std::make_unique<EmptyNode>();
  }

  if (this->iterNode == nullptr) {
    this->iterNode = std::make_unique<EmptyNode>();
  }

  this->updateToken(this->asDoWhile ? this->condNode->getToken() : this->blockNode->getToken());
}

void LoopNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(initNode);
  DUMP_PTR(condNode);
  DUMP_PTR(iterNode);
  DUMP_PTR(blockNode);
  DUMP(asDoWhile);
}

// ####################
// ##     IfNode     ##
// ####################

IfNode::IfNode(unsigned int startPos, bool elif, std::unique_ptr<Node> &&condNode,
               std::unique_ptr<Node> &&thenNode, std::unique_ptr<Node> &&elseNode, bool ifLet)
    : WithRtti({startPos, 0}), ifLet(ifLet), elif (elif), condNode(std::move(condNode)),
      thenNode(std::move(thenNode)), elseNode(std::move(elseNode)) {
  this->updateToken(this->thenNode->getToken());
  if (this->elseNode != nullptr) {
    this->updateToken(this->elseNode->getToken());
  }
  if (this->elseNode == nullptr) {
    this->elseNode = std::make_unique<EmptyNode>(this->getToken());
  }
}

Node &IfNode::getIfLetUnwrap() const {
  assert(this->getIfLetKind() == IfLetKind::UNWRAP);
  auto &varDeclNode = cast<VarDeclNode>(this->getCondNode());
  return *varDeclNode.getExprNode();
}

void IfNode::dump(NodeDumper &dumper) const {
  DUMP(ifLet);

#define EACH_ENUM(OP)                                                                              \
  OP(NOP)                                                                                          \
  OP(ERROR)                                                                                        \
  OP(UNWRAP)

  DUMP_ENUM(ifLeftKind, EACH_ENUM);

#undef EACH_ENUM
  DUMP(elif);
  DUMP_PTR(condNode);
  DUMP_PTR(thenNode);
  DUMP_PTR(elseNode);
}

// ######################
// ##     CaseNode     ##
// ######################

bool CaseNode::hasDefault() const {
  for (auto &e : this->armNodes) {
    if (e->isDefault()) {
      return true;
    }
  }
  return false;
}

void CaseNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(exprNode);
  DUMP(armNodes);

#define EACH_ENUM(OP)                                                                              \
  OP(MAP)                                                                                          \
  OP(IF_ELSE)

  DUMP_ENUM(caseKind, EACH_ENUM);
#undef EACH_ENUM
}

// #####################
// ##     ArmNode     ##
// #####################

void ArmNode::dump(ydsh::NodeDumper &dumper) const {
  DUMP(this->patternNodes);
  DUMP(this->constPatternNodes);
  DUMP_PTR(this->actionNode);
}

// ######################
// ##     JumpNode     ##
// ######################

JumpNode::JumpNode(Token token, OpKind kind, std::unique_ptr<Node> &&exprNode)
    : WithRtti(token), opKind(kind), exprNode(std::move(exprNode)) {
  if (this->exprNode == nullptr) {
    this->exprNode = std::make_unique<EmptyNode>(token);
  } else {
    this->updateToken(this->exprNode->getToken());
  }
}

void JumpNode::dump(NodeDumper &dumper) const {
#define EACH_ENUM(OP)                                                                              \
  OP(BREAK)                                                                                        \
  OP(CONTINUE)                                                                                     \
  OP(THROW)                                                                                        \
  OP(RETURN)                                                                                       \
  OP(RETURN_INIT)

  DUMP_ENUM(opKind, EACH_ENUM);
#undef EACH_ENUM

  DUMP(fieldOffset);
  DUMP(fieldSize);
  DUMP(tryDepth);
  DUMP_PTR(exprNode);
}

// #######################
// ##     CatchNode     ##
// #######################

void CatchNode::dump(NodeDumper &dumper) const {
  DUMP(exceptionName);
  DUMP_PTR(typeNode);
  DUMP_PTR(blockNode);
  DUMP(varIndex);
}

// #####################
// ##     TryNode     ##
// #####################

void TryNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(exprNode);
  DUMP(catchNodes);
  DUMP_PTR(finallyNode);
}

// #########################
// ##     VarDeclNode     ##
// #########################

VarDeclNode::VarDeclNode(unsigned int startPos, NameInfo &&varName,
                         std::unique_ptr<Node> &&exprNode, Kind kind)
    : WithRtti({startPos, 0}), kind(kind), varName(std::move(varName)),
      exprNode(std::move(exprNode)) {
  this->updateToken(this->varName.getToken());
  if (this->exprNode != nullptr) {
    this->updateToken(this->exprNode->getToken());
  }
}

void VarDeclNode::dump(NodeDumper &dumper) const {
  DUMP(varName);
  DUMP_PTR(exprNode);

#define EACH_ENUM(OP)                                                                              \
  OP(VAR)                                                                                          \
  OP(LET)                                                                                          \
  OP(IMPORT_ENV)                                                                                   \
  OP(EXPORT_ENV)

  DUMP_ENUM(kind, EACH_ENUM);
#undef EACH_ENUM

  DUMP_PTR(handle);
}

// ########################
// ##     AssignNode     ##
// ########################

void AssignNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(leftNode);
  DUMP_PTR(rightNode);

#define EACH_FLAG(OP)                                                                              \
  OP(SELF_ASSIGN)                                                                                  \
  OP(FIELD_ASSIGN)

  DUMP_BITSET(attributeSet, EACH_FLAG);
#undef EACH_FLAG
}

// ###################################
// ##     ElementSelfAssignNode     ##
// ###################################

ElementSelfAssignNode::ElementSelfAssignNode(std::unique_ptr<ApplyNode> &&leftNode,
                                             std::unique_ptr<BinaryOpNode> &&binaryNode)
    : WithRtti(leftNode->getToken()), rightNode(std::move(binaryNode)) {
  this->updateToken(this->rightNode->getToken());

  // init recv, indexNode
  assert(leftNode->isIndexOp());
  auto opToken = cast<AccessNode>(leftNode->getExprNode()).getNameToken();
  auto pair = ApplyNode::split(std::move(leftNode));
  this->recvNode = std::move(pair.first);
  this->indexNode = std::move(pair.second);

  // init getter node
  this->getterNode = ApplyNode::newIndexCall(std::make_unique<EmptyNode>(), opToken,
                                             std::make_unique<EmptyNode>());

  // init setter node
  this->setterNode =
      ApplyNode::newIndexCall(std::make_unique<EmptyNode>(), opToken, std::make_unique<EmptyNode>(),
                              std::make_unique<EmptyNode>());
}

void ElementSelfAssignNode::setRecvType(const DSType &type) {
  this->getterNode->getRecvNode().setType(type);
  this->setterNode->getRecvNode().setType(type);
}

void ElementSelfAssignNode::setIndexType(const DSType &type) {
  this->getterNode->getArgsNode().refNodes()[0]->setType(type);
  this->setterNode->getArgsNode().refNodes()[0]->setType(type);
}

void ElementSelfAssignNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(recvNode);
  DUMP_PTR(indexNode);
  DUMP_PTR(getterNode);
  DUMP_PTR(setterNode);
  DUMP_PTR(rightNode);
}

// ##############################
// ##     PrefixAssignNode     ##
// ##############################

void PrefixAssignNode::dump(NodeDumper &dumper) const {
  DUMP(declNodes);
  DUMP_PTR(exprNode);
  DUMP(baseIndex);
}

// ##########################
// ##     FunctionNode     ##
// ##########################

void FunctionNode::setFuncBody(std::unique_ptr<Node> &&node) {
  if (isa<BlockNode>(*node)) {
    this->blockNode.reset(cast<BlockNode>(node.release()));
  } else {
    this->blockNode = std::make_unique<BlockNode>(node->getPos());
    this->blockNode->addNode(std::move(node));
  }
  this->updateToken(this->blockNode->getToken());
}

void FunctionNode::dump(NodeDumper &dumper) const {
#define EACH_ENUM(OP)                                                                              \
  OP(FUNC)                                                                                         \
  OP(SINGLE_EXPR)                                                                                  \
  OP(EXPLICIT_CONSTRUCTOR)                                                                         \
  OP(IMPLICIT_CONSTRUCTOR)

  DUMP_ENUM(kind, EACH_ENUM);
#undef EACH_ENUM

  DUMP(funcName);
  DUMP(paramNodes);
  DUMP_PTR(returnTypeNode);
  DUMP_PTR(recvTypeNode);
  DUMP_PTR(blockNode);
  DUMP(maxVarNum);
  DUMP_PTR(handle);
  DUMP_PTR(resolvedType);
}

// ################################
// ##     UserDefinedCmdNode     ##
// ################################

void UserDefinedCmdNode::dump(NodeDumper &dumper) const {
  DUMP(cmdName);
  DUMP_PTR(handle);
  DUMP_PTR(returnTypeNode);
  DUMP_PTR(blockNode);
  DUMP(maxVarNum);
}

// ##########################
// ##     FuncListNode     ##
// ##########################

void FuncListNode::dump(ydsh::NodeDumper &dumper) const { DUMP(nodes); }

// ########################
// ##     SourceNode     ##
// ########################

void SourceNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(name);
  DUMP(modType);
  DUMP_PTR(pathName);
  DUMP(firstAppear);
  DUMP(inlined);
  DUMP(srcIndex);
  DUMP(maxVarNum);
}

// ############################
// ##     SourceListNode     ##
// ############################

SourceListNode::~SourceListNode() {
  for (auto &e : this->constNodes) {
    delete e;
  }
}

void SourceListNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(pathNode);
  DUMP(constNodes);
  DUMP_PTR(name);
  DUMP(optional);
  DUMP(inlined);
  DUMP(curIndex);

  std::vector<std::string> tmp;
  tmp.reserve(this->pathList.size());
  for (auto &e : this->pathList) {
    tmp.push_back(*e);
  }
  dumper.dump(NAME(pathList), tmp);
}

// ##########################
// ##     CodeCompNode     ##
// ##########################

void CodeCompNode::dump(NodeDumper &dumper) const {
  DUMP_PTR(exprNode);

#define EACH_ENUM(OP)                                                                              \
  OP(VAR)                                                                                          \
  OP(VAR_IN_CMD_ARG)                                                                               \
  OP(MEMBER)                                                                                       \
  OP(TYPE)

  DUMP_ENUM(kind, EACH_ENUM);
#undef EACH_ENUM
}

// #######################
// ##     ErrorNode     ##
// #######################

void ErrorNode::dump(NodeDumper &) const {}

// #######################
// ##     EmptyNode     ##
// #######################

void EmptyNode::dump(NodeDumper &) const {} // do nothing

// for node creation
std::unique_ptr<LoopNode> createForInNode(unsigned int startPos, NameInfo &&keyName,
                                          NameInfo &&valueName, std::unique_ptr<Node> &&exprNode,
                                          std::unique_ptr<BlockNode> &&blockNode) {
  Token dummy = exprNode->getToken();

  // create for-init
  auto call_iter = ApplyNode::newIter(std::move(exprNode));
  std::string reset_var_name = "%reset_";
  reset_var_name += std::to_string(startPos);
  auto reset_varDecl =
      std::make_unique<VarDeclNode>(startPos, NameInfo(dummy, std::string(reset_var_name)),
                                    std::move(call_iter), VarDeclNode::LET);

  // create for-in init
  if (valueName) { // for k, v in <expr>
    // value
    auto reset_var = std::make_unique<VarNode>(dummy, std::string(reset_var_name));
    auto call_next = ApplyNode::newMapIterNextValue(std::move(reset_var));
    unsigned int pos = valueName.getToken().pos;
    auto init_var = std::make_unique<VarDeclNode>(pos, std::move(valueName), std::move(call_next),
                                                  VarDeclNode::VAR);
    blockNode->insertNodeToFirst(std::move(init_var));

    // key
    reset_var = std::make_unique<VarNode>(dummy, std::string(reset_var_name));
    call_next = ApplyNode::newMapIterNextKey(std::move(reset_var));
    pos = keyName.getToken().pos;
    init_var = std::make_unique<VarDeclNode>(pos, std::move(keyName), std::move(call_next),
                                             VarDeclNode::VAR);
    blockNode->insertNodeToFirst(std::move(init_var));
  } else { // for v in <expr>
    auto reset_var = std::make_unique<VarNode>(dummy, std::string(reset_var_name));
    auto call_next = ApplyNode::newIterNext(std::move(reset_var));
    unsigned int pos = keyName.getToken().pos;
    auto init_var = std::make_unique<VarDeclNode>(pos, std::move(keyName), std::move(call_next),
                                                  VarDeclNode::VAR);

    // insert init to block
    blockNode->insertNodeToFirst(std::move(init_var));
  }
  return std::make_unique<LoopNode>(startPos, std::move(reset_varDecl), nullptr, nullptr,
                                    std::move(blockNode));
}

std::unique_ptr<Node> createAssignNode(std::unique_ptr<Node> &&leftNode, TokenKind op, Token token,
                                       std::unique_ptr<Node> &&rightNode) {
  /*
   * basic assignment
   */
  if (op == TokenKind::ASSIGN) {
    // assign to element(actually call SET)
    if (isa<ApplyNode>(*leftNode) && cast<ApplyNode>(*leftNode).isIndexOp()) {
      auto &indexNode = cast<ApplyNode>(*leftNode);
      indexNode.setMethodName(std::string(OP_SET));
      indexNode.getArgsNode().addNode(std::move(rightNode));
      return std::move(leftNode);
    }
    // assign to variable or field
    return std::make_unique<AssignNode>(std::move(leftNode), std::move(rightNode));
  }

  /**
   * self assignment
   */
  // assign to element
  auto opNode = std::make_unique<BinaryOpNode>(std::make_unique<EmptyNode>(rightNode->getToken()),
                                               resolveAssignOp(op), token, std::move(rightNode));
  if (isa<ApplyNode>(*leftNode) && cast<ApplyNode>(*leftNode).isIndexOp()) {
    auto *indexNode = cast<ApplyNode>(leftNode.release());
    return std::make_unique<ElementSelfAssignNode>(std::unique_ptr<ApplyNode>(indexNode),
                                                   std::move(opNode));
  }
  // assign to variable or field
  return std::make_unique<AssignNode>(std::move(leftNode), std::move(opNode), true);
}

const Node *findInnerNode(NodeKind kind, const Node *node) {
  while (!node->is(kind)) {
    assert(isa<TypeOpNode>(node));
    node = &cast<const TypeOpNode>(node)->getExprNode();
  }
  return node;
}

// ########################
// ##     NodeDumper     ##
// ########################

void NodeDumper::dump(const char *fieldName, const Node &node) {
  // write field name
  this->writeName(fieldName);

  // write node body
  this->newline();
  this->enterIndent();
  this->dump(node);
  this->leaveIndent();
}

void NodeDumper::dump(const char *fieldName, const DSType &type) {
  this->dump(fieldName, type.getName());
}

void NodeDumper::dump(const char *fieldName, TokenKind kind) {
  this->dump(fieldName, toString(kind));
}

void NodeDumper::dump(const char *fieldName, const Handle &handle) {
  // write field name
  this->writeName(fieldName);

  // write body
  this->newline();
  this->enterIndent();
  this->dump("index", handle.getIndex());
  this->dump("kind", toString(handle.getKind()));
  this->dump("attribute", toString(handle.attr()));
  this->leaveIndent();
}

void NodeDumper::dump(const char *fieldName, const MethodHandle &handle) {
  this->dump(fieldName, std::to_string(handle.getIndex()));
}

void NodeDumper::dump(const char *fieldName, const NameInfo &info) {
  // write field name
  this->writeName(fieldName);

  // write body
  this->newline();
  this->enterIndent();
  this->indent();
  this->dumpToken(info.getToken());
  this->dump("name", info.getName());
  this->leaveIndent();
}

void NodeDumper::dump(const Node &node) {
  this->indent();
  this->dumpNodeHeader(node);
  node.dump(*this);
}

void NodeDumper::indent() {
  for (unsigned int i = 0; i < this->bufs.back().indentLevel; i++) {
    this->append("  ");
  }
}

static const char *toString(NodeKind kind) {
  const char *table[] = {
#define GEN_STR(E) #E,
      EACH_NODE_KIND(GEN_STR)
#undef GEN_STR
  };
  return table[static_cast<unsigned char>(kind)];
}

void NodeDumper::dumpNodeHeader(const Node &node, bool inArray) {
  formatTo(this->buf(), "nodeKind: %s\n", toString(node.getNodeKind()));

  if (inArray) {
    this->enterIndent();
  }

  this->indent();
  this->dumpToken(node.getToken());

  if (node.isUntyped()) {
    this->indent();
    this->append("type:\n");
  } else {
    this->dump("type", node.getType().getName());
  }

  if (inArray) {
    this->leaveIndent();
  }
}

void NodeDumper::dumpToken(Token token) {
  this->append("token:\n");
  this->enterIndent();
  this->indent();
  formatTo(this->buf(), "pos: %d\n", token.pos);
  this->indent();
  formatTo(this->buf(), "size: %d\n", token.size);
  this->leaveIndent();
}

void NodeDumper::append(int ch) { this->buf() += static_cast<char>(ch); }

void NodeDumper::append(const char *str) { this->buf() += str; }

void NodeDumper::appendEscaped(const char *value) {
  this->append('"');
  while (*value != 0) {
    char ch = *(value++);
    bool escape = true;
    switch (ch) {
    case '\t':
      ch = 't';
      break;
    case '\r':
      ch = 'r';
      break;
    case '\n':
      ch = 'n';
      break;
    case '"':
      ch = '"';
      break;
    case '\\':
      ch = '\\';
      break;
    default:
      escape = false;
      break;
    }
    if (escape) {
      this->append('\\');
    }
    this->append(ch);
  }
  this->append('"');
}

void NodeDumper::writeName(const char *fieldName) {
  this->indent();
  formatTo(this->buf(), "%s:", fieldName);
}

void NodeDumper::enterModule(const char *sourceName, const char *header) {
  this->bufs.emplace_back();

  if (header) {
    formatTo(this->buf(), "%s\n", header);
  }
  this->dump("sourceName", sourceName);
  this->writeName("nodes");
  this->newline();

  this->enterIndent();
}

void NodeDumper::leaveModule() {
  auto b = std::move(this->bufs.back());
  this->bufs.pop_back();
  this->bufs.push_front(std::move(b));
}

void NodeDumper::operator()(const Node &node) {
  this->indent();
  this->append("- ");
  this->dumpNodeHeader(node, true);
  this->enterIndent();
  node.dump(*this);
  this->leaveIndent();
}

void NodeDumper::finalize(const NameScope &scope) {
  this->leaveIndent();

  this->dumpRaw("maxVarNum", std::to_string(scope.getMaxLocalVarIndex()).c_str());
  this->dumpRaw("maxGVarNum", std::to_string(scope.getMaxGlobalVarIndex()).c_str());

  this->leaveModule();

  assert(this->fp != nullptr);
  for (auto &e : this->bufs) {
    fputs(e.value.c_str(), this->fp);
    fputc('\n', this->fp);
    fflush(this->fp);
  }
}

} // namespace ydsh