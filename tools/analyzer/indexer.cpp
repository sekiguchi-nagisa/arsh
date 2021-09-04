/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include "indexer.h"

namespace ydsh::lsp {

// ##########################
// ##     IndexBuidler     ##
// ##########################

static bool checkNameInfo(const NameInfo &info) {
  return static_cast<bool>(info) && info.getToken().size > 0 && info.getName()[0] != '%';
}

static std::string mangleSymbolName(DeclSymbol::Kind k, const std::string &name) {
  switch (k) {
  case DeclSymbol::Kind::CMD:
    return toCmdFullName(name);
  case DeclSymbol::Kind::TYPE_ALIAS:
    return toTypeAliasFullName(name);
  default:
    return name;
  }
}

bool IndexBuilder::addDecl(const NameInfo &info, DeclSymbol::Kind kind, const char *hover) {
  if (!checkNameInfo(info)) {
    return false;
  }
  auto ref = SymbolRef::create(info.getToken(), this->modId);
  if (!ref.hasValue()) {
    return false;
  }
  std::string name = mangleSymbolName(kind, info.getName());
  auto pair = this->scope->map.emplace(name, ref.unwrap());
  if (!pair.second) {
    return false;
  }
  auto *decl = this->addDeclImpl(kind, info.getToken(), hover);
  if (!decl) {
    return false;
  }
  if (!this->addSymbolImpl(info.getToken(), this->modId, *decl)) {
    return false;
  }
  decl->addRef(ref.unwrap());
  return true;
}

bool IndexBuilder::addSymbol(const NameInfo &info, DeclSymbol::Kind kind) {
  if (!checkNameInfo(info)) {
    return false;
  }
  auto symbol = SymbolRef::create(info.getToken(), this->modId);
  if (!symbol.hasValue()) {
    return false;
  }
  std::string name = mangleSymbolName(kind, info.getName());
  auto *ref = this->findDeclRef(name);
  if (!ref) {
    return false;
  }
  if (ref->getModId() != this->modId) {
    return false; // FIXME: support foreign decl lookup
  }
  auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), ref->getPos(),
                               DeclSymbol::Compare());
  if (iter == this->decls.end()) {
    return false;
  }
  auto &decl = *iter;
  if (!this->addSymbolImpl(info.getToken(), ref->getModId(), decl)) {
    return false;
  }
  decl.addRef(symbol.unwrap());
  return true;
}

const SymbolRef *IndexBuilder::findDeclRef(const std::string &name) const {
  auto cur = this->scope;
  do {
    auto iter = cur->map.find(name);
    if (iter != cur->map.end()) {
      return &iter->second;
    }
    cur = cur->parent;
  } while (cur);
  return nullptr;
}

DeclSymbol *IndexBuilder::addDeclImpl(DeclSymbol::Kind k, Token token, const char *info) {
  auto ret = DeclSymbol::create(k, token, info);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto &decl = ret.unwrap();
  auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), decl.getPos(),
                               DeclSymbol::Compare());
  if (iter != this->decls.end()) {
    if (iter->getPos() == decl.getPos()) {
      fatal("try to add token: %s, but already added: %s\n", toString(token).c_str(),
            toString(iter->getToken()).c_str());
    }
  }
  iter = this->decls.insert(iter, std::move(decl));
  return &(*iter);
}

bool IndexBuilder::addSymbolImpl(Token token, unsigned short declModId, const DeclSymbol &decl) {
  auto ret = Symbol::create(token, declModId, decl.getPos());
  if (!ret.hasValue()) {
    return false;
  }
  auto &symbol = ret.unwrap();
  auto iter = std::lower_bound(this->symbols.begin(), this->symbols.end(), symbol.getPos(),
                               Symbol::Compare());
  if (iter != this->symbols.end()) {
    if (iter->getPos() == decl.getPos()) {
      fatal("try to add token: %s, but already added: %s\n", toString(token).c_str(),
            toString(iter->getToken()).c_str());
    }
  }
  this->symbols.insert(iter, std::move(symbol));
  return true;
}

// ###########################
// ##     SymbolIndexer     ##
// ###########################

void SymbolIndexer::visitTypeNode(TypeNode &node) {
  switch (node.typeKind) {
  case TypeNode::Base: {
    auto &base = cast<BaseTypeNode>(node);
    this->builder().addSymbol(NameInfo(base.getToken(), std::string(base.getTokenText())),
                              DeclSymbol::Kind::TYPE_ALIAS);
    break;
  }
  case TypeNode::Qualified: {
    auto &type = cast<QualifiedTypeNode>(node);
    this->visit(type.getRecvTypeNode()); // FIXME: resolve module
    break;
  }
  case TypeNode::Reified: {
    auto &type = cast<ReifiedTypeNode>(node);
    this->visitEach(type.getElementTypeNodes());
    break;
  }
  case TypeNode::Func: {
    auto &type = cast<FuncTypeNode>(node);
    this->visitEach(type.getParamTypeNodes());
    this->visit(type.getReturnTypeNode());
    break;
  }
  case TypeNode::Return: {
    auto &type = cast<ReturnTypeNode>(node);
    this->visitEach(type.getTypeNodes());
    break;
  }
  case TypeNode::TypeOf: {
    auto &type = cast<TypeOfNode>(node);
    this->visit(type.getExprNode());
    break;
  }
  }
}

void SymbolIndexer::visitNumberNode(NumberNode &) {}

void SymbolIndexer::visitStringNode(StringNode &) {}

void SymbolIndexer::visitStringExprNode(StringExprNode &node) {
  this->visitEach(node.getExprNodes());
}

void SymbolIndexer::visitRegexNode(RegexNode &) {}

void SymbolIndexer::visitArrayNode(ArrayNode &node) { this->visitEach(node.getExprNodes()); }

void SymbolIndexer::visitMapNode(MapNode &node) {
  this->visitEach(node.getKeyNodes());
  this->visitEach(node.getValueNodes());
}

void SymbolIndexer::visitTupleNode(TupleNode &node) { this->visitEach(node.getNodes()); }

void SymbolIndexer::visitVarNode(VarNode &node) {
  NameInfo info(node.getToken(), std::string(node.getVarName()));
  this->builder().addSymbol(info);
}

void SymbolIndexer::visitAccessNode(AccessNode &node) { // FIXME: field name
  this->visit(node.getRecvNode());
}

void SymbolIndexer::visitTypeOpNode(TypeOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getTargetTypeNode());
}

void SymbolIndexer::visitUnaryOpNode(UnaryOpNode &node) {
  this->visit(node.getExprNode());
  this->visit(node.getApplyNode());
}

void SymbolIndexer::visitBinaryOpNode(BinaryOpNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
  this->visit(node.getOptNode());
}

void SymbolIndexer::visitArgsNode(ArgsNode &node) { // FIXME: method name ?
  this->visitEach(node.getNodes());
}

void SymbolIndexer::visitApplyNode(ApplyNode &node) { // FIXME: method call?
  this->visit(node.getExprNode());
  this->visit(node.getArgsNode());
}

void SymbolIndexer::visitEmbedNode(EmbedNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitNewNode(NewNode &node) { // FIXME: constructor name ?
  this->visit(node.getTargetTypeNode());
  this->visit(node.getArgsNode());
}

void SymbolIndexer::visitForkNode(ForkNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitCmdNode(CmdNode &node) {
  auto &cmdName = node.getNameNode().getValue();
  if (!cmdName.empty() && cmdName[0] != '~' && !StringRef(cmdName).contains('/')) {
    NameInfo info(node.getNameNode().getToken(), std::string(cmdName));
    this->builder().addSymbol(info, DeclSymbol::Kind::CMD);
  }
  this->visitEach(node.getArgNodes());
}

void SymbolIndexer::visitCmdArgNode(CmdArgNode &node) { this->visitEach(node.getSegmentNodes()); }

void SymbolIndexer::visitArgArrayNode(ArgArrayNode &node) {
  this->visitEach(node.getCmdArgNodes());
}

void SymbolIndexer::visitRedirNode(RedirNode &node) { this->visit(node.getTargetNode()); }

void SymbolIndexer::visitWildCardNode(WildCardNode &) {}

void SymbolIndexer::visitPipelineNode(PipelineNode &node) { this->visitEach(node.getNodes()); }

void SymbolIndexer::visitWithNode(WithNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getRedirNodes());
}

void SymbolIndexer::visitAssertNode(AssertNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getMessageNode());
}

void SymbolIndexer::visitBlockNode(BlockNode &node) {
  auto block = this->builder().intoScope();
  this->visitBlockWithCurrentScope(node);
}

void SymbolIndexer::visitTypeAliasNode(TypeAliasNode &node) {
  this->visit(node.getTargetTypeNode());
  this->builder().addDecl(node.getNameInfo(), node.getTargetTypeNode().getType(),
                          DeclSymbol::Kind::TYPE_ALIAS);
}

void SymbolIndexer::visitLoopNode(LoopNode &node) {
  auto block = this->builder().intoScope();
  this->visit(node.getInitNode());
  this->visit(node.getCondNode());
  this->visitBlockWithCurrentScope(node.getBlockNode());
  this->visit(node.getIterNode());
}

void SymbolIndexer::visitIfNode(IfNode &node) {
  this->visit(node.getCondNode());
  this->visit(node.getThenNode());
  this->visit(node.getElseNode());
}

void SymbolIndexer::visitCaseNode(CaseNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getArmNodes());
}

void SymbolIndexer::visitArmNode(ArmNode &node) {
  this->visitEach(node.getPatternNodes());
  this->visit(node.getActionNode());
}

void SymbolIndexer::visitJumpNode(JumpNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitCatchNode(CatchNode &node) {
  auto block = this->builder().intoScope();
  this->visit(node.getTypeNode());
  this->builder().addDecl(node.getNameInfo(), node.getTypeNode().getType());
  this->visitBlockWithCurrentScope(node.getBlockNode());
}

void SymbolIndexer::visitTryNode(TryNode &node) {
  this->visit(node.getExprNode());
  this->visitEach(node.getCatchNodes());
  this->visit(node.getFinallyNode());
}

void SymbolIndexer::visitVarDeclNode(VarDeclNode &node) {
  this->visit(node.getExprNode());
  auto &type = node.getExprNode() ? node.getExprNode()->getType()
                                  : this->builder().getPool().get(TYPE::String);
  this->builder().addDecl(node.getNameInfo(), type);
}

void SymbolIndexer::visitAssignNode(AssignNode &node) {
  this->visit(node.getLeftNode());
  this->visit(node.getRightNode());
}

void SymbolIndexer::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
  this->visit(node.getRecvNode());
  this->visit(node.getIndexNode());
  this->visit(node.getRightNode());
}

void SymbolIndexer::visitPrefixAssignNode(PrefixAssignNode &node) {
  if (node.getExprNode()) {
    auto block = this->builder().intoScope();
    for (auto &e : node.getAssignNodes()) {
      this->visit(e->getRightNode());
      assert(isa<VarNode>(e->getLeftNode()));
      auto &leftNode = cast<VarNode>(e->getLeftNode());
      NameInfo info(leftNode.getToken(), std::string(leftNode.getVarName()));
      this->builder().addDecl(info, leftNode.getType());
    }
    this->visit(node.getExprNode());
  } else {
    this->visitEach(node.getAssignNodes());
  }
}

void SymbolIndexer::visitFunctionNode(FunctionNode &node) {
  this->visit(node.getReturnTypeToken());
  this->visitEach(node.getParamTypeNodes());
  this->builder().addDecl(node.getNameInfo(), *node.getFuncType(), DeclSymbol::Kind::FUNC);
  auto func = this->builder().intoScope();
  for (unsigned int i = 0; i < node.getParams().size(); i++) {
    this->builder().addDecl(node.getParams()[i], node.getParamTypeNodes()[i]->getType());
  }
  this->visitBlockWithCurrentScope(node.getBlockNode());
}

void SymbolIndexer::visitInterfaceNode(InterfaceNode &) {}

void SymbolIndexer::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
  this->builder().addUdcDecl(node.getNameInfo());
  auto udc = this->builder().intoScope(); // FIXME: register parameter?
  this->visitBlockWithCurrentScope(node.getBlockNode());
}

void SymbolIndexer::visitSourceNode(SourceNode &node) { // FIXME: import foreign decl
  if (node.getNameInfo()) {
    this->builder().addDecl(*node.getNameInfo(), node.getModType());
    //  this->builder().addDecl(DeclSymbol::Kind::TYPE_ALIAS, *node.getNameInfo());
    //  this->builder().addDecl(DeclSymbol::Kind::CMD, *node.getNameInfo());
  }
}

void SymbolIndexer::visitSourceListNode(SourceListNode &) {}

void SymbolIndexer::visitCodeCompNode(CodeCompNode &) {}

void SymbolIndexer::visitErrorNode(ErrorNode &) {}

void SymbolIndexer::visitEmptyNode(EmptyNode &) {}

void SymbolIndexer::visit(Node &node) { NodeVisitor::visit(node); }

void SymbolIndexer::enterModule(unsigned short modId, int version,
                                const std::shared_ptr<TypePool> &p) {
  this->builders.emplace_back(modId, version, p);
}

void SymbolIndexer::exitModule(std::unique_ptr<Node> &&node) {
  assert(!this->builders.empty());
  auto index = std::move(this->builder()).build();
  this->builders.pop_back();
  this->indexes.add(std::move(index));
  this->visit(node);
}

void SymbolIndexer::consume(std::unique_ptr<Node> &&node) { this->visit(node); }

} // namespace ydsh::lsp