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

bool IndexBuilder::ScopeEntry::addDecl(const DeclSymbol &decl) {
  bool r1 = this->map.emplace(decl.getMangledName().toString(), decl.toRef()).second;
  if (decl.getKind() == DeclSymbol::Kind::MOD) {
    // register udc
    std::string name = DeclSymbol::mangle(DeclSymbol::Kind::CMD, decl.getMangledName().toString());
    auto r2 = this->map.emplace(std::move(name), decl.toRef());

    // register type alias
    name = DeclSymbol::mangle(DeclSymbol::Kind::TYPE_ALIAS, decl.getMangledName().toString());
    auto r3 = this->map.emplace(std::move(name), decl.toRef());
    r1 = r1 || r2.second || r3.second;
  }
  return r1;
}

void IndexBuilder::LazyMemberMap::buildCache(const DSType &recvType) {
  if (!recvType.isModType()) {
    return; // currently, only support Mod type
  }

  unsigned short targetModId = cast<ModType>(recvType).getModID();
  if (auto iter = this->cachedModIds.find(targetModId); iter != this->cachedModIds.end()) {
    return;
  }

  auto *index = this->indexes.find(targetModId);
  if (!index) {
    return;
  }
  this->cachedModIds.emplace(targetModId);
  for (auto &decl : index->getDecls()) {
    this->addDecl(recvType, decl);
  }
  for (auto &e : index->getInlinedModIds()) {
    auto ret = this->getPool().getModTypeById(e);
    assert(ret);
    this->buildCache(*ret.asOk());
  }
}

void IndexBuilder::LazyMemberMap::addDecl(const DSType &recvType, const DeclSymbol &decl) {
  if (!hasFlag(decl.getAttr(), DeclSymbol::Attr::PUBLIC | DeclSymbol::Attr::GLOBAL)) {
    return;
  }
  unsigned int typeId = recvType.typeId();
  auto key = std::make_pair(typeId, decl.getMangledName().toString());
  this->map.emplace(std::move(key), ObserverPtr<const DeclSymbol>(&decl));
  if (decl.getKind() == DeclSymbol::Kind::MOD) {
    // udc
    key = {typeId, DeclSymbol::mangle(DeclSymbol::Kind::CMD, decl.getMangledName().toString())};
    this->map.emplace(std::move(key), ObserverPtr<const DeclSymbol>(&decl));

    // type alias
    key = {typeId,
           DeclSymbol::mangle(DeclSymbol::Kind::TYPE_ALIAS, decl.getMangledName().toString())};
    this->map.emplace(std::move(key), ObserverPtr<const DeclSymbol>(&decl));
  }
}

ObserverPtr<const DeclSymbol> IndexBuilder::LazyMemberMap::find(const DSType &recvType,
                                                                const std::string &memberName) {
  this->buildCache(recvType);
  if (auto decl = this->findImpl(recvType, memberName)) {
    return decl;
  }

  // if recvType is ModType, search inlined module
  if (isa<ModType>(recvType)) {
    auto &modType = cast<ModType>(recvType);
    unsigned int size = modType.getChildSize();
    for (unsigned int i = 0; i < size; i++) {
      auto child = modType.getChildAt(i);
      if (child.isInlined()) {
        auto &childType = this->getPool().get(child.typeId());
        if (auto decl = this->findImpl(childType, memberName)) {
          return decl;
        }
      }
    }
  }
  return nullptr;
}

ObserverPtr<const DeclSymbol>
IndexBuilder::LazyMemberMap::findImpl(const DSType &recvType, const std::string &memberName) const {
  auto iter = this->map.find({recvType.typeId(), memberName});
  if (iter != this->map.end()) {
    return iter->second;
  }
  return nullptr;
}

static std::string mangleSymbolName(DeclSymbol::Kind k, const NameInfo &nameInfo) {
  if (!static_cast<bool>(nameInfo) || nameInfo.getToken().size == 0) {
    return "";
  }
  switch (k) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::FUNC:
    if (nameInfo.getName()[0] == '%') {
      return "";
    }
    break;
  default:
    break;
  }
  return DeclSymbol::mangle(k, nameInfo.getName());
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
    if (iter == this->foreigns.end() || (*iter).getModId() != request.modId ||
        (*iter).getPos() != request.pos) { // not found, register foreign decl
      auto *ret = this->memberMap.indexes.findDecl(request);
      if (!ret || !hasFlag(ret->getAttr(), DeclSymbol::Attr::GLOBAL | DeclSymbol::Attr::PUBLIC)) {
        return false;
      }
      iter = this->foreigns.insert(iter, ForeignDecl(*ret));
    }
    decl = &*iter;
  }
  if (!this->addSymbolImpl(info.getToken(), *decl)) {
    return false;
  }
  auto symbol = SymbolRef::create(info.getToken(), this->modId);
  assert(symbol.hasValue());
  decl->addRef(symbol.unwrap());
  return true;
}

bool IndexBuilder::importForeignDecls(unsigned short foreignModId, bool inlined) {
  if (!this->scope->isGlobal()) {
    return false;
  }
  if (auto iter = this->inlinedModIds.find(foreignModId); iter != this->inlinedModIds.end()) {
    return true;
  }
  if (auto iter = this->globallyImportedModIds.find(foreignModId);
      iter != this->globallyImportedModIds.end()) {
    return true; // already imported
  }

  auto *index = this->memberMap.indexes.find(foreignModId);
  if (!index) {
    return false;
  }
  if (inlined) {
    this->inlinedModIds.emplace(foreignModId);
  }
  this->globallyImportedModIds.emplace(foreignModId);
  for (auto &decl : index->getDecls()) {
    if (hasFlag(decl.getAttr(), DeclSymbol::Attr::PUBLIC | DeclSymbol::Attr::GLOBAL)) {
      this->scope->addDecl(decl);
    }
  }
  // resolve inlined imported symbols
  for (auto &e : index->getInlinedModIds()) {
    this->importForeignDecls(e, inlined);
  }
  return true;
}

bool IndexBuilder::addMember(const DSType &recv, const NameInfo &info, DeclSymbol::Kind kind) {
  if (!recv.isModType()) {
    return false; // currently, only support mod type
  }
  std::string name = mangleSymbolName(kind, info);
  if (name.empty()) {
    return false;
  }
  auto declPtr = this->memberMap.find(recv, name);
  if (!declPtr) {
    return false;
  }

  // search Foreign decls
  SymbolRequest request = {.modId = declPtr->getModId(), .pos = declPtr->getPos()};
  auto iter = std::lower_bound(this->foreigns.begin(), this->foreigns.end(), request,
                               ForeignDecl::Compare());
  if (iter == this->foreigns.end() || (*iter).getModId() != request.modId ||
      (*iter).getPos() != request.pos) { // not found, add decl
    iter = this->foreigns.insert(iter, ForeignDecl(*declPtr));
  }
  auto &decl = *iter;
  if (!this->addSymbolImpl(info.getToken(), decl)) {
    return false;
  }
  auto symbol = SymbolRef::create(info.getToken(), this->modId);
  assert(symbol.hasValue());
  decl.addRef(symbol.unwrap());
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
    this->visit(type.getRecvTypeNode());
    if (!type.isUntyped()) {
      NameInfo info(type.getNameTypeNode().getToken(), type.getNameTypeNode().getTokenText());
      this->builder().addMember(type.getRecvTypeNode().getType(), info,
                                DeclSymbol::Kind::TYPE_ALIAS);
    }
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
  assert(!node.isUntyped());
  if (!node.getType().isVoidType() && !node.getType().isNothingType()) {
    NameInfo info(node.getToken(), node.getVarName());
    this->builder().addSymbol(info);
  }
}

void SymbolIndexer::visitAccessNode(AccessNode &node) {
  assert(!node.isUntyped());
  this->visit(node.getRecvNode());
  if (!node.getType().isNothingType()) {
    NameInfo info(node.getNameNode().getToken(), node.getFieldName());
    this->builder().addMember(node.getRecvNode().getType(), info);
  }
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

void SymbolIndexer::visitArgsNode(ArgsNode &node) { this->visitEach(node.getNodes()); }

void SymbolIndexer::visitApplyNode(ApplyNode &node) {
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
  if (!cmdName.empty()) {
    NameInfo info(node.getNameNode().getToken(), cmdName);
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
      NameInfo info(leftNode.getToken(), leftNode.getVarName());
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
  if (node.getVarIndex() > 0) {
    this->builder().addDecl(node.getNameInfo(), *node.getFuncType(), DeclSymbol::Kind::FUNC);
  }
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
  if (node.getUdcIndex() > 0) {
    this->builder().addUdcDecl(node.getNameInfo());
  }
  auto udc = this->builder().intoScope();
  this->visitBlockWithCurrentScope(node.getBlockNode());
}

void SymbolIndexer::visitSourceNode(SourceNode &node) {
  if (!this->isTopLevel()) {
    return;
  }
  if (node.getNameInfo()) {
    this->builder().addDecl(*node.getNameInfo(), node.getModType(), DeclSymbol::Kind::MOD);
  } else {
    this->builder().importForeignDecls(node.getModType().getModID(), node.isInlined());
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