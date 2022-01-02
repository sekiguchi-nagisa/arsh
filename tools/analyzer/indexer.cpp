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

#include <cmd_desc.h>

#include "indexer.h"

namespace ydsh::lsp {

// ##########################
// ##     IndexBuidler     ##
// ##########################

bool IndexBuilder::ScopeEntry::addDecl(const DeclSymbol &decl) {
  bool r1 = this->map.emplace(decl.getMangledName().toString(), decl.toRef()).second;
  if (decl.getKind() == DeclSymbol::Kind::MOD) {
    // register udc
    std::string name = DeclSymbol::mangle(DeclSymbol::Kind::CMD, decl.getMangledName());
    auto r2 = this->map.emplace(std::move(name), decl.toRef());

    // register type alias
    name = DeclSymbol::mangle(DeclSymbol::Kind::TYPE_ALIAS, decl.getMangledName());
    auto r3 = this->map.emplace(std::move(name), decl.toRef());
    r1 = r1 || r2.second || r3.second;
  }
  return r1;
}

void IndexBuilder::LazyMemberMap::buildCache(const DSType &recvType) {
  if (!recvType.isModType()) {
    return; // currently, only support Mod type
  }

  unsigned short targetModId = cast<ModType>(recvType).getModId();
  if (auto iter = this->cachedModIds.find(targetModId); iter != this->cachedModIds.end()) {
    return;
  }

  auto index = this->indexes.find(targetModId);
  if (!index) {
    return;
  }
  this->cachedModIds.emplace(targetModId);
  for (auto &decl : index->getDecls()) {
    this->add(recvType, decl);
  }
  for (auto &e : index->getInlinedModIds()) {
    auto ret = this->getPool().getModTypeById(e);
    assert(ret);
    this->buildCache(*ret.asOk());
  }
}

void IndexBuilder::LazyMemberMap::add(const DSType &recvType, const DeclSymbol &decl) {
  if (!hasFlag(decl.getAttr(), DeclSymbol::Attr::PUBLIC | DeclSymbol::Attr::GLOBAL)) {
    return;
  }
  unsigned int typeId = recvType.typeId();
  auto key = std::make_tuple(typeId, decl.getKind() == DeclSymbol::Kind::METHOD,
                             decl.getMangledName().toString());
  this->map.emplace(std::move(key), decl.toRef());
  if (decl.getKind() == DeclSymbol::Kind::MOD) {
    // udc
    key = {typeId, false, DeclSymbol::mangle(DeclSymbol::Kind::CMD, decl.getMangledName())};
    this->map.emplace(std::move(key), decl.toRef());

    // type alias
    key = {typeId, false, DeclSymbol::mangle(DeclSymbol::Kind::TYPE_ALIAS, decl.getMangledName())};
    this->map.emplace(std::move(key), decl.toRef());
  }
}

IndexBuilder::MemberRef IndexBuilder::LazyMemberMap::find(const DSType &recvType,
                                                          const std::string &memberName,
                                                          bool isMethod) {
  this->buildCache(recvType);
  if (auto decl = this->findImpl(recvType, memberName, isMethod); decl.hasValue()) {
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
        if (auto decl = this->findImpl(childType, memberName, false); decl.hasValue()) {
          return decl;
        }
      }
    }
  }
  return {};
}

IndexBuilder::MemberRef IndexBuilder::LazyMemberMap::findImpl(const DSType &recvType,
                                                              const std::string &memberName,
                                                              bool isMethod) const {
  auto iter = this->map.find({recvType.typeId(), isMethod, memberName});
  if (iter != this->map.end()) {
    return iter->second;
  }
  if (recvType.isTupleType()) {
    if (auto *handle = recvType.lookupField(this->getPool(), memberName)) {
      return std::ref(*handle);
    }
  }
  return {};
}

static std::string mangleSymbolName(DeclSymbol::Kind k, const NameInfo &nameInfo) {
  if (!static_cast<bool>(nameInfo) || nameInfo.getToken().size == 0) {
    return "";
  }
  if (DeclSymbol::isVarName(k) && nameInfo.getName()[0] == '%') {
    return "";
  }
  return DeclSymbol::mangle(k, nameInfo.getName());
}

bool IndexBuilder::addDecl(const NameInfo &info, DeclSymbol::Kind kind, const char *hover) {
  DeclSymbol::Attr attr = {};
  if (this->scope->isGlobal()) {
    setFlag(attr, DeclSymbol::Attr::GLOBAL);
  }
  if (info.getName()[0] != '_') {
    setFlag(attr, DeclSymbol::Attr::PUBLIC);
  }

  if (auto *decl = this->addDeclImpl(kind, attr, info, hover)) {
    if (!this->addSymbolImpl(info.getToken(), decl)) {
      return false;
    }
    return true;
  }
  return false;
}

const Symbol *IndexBuilder::addSymbol(const NameInfo &info, DeclSymbol::Kind kind) {
  std::string name = mangleSymbolName(kind, info);
  if (name.empty()) {
    return nullptr;
  }
  auto *ref = this->scope->find(name);
  if (!ref && kind == DeclSymbol::Kind::CMD) {
    ref = this->builtinCmd->find(name);
  }
  if (!ref) {
    return nullptr;
  }

  DeclBase *decl;
  if (ref->getModId() == this->modId) {
    auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), ref->getPos(),
                                 DeclSymbol::Compare());
    if (iter == this->decls.end()) {
      return nullptr;
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
        return nullptr;
      }
      iter = this->foreigns.insert(iter, ForeignDecl(*ret));
    }
    decl = &*iter;
  }
  if (auto *symbol = this->addSymbolImpl(info.getToken(), decl)) {
    auto symbolRef = SymbolRef::create(info.getToken(), this->modId);
    assert(symbolRef.hasValue());
    decl->addRef(symbolRef.unwrap());
    return symbol;
  }
  return nullptr;
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

  auto index = this->memberMap.indexes.find(foreignModId);
  if (!index) {
    return false;
  }
  if (inlined) {
    this->inlinedModIds.emplace(foreignModId);
  }
  this->globallyImportedModIds.emplace(foreignModId);
  for (auto &decl : index->getDecls()) {
    if (hasFlag(decl.getAttr(), DeclSymbol::Attr::PUBLIC | DeclSymbol::Attr::GLOBAL)) {
      if (foreignModId == 0 && decl.getKind() == DeclSymbol::Kind::BUILTIN_CMD) {
        this->builtinCmd->addDecl(decl);
      } else {
        this->scope->addDecl(decl);
      }
    }
  }
  // resolve inlined imported symbols
  for (auto &e : index->getInlinedModIds()) {
    this->importForeignDecls(e, inlined);
  }
  return true;
}

const Symbol *IndexBuilder::addMemberImpl(const DSType &recv, const NameInfo &info,
                                          DeclSymbol::Kind kind, const MethodHandle *handle) {
  std::string name = mangleSymbolName(kind, info);
  if (name.empty()) {
    return nullptr;
  }
  auto entry = this->memberMap.find(recv, name, kind == DeclSymbol::Kind::METHOD);
  if (!entry.hasValue() && handle) {
    entry = std::ref(*handle);
  }
  if (!entry.hasValue()) {
    return nullptr;
  }
  auto *decl = this->resolveMemberDecl(recv, info, kind, entry);
  if (auto *symbol = this->addSymbolImpl(info.getToken(), decl)) {
    auto symbolRef = SymbolRef::create(info.getToken(), this->modId);
    assert(symbolRef.hasValue());
    decl->addRef(symbolRef.unwrap());
    return symbol;
  }
  return nullptr;
}

DeclBase *IndexBuilder::resolveMemberDecl(const DSType &recv, const NameInfo &nameInfo,
                                          DeclSymbol::Kind kind, MemberRef &entry) {
  assert(entry.hasValue());
  if (is<SymbolRef>(entry)) {
    auto &declRef = get<SymbolRef>(entry);
    if (this->modId == declRef.getModId()) {
      auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), declRef.getPos(),
                                   DeclSymbol::Compare());
      if (iter != this->decls.end() && iter->getPos() == declRef.getPos()) {
        return &*iter;
      }
    } else {
      SymbolRequest request = {.modId = declRef.getModId(), .pos = declRef.getPos()};
      auto iter = std::lower_bound(this->foreigns.begin(), this->foreigns.end(), request,
                                   ForeignDecl::Compare());
      if (iter == this->foreigns.end() || (*iter).getModId() != request.modId ||
          (*iter).getPos() != request.pos) { // not found, add decl
        iter = this->foreigns.insert(iter, ForeignDecl(declRef));
      }
      return &*iter;
    }
  } else if (is<FieldRef>(entry)) {
    /**
     * for builtin type field (may be Tuple)
     */
    auto &fieldRef = get<FieldRef>(entry);
    auto &fieldType = this->getPool().get(fieldRef.get().getTypeId());
    auto attr = DeclSymbol::Attr::PUBLIC | DeclSymbol::Attr::GLOBAL | DeclSymbol::Attr::BUILTIN;
    auto *decl = this->addDeclImpl(kind, attr, nameInfo, fieldType.getName(), false);
    if (decl) {
      this->memberMap.add(recv, *decl);
    }
    return decl;
  } else if (is<MethodRef>(entry)) {
    /**
     * for builtin method
     */
    auto &handle = get<MethodRef>(entry).get();
    auto attr = DeclSymbol::Attr::PUBLIC | DeclSymbol::Attr::GLOBAL | DeclSymbol::Attr::BUILTIN;
    assert(handle.isNative());
    std::string content = "($this";
    for (unsigned int i = 0; i < handle.getParamSize(); i++) {
      content += ", ";
      content += "$p";
      content += std::to_string(i);
      content += " : ";
      content += handle.getParamTypeAt(i).getNameRef();
    }
    content += ") : ";
    content += handle.getReturnType().getNameRef();
    content += " for ";
    content += handle.getRecvType().getNameRef();
    auto *decl = this->addDeclImpl(kind, attr, nameInfo, content.c_str(), false);
    if (decl) {
      this->memberMap.add(recv, *decl);
    }
    return decl;
  }
  return nullptr;
}

const DeclSymbol *IndexBuilder::findDecl(const Symbol &symbol) const {
  if (symbol.getDeclModId() == this->modId) {
    auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), symbol.getDeclPos(),
                                 DeclSymbol::Compare());
    if (iter == this->decls.end() || iter->getPos() != symbol.getDeclPos()) {
      return nullptr;
    }
    return &(*iter);
  } else {
    auto *index = this->memberMap.indexes.findDecl({
        .modId = symbol.getDeclModId(),
        .pos = symbol.getDeclPos(),
    });
    return index;
  }
}

DeclSymbol *IndexBuilder::addDeclImpl(DeclSymbol::Kind k, DeclSymbol::Attr attr,
                                      const NameInfo &nameInfo, const char *info, bool checkScope) {
  std::string name = mangleSymbolName(k, nameInfo);
  if (name.empty()) {
    return nullptr;
  }

  // create DeclSymbol
  const Token token = nameInfo.getToken();
  auto ret = DeclSymbol::create(k, attr, nameInfo.getToken(), this->modId, name, info);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto &decl = ret.unwrap();
  if (checkScope && !this->scope->addDecl(decl)) { // register name to scope
    return nullptr;
  }

  auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), decl.getPos(),
                               DeclSymbol::Compare());
  if (iter != this->decls.end() && iter->getPos() == decl.getPos()) {
    fatal("try to add token: %s, but already added: %s\n", toString(token).c_str(),
          toString(iter->getToken()).c_str());
  }
  iter = this->decls.insert(iter, std::move(decl));
  return &(*iter);
}

const Symbol *IndexBuilder::addSymbolImpl(Token token, const DeclBase *decl) {
  if (!decl) {
    return nullptr;
  }
  auto ret = Symbol::create(token, *decl);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto &symbol = ret.unwrap();
  auto iter = std::lower_bound(this->symbols.begin(), this->symbols.end(), symbol.getPos(),
                               Symbol::Compare());
  if (iter != this->symbols.end()) {
    if (iter->getPos() == decl->getPos()) {
      fatal("try to add token: %s, but already added: %s\n", toString(token).c_str(),
            toString(iter->getToken()).c_str());
    }
  }
  iter = this->symbols.insert(iter, symbol);
  return &(*iter);
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
  if (!node.isUntyped() && !node.getType().isVoidType() && !node.getType().isNothingType()) {
    NameInfo info(node.getToken(), node.getVarName());
    this->builder().addSymbol(info);
  }
}

void SymbolIndexer::visitAccessNode(AccessNode &node) {
  this->visit(node.getRecvNode());
  if (!node.isUntyped() && !node.getType().isNothingType()) {
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
  if (node.isMethodCall() && node.getHandle()) {
    auto &accessNode = cast<AccessNode>(node.getExprNode());
    NameInfo nameInfo(accessNode.getNameNode().getToken(), accessNode.getFieldName());
    this->builder().addMethod(*node.getHandle(), nameInfo);
  }
  this->visit(node.getArgsNode());
}

void SymbolIndexer::visitEmbedNode(EmbedNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitNewNode(NewNode &node) { // FIXME: constructor name ?
  this->visit(node.getTargetTypeNode());
  this->visit(node.getArgsNode());
}

void SymbolIndexer::visitForkNode(ForkNode &node) { this->visit(node.getExprNode()); }

static Optional<NameInfo> getConstArg(const std::vector<std::unique_ptr<Node>> &argsNode,
                                      unsigned int offset = 0) {
  const CmdArgNode *argNode = nullptr;
  unsigned int offsetCount = 0;
  for (unsigned int i = 0; i < argsNode.size() && offset <= offsetCount; i++) {
    if (isa<CmdArgNode>(*argsNode[i]) && offset == offsetCount++) {
      argNode = cast<CmdArgNode>(argsNode[i].get());
      break;
    }
  }
  if (argNode && argNode->getSegmentNodes().size() == 1 &&
      isa<StringNode>(*argNode->getSegmentNodes()[0])) {
    auto &strNode = cast<StringNode>(*argNode->getSegmentNodes()[0]);
    return NameInfo(strNode.getToken(), strNode.getValue());
  }
  return {};
}

void SymbolIndexer::visitCmdNode(CmdNode &node) {
  const Symbol *symbol = nullptr;
  if (auto &cmdName = node.getNameNode().getValue(); !cmdName.empty()) {
    NameInfo info(node.getNameNode().getToken(), cmdName);
    symbol = this->builder().addSymbol(info, DeclSymbol::Kind::CMD);
  }
  if (auto nameInfo = getConstArg(node.getArgNodes()); symbol && nameInfo.hasValue()) {
    if (auto *decl = this->builder().findDecl(*symbol);
        decl && decl->getKind() == DeclSymbol::Kind::MOD) { // resolve sub-command
      auto ret = decl->getInfoAsModId();
      if (ret.second) {
        auto &type = *this->builder().getPool().getModTypeById(ret.first).asOk();
        this->builder().addMember(type, nameInfo.unwrap(), DeclSymbol::Kind::CMD);
      }
    }
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

void SymbolIndexer::visitTypeDefNode(TypeDefNode &node) {
  switch (node.getDefKind()) {
  case TypeDefNode::ALIAS:
    this->visit(node.getTargetTypeNode());
    this->builder().addDecl(node.getNameInfo(), node.getTargetTypeNode().getType(),
                            DeclSymbol::Kind::TYPE_ALIAS);
    break;
  case TypeDefNode::ERROR_DEF:
    if (this->isTopLevel()) {
      this->visit(node.getTargetTypeNode());
      this->builder().addDecl(node.getNameInfo(), node.getTargetTypeNode().getType(),
                              DeclSymbol::Kind::ERROR_TYPE_DEF);
    }
    break;
  }
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

static DeclSymbol::Kind fromVarDeclKind(VarDeclNode::Kind k) {
  switch (k) {
  case VarDeclNode::VAR:
    return DeclSymbol::Kind::VAR;
  case VarDeclNode::LET:
    return DeclSymbol::Kind::LET;
  case VarDeclNode::IMPORT_ENV:
    return DeclSymbol::Kind::IMPORT_ENV;
  case VarDeclNode::EXPORT_ENV:
    return DeclSymbol::Kind::EXPORT_ENV;
  default:
    return DeclSymbol::Kind::VAR; // normally unreachable
  }
}

void SymbolIndexer::visitVarDeclNode(VarDeclNode &node) {
  this->visit(node.getExprNode());
  auto &type = node.getExprNode() ? node.getExprNode()->getType()
                                  : this->builder().getPool().get(TYPE::String);
  this->builder().addDecl(node.getNameInfo(), type, fromVarDeclKind(node.getKind()));
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
      this->builder().addDecl(info, leftNode.getType(), DeclSymbol::Kind::EXPORT_ENV);
    }
    this->visit(node.getExprNode());
  } else {
    this->visitEach(node.getAssignNodes());
  }
}

void SymbolIndexer::visitFunctionNode(FunctionNode &node) {
  if (!this->builder().isGlobal()) {
    return;
  }
  this->visitEach(node.getParamTypeNodes());
  this->visit(node.getReturnTypeToken());
  if (node.getVarIndex() > 0) {
    std::string value = "(";
    for (unsigned int i = 0; i < node.getParams().size(); i++) {
      if (i > 0) {
        value += ", ";
      }
      value += "$";
      value += node.getParams()[i].getName();
      value += " : ";
      value += node.getParamTypeNodes()[i]->getType().getName();
    }
    value += ") : ";
    value += node.getReturnTypeToken()->getType().getName();
    this->builder().addDecl(node.getNameInfo(), DeclSymbol::Kind::FUNC, value.c_str());
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
    const char *hover = this->builder().getPool().get(TYPE::Boolean).getName();
    if (node.getReturnTypeNode() && node.getReturnTypeNode()->getType().isNothingType()) {
      hover = node.getReturnTypeNode()->getType().getName();
    }
    this->builder().addDecl(node.getNameInfo(), DeclSymbol::Kind::CMD, hover);
  }
  auto udc = this->builder().intoScope();
  this->visitBlockWithCurrentScope(node.getBlockNode());
}

void SymbolIndexer::visitSourceNode(SourceNode &node) {
  if (!this->isTopLevel()) {
    return;
  }
  this->visitEach(node.getPathNode().getSegmentNodes());
  if (node.getNameInfo()) {
    this->builder().addDecl(*node.getNameInfo(), DeclSymbol::Kind::MOD,
                            std::to_string(node.getModType().getModId()).c_str());
  } else {
    this->builder().importForeignDecls(node.getModType().getModId(), node.isInlined());
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

bool SymbolIndexer::enterModule(unsigned short modId, int version,
                                const std::shared_ptr<TypePool> &p) {
  this->builders.emplace_back(modId, version, p, this->indexes);
  if (!this->indexes.find(0)) {
    this->builders.emplace_back(0, 0, p, this->indexes);
    this->addBuiltinSymbols();
    this->exitModule(nullptr);
  }
  this->builder().importForeignDecls(0, false);
  return true;
}

bool SymbolIndexer::exitModule(std::unique_ptr<Node> &&node) {
  assert(!this->builders.empty());
  auto index = std::make_shared<SymbolIndex>(std::move(this->builder()).build());
  this->builders.pop_back();
  this->indexes.add(std::move(index));
  this->visit(node);
  return true;
}

bool SymbolIndexer::consume(std::unique_ptr<Node> &&node) {
  this->visit(node);
  return true;
}

static DeclSymbol::Kind resolveDeclKind(const std::pair<std::string, FieldHandle> &entry) {
  if (isTypeAliasFullName(entry.first)) {
    assert(entry.second.has(FieldAttribute::TYPE_ALIAS));
    return DeclSymbol::Kind::TYPE_ALIAS;
  } else if (isCmdFullName(entry.first)) {
    return DeclSymbol::Kind::CMD;
  } else {
    if (entry.second.has(FieldAttribute::ENV)) {
      return DeclSymbol::Kind::IMPORT_ENV;
    }
    if (entry.second.has(FieldAttribute::READ_ONLY)) {
      return DeclSymbol::Kind::LET;
    }
    return DeclSymbol::Kind::VAR;
  }
}

void SymbolIndexer::addBuiltinSymbols() {
  auto &modType = this->builder().getPool().getBuiltinModType();
  unsigned int offset = 0;
  for (auto &e : modType.getHandleMap()) {
    const auto kind = resolveDeclKind(e);
    NameInfo nameInfo(Token{offset, 1}, DeclSymbol::demangle(kind, e.first));
    auto &type = this->builder().getPool().get(e.second.getTypeId());
    if (kind == DeclSymbol::Kind::CMD) {
      this->builder().addDecl(nameInfo, kind, "");
    } else if (auto iter = getBuiltinConstMap().find(nameInfo.getName());
               iter != getBuiltinConstMap().end()) {
      assert(type.is(TYPE::String));
      std::string value = "'";
      value += iter->second;
      value += "'";
      this->builder().addDecl(nameInfo, DeclSymbol::Kind::CONST, value.c_str());
    } else if (nameInfo.getName() == CVAR_SCRIPT_NAME || nameInfo.getName() == CVAR_SCRIPT_DIR) {
      this->builder().addDecl(nameInfo, DeclSymbol::Kind::MOD_CONST, "");
    } else {
      this->builder().addDecl(nameInfo, type, kind);
    }
    offset += 5;
  }

  // add builtin command
  unsigned int size = getBuiltinCmdSize();
  auto *cmdList = getBuiltinCmdDescList();
  for (unsigned int i = 0; i < size; i++) {
    NameInfo nameInfo(Token{offset, 1}, cmdList[i].name);
    this->builder().addDecl(nameInfo, DeclSymbol::Kind::BUILTIN_CMD, "");
    offset += 5;
  }
}

} // namespace ydsh::lsp