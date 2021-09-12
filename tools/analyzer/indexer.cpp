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

static std::string mangleSymbolName(DeclSymbol::Kind k, const NameInfo &nameInfo) {
  if (!static_cast<bool>(nameInfo) || nameInfo.getToken().size == 0) {
    return "";
  }

  std::string name = nameInfo.getName();
  switch (k) {
  case DeclSymbol::Kind::CMD:
    return toCmdFullName(name);
  case DeclSymbol::Kind::TYPE_ALIAS:
    return toTypeAliasFullName(name);
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::FUNC:
    if (name[0] == '%') {
      return "";
    }
    break;
  }
  return name;
}

bool IndexBuilder::addDecl(const NameInfo &info, DeclSymbol::Kind kind, const char *hover) {
  auto *decl = this->addDeclImpl(kind, info, hover);
  if (!decl) {
    return false;
  }
  if (!this->addSymbolImpl(info.getToken(), *decl)) {
    return false;
  }
  return true;
}

bool IndexBuilder::addSymbol(const NameInfo &info, DeclSymbol::Kind kind) {
  std::string name = mangleSymbolName(kind, info);
  if (name.empty()) {
    return false;
  }
  auto *ref = this->scope->find(name);
  if (!ref) {
    return false;
  }

  DeclBase *decl;
  if (ref->getModId() == this->modId) {
    auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), ref->getPos(),
                                 DeclSymbol::Compare());
    if (iter == this->decls.end()) {
      return false;
    }
    decl = &*iter;
    assert(decl->getPos() == ref->getPos());
  } else { // foreign decl
    SymbolRequest request = {.modId = ref->getModId(), .pos = ref->getPos()};
    auto iter = std::lower_bound(this->foreigns.begin(), this->foreigns.end(), request,
                                 ForeignDecl::Compare());
    if (iter != this->foreigns.end() && (*iter).getModId() == request.modId &&
        (*iter).getPos() == request.pos) { // already registered
      decl = &*iter;
    } else {
      auto *ret = this->indexes.findDecl(request);
      if (!ret || !hasFlag(ret->getAttr(), DeclSymbol::Attr::GLOBAL | DeclSymbol::Attr::PUBLIC)) {
        return false;
      }
      iter = this->foreigns.insert(iter, ForeignDecl(*ret));
      decl = &*iter;
    }
  }
  if (!this->addSymbolImpl(info.getToken(), *decl)) {
    return false;
  }
  auto symbol = SymbolRef::create(info.getToken(), this->modId);
  assert(symbol.hasValue());
  decl->addRef(symbol.unwrap());
  return true;
}

bool IndexBuilder::importForeignDecls(unsigned short foreignModId) {
  if (!this->scope->isGlobal()) {
    return false;
  }
  if (auto iter = this->globallyImportedModIds.find(foreignModId);
      iter != this->globallyImportedModIds.end()) {
    return true; // already imported
  }

  auto *index = this->indexes.find(foreignModId);
  if (!index) {
    return false;
  }
  this->globallyImportedModIds.emplace(foreignModId);
  for (auto &decl : index->getDecls()) {
    if (hasFlag(decl.getAttr(), DeclSymbol::Attr::PUBLIC | DeclSymbol::Attr::GLOBAL)) {
      this->scope->addDecl(decl);
    }
  }
  return true;
}

DeclSymbol *IndexBuilder::addDeclImpl(DeclSymbol::Kind k, const NameInfo &nameInfo,
                                      const char *info) {
  std::string name = mangleSymbolName(k, nameInfo);
  if (name.empty()) {
    return nullptr;
  }

  // create DeclSymbol
  const Token token = nameInfo.getToken();
  DeclSymbol::Attr attr = {};
  if (this->scope->isGlobal()) {
    setFlag(attr, DeclSymbol::Attr::GLOBAL);
  }
  if (name[0] != '_') {
    setFlag(attr, DeclSymbol::Attr::PUBLIC);
  }
  auto ret = DeclSymbol::create(k, attr, nameInfo.getToken(), this->modId, name, info);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto &decl = ret.unwrap();

  // register name to scope
  if (!this->scope->addDecl(decl)) {
    return nullptr;
  }

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

bool IndexBuilder::addSymbolImpl(Token token, const DeclBase &decl) {
  auto ret = Symbol::create(token, decl);
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
  this->symbols.insert(iter, symbol);
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
  if (!this->isTopLevel()) {
    return;
  }
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
  if (!this->isTopLevel()) {
    return;
  }
  this->builder().addUdcDecl(node.getNameInfo());
  auto udc = this->builder().intoScope(); // FIXME: register parameter?
  this->visitBlockWithCurrentScope(node.getBlockNode());
}

void SymbolIndexer::visitSourceNode(SourceNode &node) {
  if (!this->isTopLevel()) {
    return;
  }
  if (node.getNameInfo()) { // FIXME: named import
    this->builder().addDecl(*node.getNameInfo(), node.getModType());
    //  this->builder().addDecl(DeclSymbol::Kind::TYPE_ALIAS, *node.getNameInfo());
    //  this->builder().addDecl(DeclSymbol::Kind::CMD, *node.getNameInfo());
  } else {
    this->builder().importForeignDecls(node.getModType().getModID());
  }
}

void SymbolIndexer::visitSourceListNode(SourceListNode &) {}

void SymbolIndexer::visitCodeCompNode(CodeCompNode &) {}

void SymbolIndexer::visitErrorNode(ErrorNode &node) {
  assert(this->isTopLevel());
  this->visitingDepth--;
  this->visit(node.getOrgNode());
  this->visitingDepth++;
}

void SymbolIndexer::visitEmptyNode(EmptyNode &) {}

void SymbolIndexer::visit(Node &node) {
  this->visitingDepth++;
  NodeVisitor::visit(node);
  this->visitingDepth--;
}

void SymbolIndexer::enterModule(unsigned short modId, int version,
                                const std::shared_ptr<TypePool> &p) {
  this->builders.emplace_back(modId, version, p, this->indexes);
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