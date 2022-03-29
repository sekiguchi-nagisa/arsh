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

const SymbolRef *IndexBuilder::lookup(const std::string &mangledName, DeclSymbol::Kind kind,
                                      const Handle *handle) const {
  if (mangledName.empty()) {
    return nullptr;
  }

  unsigned short declModId;
  if (mangledName.size() == 1 && StringRef("#@0123456789").contains(mangledName[0])) {
    declModId = 0;
  } else if (handle) {
    declModId = handle->getModId();
  } else if (kind == DeclSymbol::Kind::CMD) { // for builtin
    declModId = 0;
  } else {
    return nullptr;
  }

  if (this->modId == declModId) {
    return this->scope->find(mangledName);
  } else {
    auto foreignIndex = this->indexes.find(declModId);
    assert(foreignIndex);
    return foreignIndex->findGlobal(mangledName);
  }
}

static StringRef trimTypeName(const DSType &type) {
  auto ref = type.getNameRef();
  if (auto pos = ref.find("."); pos != StringRef::npos) {
    ref = ref.substr(pos + 1);
  }
  return ref;
}

static std::string mangleSymbolName(const DSType *recv, DeclSymbol::Kind k,
                                    const NameInfo &nameInfo) {
  if (!static_cast<bool>(nameInfo) || nameInfo.getToken().size == 0) {
    return "";
  }
  if (DeclSymbol::isVarName(k) && nameInfo.getName()[0] == '%') {
    return "";
  }
  return DeclSymbol::mangle(recv ? recv->getNameRef() : "", k, nameInfo.getName());
}

const DeclSymbol *IndexBuilder::addDecl(const NameInfo &info, const DSType &type,
                                        DeclSymbol::Kind kind) {
  if (type.isUnresolved()) {
    return nullptr;
  }
  StringRef ref = trimTypeName(type);
  return this->addDecl(info, kind, ref.data());
}

const DeclSymbol *IndexBuilder::addDeclImpl(const DSType *recv, const NameInfo &info,
                                            DeclSymbol::Kind kind, const char *hover,
                                            bool builtin) {
  DeclSymbol::Attr attr = {};
  if (this->scope->isGlobal() || this->scope->isConstructor()) {
    setFlag(attr, DeclSymbol::Attr::GLOBAL);
  }
  if (info.getName()[0] != '_') {
    setFlag(attr, DeclSymbol::Attr::PUBLIC);
  }
  if (recv) {
    setFlag(attr, DeclSymbol::Attr::MEMBER);
  }
  if (builtin) {
    setFlag(attr, DeclSymbol::Attr::BUILTIN);
  }

  std::string mangledName = mangleSymbolName(recv, kind, info);
  if (auto *decl = this->insertNewDecl(kind, attr, info.getToken(), mangledName, hover)) {
    if (!this->insertNewSymbol(info.getToken(), decl)) {
      return nullptr;
    }
    return decl;
  }
  return nullptr;
}

const Symbol *IndexBuilder::addSymbolImpl(const DSType *recv, const NameInfo &nameInfo,
                                          DeclSymbol::Kind kind, const Handle *handle) {
  std::string name = mangleSymbolName(recv, kind, nameInfo);
  auto *ref = this->lookup(name, kind, handle);
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
      auto *ret = this->indexes.findDecl(request);
      if (!ret || !hasFlag(ret->getAttr(), DeclSymbol::Attr::GLOBAL | DeclSymbol::Attr::PUBLIC)) {
        return nullptr;
      }
      iter = this->foreigns.insert(iter, ForeignDecl(*ret));
    }
    decl = &*iter;
  }
  if (auto *symbol = this->insertNewSymbol(nameInfo.getToken(), decl)) {
    auto symbolRef = SymbolRef::create(nameInfo.getToken(), this->modId);
    assert(symbolRef.hasValue());
    decl->addRef(symbolRef.unwrap());
    return symbol;
  }
  return nullptr;
}

bool IndexBuilder::addThis(const NameInfo &info, HandlePtr handle) {
  auto *methodScope = this->curScope().findMethodScope();
  assert(methodScope);
  if (methodScope->find(mangleSymbolName(nullptr, DeclSymbol::Kind::LET, info))) {
    return this->addSymbol(info, DeclSymbol::Kind::LET, std::move(handle));
  } else {
    return this->addDecl(info, *methodScope->getResolvedType(), DeclSymbol::Kind::LET);
  }
}

const DeclSymbol *IndexBuilder::addMemberDecl(const DSType &recv, const NameInfo &nameInfo,
                                              const DSType &type, DeclSymbol::Kind kind) {
  if (type.isUnresolved()) {
    return nullptr;
  }
  std::string content = trimTypeName(type).toString();
  content += " for ";
  content += trimTypeName(recv);
  return this->addMemberDecl(recv, nameInfo, kind, content.c_str());
}

static std::string generateBuiltinFieldOrMethodInfo(const TypePool &pool, const DSType &recv,
                                                    const Handle &handle) {
  if (handle.isMethod()) {
    /**
     * for builtin method
     */
    auto &methodHandle = cast<MethodHandle>(handle);
    assert(methodHandle.isNative());
    std::string content = "(";
    for (unsigned int i = 0; i < methodHandle.getParamSize(); i++) {
      if (i > 0) {
        content += ", ";
      }
      content += "$p";
      content += std::to_string(i);
      content += " : ";
      content += trimTypeName(methodHandle.getParamTypeAt(i));
    }
    content += ") : ";
    content += trimTypeName(methodHandle.getReturnType());
    content += " for ";
    content += trimTypeName(recv);
    return content;
  } else {
    /**
     * for builtin type field (must be Tuple)
     */
    assert(recv.isTupleType());
    auto &fieldType = pool.get(handle.getTypeId());
    std::string content = trimTypeName(fieldType).toString();
    content += " for ";
    content += trimTypeName(recv);
    return content;
  }
}

bool IndexBuilder::addMember(const DSType &recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                             const Handle &handle) {
  const DSType *acutalRecv = &recv;
  if (recv.isModType() && !handle.isMethod()) {
    acutalRecv = nullptr;
  }

  if (this->addSymbolImpl(acutalRecv, nameInfo, kind, &handle)) {
    return true;
  }
  if (handle.getModId() == 0) { // tuple field or builtin method
    std::string hover = generateBuiltinFieldOrMethodInfo(this->getPool(), recv, handle);
    if (this->addDeclImpl(&recv, nameInfo, kind, hover.c_str(), true)) {
      return true;
    }
  }
  return false;
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
    auto *index = this->indexes.findDecl({
        .modId = symbol.getDeclModId(),
        .pos = symbol.getDeclPos(),
    });
    return index;
  }
}

DeclSymbol *IndexBuilder::insertNewDecl(DeclSymbol::Kind k, DeclSymbol::Attr attr, Token token,
                                        const std::string &mangledName, const char *info) {
  if (mangledName.empty()) {
    return nullptr;
  }

  // create DeclSymbol
  auto ret = DeclSymbol::create(k, attr, token, this->modId, mangledName, info);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto &decl = ret.unwrap();
  if (!this->scope->addDecl(decl)) { // register name to scope
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

const Symbol *IndexBuilder::insertNewSymbol(Token token, const DeclBase *decl) {
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
                              DeclSymbol::Kind::TYPE_ALIAS, base.getHandle());
    break;
  }
  case TypeNode::Qualified: {
    auto &type = cast<QualifiedTypeNode>(node);
    this->visit(type.getRecvTypeNode());
    if (!type.isUntyped()) {
      NameInfo info(type.getNameTypeNode().getToken(), type.getNameTypeNode().getTokenText());
      this->builder().addMember(type.getRecvTypeNode().getType(), info,
                                DeclSymbol::Kind::TYPE_ALIAS, *type.getNameTypeNode().getHandle());
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
    if (this->builder().isReceiver(node.getVarName(), *node.getHandle())) {
      this->builder().addThis(info, node.getHandle());
    } else {
      this->builder().addSymbol(info, DeclSymbol::Kind::VAR, node.getHandle());
    }
  }
}

void SymbolIndexer::visitAccessNode(AccessNode &node) {
  this->visit(node.getRecvNode());
  assert(!node.isUntyped());
  if (node.getHandle()) {
    NameInfo info(node.getNameNode().getToken(), node.getFieldName());
    this->builder().addMember(node.getRecvNode().getType(), info, DeclSymbol::Kind::VAR,
                              *node.getHandle());
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
  if (node.isMethodCall() && node.getHandle()) {
    auto &accessNode = cast<AccessNode>(node.getExprNode());
    this->visit(accessNode.getRecvNode());
    NameInfo nameInfo(accessNode.getNameNode().getToken(), accessNode.getFieldName());
    this->builder().addMember(*node.getHandle(), nameInfo);
  } else if (node.isFuncCall()) {
    this->visit(node.getExprNode());
  }
  this->visit(node.getArgsNode());
}

void SymbolIndexer::visitEmbedNode(EmbedNode &node) { this->visit(node.getExprNode()); }

void SymbolIndexer::visitNewNode(NewNode &node) {
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
    symbol = this->builder().addSymbol(info, DeclSymbol::Kind::CMD, node.getHandle());
  }
  if (auto nameInfo = getConstArg(node.getArgNodes()); symbol && nameInfo.hasValue()) {
    if (auto *decl = this->builder().findDecl(*symbol);
        decl && decl->getKind() == DeclSymbol::Kind::MOD) { // resolve sub-command
      auto ret = decl->getInfoAsModId();
      if (ret.second) {
        auto &type = *this->builder().getPool().getModTypeById(ret.first).asOk();
        auto handle = cast<ModType>(type).lookup(this->builder().getPool(),
                                                 toCmdFullName(nameInfo.unwrap().getName()));
        this->builder().addSymbol(nameInfo.unwrap(), DeclSymbol::Kind::CMD, handle);
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
  auto block = this->builder().intoScope(ScopeKind::BLOCK);
  this->visitBlockWithCurrentScope(node);
}

void SymbolIndexer::visitTypeDefNode(TypeDefNode &node) {
  switch (node.getDefKind()) {
  case TypeDefNode::ALIAS:
    this->visit(node.getTargetTypeNode());
    if (this->builder().curScope().isConstructor()) {
      this->builder().addMemberDecl(*this->builder().curScope().getResolvedType(),
                                    node.getNameInfo(), node.getTargetTypeNode().getType(),
                                    DeclSymbol::Kind::TYPE_ALIAS);
    } else {
      this->builder().addDecl(node.getNameInfo(), node.getTargetTypeNode().getType(),
                              DeclSymbol::Kind::TYPE_ALIAS);
    }
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

void SymbolIndexer::visitDeferNode(DeferNode &node) { this->visit(node.getBlockNode()); }

void SymbolIndexer::visitLoopNode(LoopNode &node) {
  auto block = this->builder().intoScope(ScopeKind::BLOCK);
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
  auto block = this->builder().intoScope(ScopeKind::BLOCK);
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
  if (this->builder().curScope().isConstructor()) {
    this->builder().addMemberDecl(*this->builder().curScope().getResolvedType(), node.getNameInfo(),
                                  type, fromVarDeclKind(node.getKind()));
  } else {
    this->builder().addDecl(node.getNameInfo(), type, fromVarDeclKind(node.getKind()));
  }
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
    auto block = this->builder().intoScope(ScopeKind::BLOCK);
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

static std::string generateFuncInfo(const FunctionNode &node) {
  assert(!node.isConstructor() && !node.isAnonymousFunc());

  std::string value = "(";
  for (unsigned int i = 0; i < node.getParamNodes().size(); i++) {
    auto &paramNode = node.getParamNodes()[i];
    if (i > 0) {
      value += ", ";
    }
    value += "$";
    value += paramNode->getVarName();
    value += " : ";
    value += trimTypeName(paramNode->getExprNode()->getType());
  }
  value += ") : ";
  value += trimTypeName(node.getReturnTypeNode()->getType());
  if (node.isMethod()) {
    value += " for ";
    value += trimTypeName(node.getRecvTypeNode()->getType());
  }
  return value;
}

static std::string generateConstructorInfo(const TypePool &pool, const FunctionNode &node) {
  assert(node.isConstructor());

  std::string value;
  if (unsigned int size = node.getParamNodes().size(); size > 0) {
    value += "(";
    for (unsigned int i = 0; i < size; i++) {
      auto &paramNode = node.getParamNodes()[i];
      if (i > 0) {
        value += ", ";
      }
      value += "$";
      value += paramNode->getVarName();
      value += " : ";
      value += trimTypeName(paramNode->getExprNode()->getType());
    }
    value += ")";
  }
  value += " {\n";
  for (auto &e : node.getBlockNode().getNodes()) {
    if (isa<VarDeclNode>(*e)) {
      auto &declNode = cast<VarDeclNode>(*e);
      auto declKind = fromVarDeclKind(declNode.getKind());
      value += "    ";
      value += DeclSymbol::getVarDeclPrefix(declKind);
      value += " ";
      value += declNode.getVarName();
      value += " : ";
      if (declNode.getExprNode()) {
        value += trimTypeName(declNode.getExprNode()->getType());
      } else {
        value += trimTypeName(pool.get(TYPE::String));
      }
      value += "\n";
    } else if (isa<TypeDefNode>(*e)) {
      auto &defNode = cast<TypeDefNode>(*e);
      if (defNode.getDefKind() == TypeDefNode::ALIAS) {
        value += "    typedef ";
        value += defNode.getName();
        value += " = ";
        value += trimTypeName(defNode.getTargetTypeNode().getType());
        value += "\n";
      }
    }
  }
  value += "}";
  return value;
}

static ScopeKind getScopeKind(const FunctionNode &node) {
  if (node.isConstructor()) {
    return ScopeKind::CONSTRUCTOR;
  } else if (node.isMethod()) {
    return ScopeKind::METHOD;
  } else {
    return ScopeKind::FUNC;
  }
}

void SymbolIndexer::visitFunctionNode(FunctionNode &node) {
  if (!this->builder().isGlobal() || node.getType().isUnresolved()) {
    return;
  }

  if (node.getHandle()) {
    if (node.isConstructor()) {
      auto value = generateConstructorInfo(this->builder().getPool(), node);
      this->builder().addDecl(node.getNameInfo(), DeclSymbol::Kind::CONSTRUCTOR, value.c_str());
    } else if (node.isMethod()) {
      auto value = generateFuncInfo(node);
      this->builder().addMemberDecl(node.getRecvTypeNode()->getType(), node.getNameInfo(),
                                    DeclSymbol::Kind::METHOD, value.c_str());
    } else {
      auto value = generateFuncInfo(node);
      this->builder().addDecl(node.getNameInfo(), DeclSymbol::Kind::FUNC, value.c_str());
    }
  }
  for (auto &paramNode : node.getParamNodes()) {
    this->visit(paramNode->getExprNode());
  }
  this->visit(node.getReturnTypeNode());
  this->visit(node.getRecvTypeNode());
  auto func = this->builder().intoScope(getScopeKind(node), node.isMethod()
                                                                ? &node.getRecvTypeNode()->getType()
                                                                : node.getResolvedType());
  for (auto &paramNode : node.getParamNodes()) {
    this->builder().addDecl(paramNode->getNameInfo(), paramNode->getExprNode()->getType());
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
  auto udc = this->builder().intoScope(ScopeKind::FUNC);
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

static DeclSymbol::Kind resolveDeclKind(const std::pair<std::string, HandlePtr> &entry) {
  if (isTypeAliasFullName(entry.first)) {
    assert(entry.second->is(HandleKind::TYPE_ALIAS));
    return DeclSymbol::Kind::TYPE_ALIAS;
  } else if (isCmdFullName(entry.first)) {
    return DeclSymbol::Kind::CMD;
  } else {
    if (entry.second->is(HandleKind::ENV)) {
      return DeclSymbol::Kind::IMPORT_ENV;
    }
    if (entry.second->has(HandleAttr::READ_ONLY)) {
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
    auto &type = this->builder().getPool().get(e.second->getTypeId());
    if (kind == DeclSymbol::Kind::CMD) {
      this->builder().addDecl(nameInfo, kind, "");
    } else if (auto *ptr = this->sysConfig.lookup(nameInfo.getName())) {
      assert(type.is(TYPE::String));
      std::string value = "'";
      value += *ptr;
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