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

#include <arg_parser_base.h>
#include <cmd_desc.h>
#include <format_signature.h>

#include "indexer.h"
#include "symbol.h"

namespace arsh::lsp {

// ##########################
// ##     IndexBuilder     ##
// ##########################

#define LOG(L, ...)                                                                                \
  do {                                                                                             \
    if (this->logger) {                                                                            \
      auto &_logger = (*this->logger);                                                             \
      _logger.enabled(L) && (_logger)(L, __VA_ARGS__);                                             \
    }                                                                                              \
  } while (false)

bool IndexBuilder::ScopeEntry::addDecl(const DeclSymbol &decl) {
  bool r1 = this->addDeclWithSpecifiedName(decl.getMangledName().toString(), decl);
  if (decl.is(DeclSymbol::Kind::MOD)) {
    // register udc
    std::string name = DeclSymbol::mangle(DeclSymbol::Kind::CMD, decl.getMangledName());
    bool r2 = this->addDeclWithSpecifiedName(std::move(name), decl);

    // register type alias
    name = DeclSymbol::mangle(DeclSymbol::Kind::TYPE_ALIAS, decl.getMangledName());
    bool r3 = this->addDeclWithSpecifiedName(std::move(name), decl);
    r1 = r1 && r2 && r3;
  }
  return r1;
}

static bool isTupleField(const std::string &mangledName, DeclSymbol::Kind kind,
                         const Handle &handle) {
  if (isBuiltinMod(handle.getModId()) && kind == DeclSymbol::Kind::VAR && mangledName.size() > 1) {
    return DeclSymbol::mayBeMemberName(mangledName); // tuple field
  }
  return false;
}

const SymbolRef *IndexBuilder::lookup(const std::string &mangledName, DeclSymbol::Kind kind,
                                      const Handle *handle) const {
  if (mangledName.empty()) {
    return nullptr;
  }

  ModId declModId;
  if (mangledName.size() == 1 && StringRef("#@0123456789").contains(mangledName[0])) {
    declModId = BUILTIN_MOD_ID;
  } else if (handle) {
    declModId = handle->getModId();
    if (isTupleField(mangledName, kind, *handle)) {
      declModId = this->getModId();
    }
  } else if (kind == DeclSymbol::Kind::CMD || kind == DeclSymbol::Kind::TYPE_ALIAS) { // for builtin
    declModId = BUILTIN_MOD_ID;
  } else {
    return nullptr;
  }

  if (this->getModId() == declModId) {
    return this->scope->find(mangledName);
  } else {
    auto foreignIndex = this->indexes.find(declModId);
    assert(foreignIndex);
    return foreignIndex->findGlobal(mangledName);
  }
}

static std::string mangleSymbolName(const Type *recv, DeclSymbol::Kind k,
                                    const NameInfo &nameInfo) {
  if (!static_cast<bool>(nameInfo) || nameInfo.getToken().size == 0) {
    return "";
  }
  if (DeclSymbol::isVarName(k) && nameInfo.getName()[0] == '%') {
    return "";
  }
  StringRef recvTypeName;
  if (recv) {
    if (isa<ArrayType>(recv)) {
      recvTypeName = TYPE_ARRAY;
    } else if (isa<MapType>(recv)) {
      recvTypeName = TYPE_MAP;
    } else {
      recvTypeName = recv->getNameRef();
    }
  }
  return DeclSymbol::mangle(recvTypeName, k, nameInfo.getName());
}

const DeclSymbol *IndexBuilder::addDecl(const NameInfo &info, const Type &type, Token token,
                                        DeclSymbol::Kind kind) {
  if (type.isUnresolved()) {
    return nullptr;
  }
  return this->addDecl(info, kind, normalizeTypeName(type).c_str(), token);
}

const DeclSymbol *IndexBuilder::addDeclImpl(const Type *recv, const NameInfo &info,
                                            DeclSymbol::Kind kind, const char *hover, Token body,
                                            DeclInsertOp op) {
  DeclSymbol::Attr attr = {};
  if (this->scope->isGlobal() || this->scope->isConstructor() || op == DeclInsertOp::BUILTIN ||
      op == DeclInsertOp::PARAM) {
    setFlag(attr, DeclSymbol::Attr::GLOBAL);
  }
  if (info.getName()[0] != '_' || kind == DeclSymbol::Kind::PARAM ||
      kind == DeclSymbol::Kind::GENERIC_METHOD_PARAM) { // PARAM is always public
    setFlag(attr, DeclSymbol::Attr::PUBLIC);
  }
  if (recv) {
    setFlag(attr, DeclSymbol::Attr::MEMBER);
  }
  if (op == DeclInsertOp::BUILTIN) {
    setFlag(attr, DeclSymbol::Attr::BUILTIN);
  }

  std::string mangledName = mangleSymbolName(recv, kind, info);
  if (mangledName.empty()) {
    return nullptr;
  }
  DeclSymbol::Name name = {
      .token = info.getToken(),
      .name = CStrPtr(strdup(mangledName.c_str())),
  };
  if (auto *decl = this->insertNewDecl(kind, attr, std::move(name), hover, body, op)) {
    if (!this->insertNewSymbol(info.getToken(), decl)) {
      return nullptr;
    }
    return decl;
  }
  return nullptr;
}

const Symbol *IndexBuilder::addSymbolImpl(const Type *recv, const NameInfo &nameInfo,
                                          DeclSymbol::Kind kind, const Handle *handle) {
  std::string name = mangleSymbolName(recv, kind, nameInfo);
  auto *ref = this->lookup(name, kind, handle);
  if (!ref) {
    return nullptr;
  }

  DeclBase *decl;
  if (ref->getModId() == this->getModId()) {
    auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), ref->getPos(),
                                 DeclSymbol::Compare());
    if (iter == this->decls.end()) {
      return nullptr;
    }
    decl = &*iter;
    assert(decl->getPos() == ref->getPos());
  } else { // foreign decl
    SymbolRequest request = {.modId = ref->getModId(), .pos = ref->getPos()};
    auto iter = std::lower_bound(this->foreign.begin(), this->foreign.end(), request,
                                 ForeignDecl::Compare());
    if (iter == this->foreign.end() || iter->getModId() != request.modId ||
        iter->getPos() != request.pos) { // not found, register foreign decl
      auto *ret = this->indexes.findDecl(request);
      if (!ret || (!ret->has(DeclSymbol::Attr::GLOBAL | DeclSymbol::Attr::PUBLIC) &&
                   !isBuiltinMod(ref->getModId()))) {
        return nullptr;
      }
      iter = this->foreign.insert(iter, ForeignDecl(*ret));
    }
    decl = &*iter;
  }
  if (auto *symbol = this->insertNewSymbol(nameInfo.getToken(), decl)) {
    auto symbolRef = SymbolRef::create(nameInfo.getToken(), this->getModId());
    assert(symbolRef.hasValue());
    decl->addRef(symbolRef.unwrap());
    return symbol;
  }
  return nullptr;
}

const Symbol *IndexBuilder::addCmd(const NameInfo &info, const HandlePtr &hd) {
  auto *symbol = this->addSymbol(info, DeclSymbol::Kind::CMD, hd);
  if (!symbol) { // external commands
    this->externalCmdSet.emplace(info.getName());
  }
  return symbol;
}

void IndexBuilder::addTypeInheritance(const Type &type) {
  if (type.isRecordOrDerived() || type.typeKind() == TypeKind::Error || type.is(TYPE::Error)) {
    auto *baseType = type.getSuperType();
    if (!baseType || baseType->is(TYPE::Any)) {
      return;
    }
    this->inheritanceMap.emplace(TypeWrapper(type.getNameRef()),
                                 TypeWrapper(baseType->getNameRef()));
  }
}

bool IndexBuilder::addThis(const NameInfo &info, const HandlePtr &handle) {
  auto *methodScope = this->curScope().findMethodScope();
  assert(methodScope);
  if (methodScope->find(mangleSymbolName(nullptr, DeclSymbol::Kind::THIS, info))) {
    return this->addSymbol(info, DeclSymbol::Kind::THIS, handle);
  } else {
    return this->addDecl(info, *methodScope->getResolvedType(), info.getToken(),
                         DeclSymbol::Kind::THIS);
  }
}

const DeclSymbol *IndexBuilder::addMemberDecl(const Type &recv, const NameInfo &nameInfo,
                                              const Type &type, DeclSymbol::Kind kind,
                                              Token token) {
  if (type.isUnresolved()) {
    return nullptr;
  }
  std::string content = normalizeTypeName(type);
  content += " for ";
  normalizeTypeName(recv, content);
  return this->addMemberDecl(recv, nameInfo, kind, content.c_str(), token);
}

static std::string generateTupleInfo(const TypePool &pool, const Type &recv, const Handle &handle) {
  if (handle.isMethodHandle()) { // for builtin method
    auto &methodHandle = cast<MethodHandle>(handle);
    assert(methodHandle.isNative());
    std::string content;
    formatMethodSignature(recv, methodHandle, content);
    return content;
  } else {
    /**
     * for builtin type field (must be Tuple)
     */
    assert(recv.isTupleType());
    auto &fieldType = pool.get(handle.getTypeId());
    std::string content = normalizeTypeName(fieldType);
    content += " for ";
    normalizeTypeName(recv, content);
    return content;
  }
}

bool IndexBuilder::addMember(const Type &recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                             const Handle &handle, Token token) {
  const Type *actualRecv = &recv;
  if (recv.isModType() && !handle.isMethodHandle()) {
    actualRecv = nullptr;
  }

  if (this->addSymbolImpl(actualRecv, nameInfo, kind, &handle)) {
    if (handle.isMethodHandle() && cast<MethodHandle>(handle).isNative()) {
      this->addParamTypeInfo(nameInfo.getToken(), recv);
    }
    return true;
  }
  if (recv.isTupleType()) { // tuple field
    std::string hover = generateTupleInfo(this->getPool(), recv, handle);
    if (this->addDeclImpl(&recv, nameInfo, kind, hover.c_str(), token, DeclInsertOp::BUILTIN)) {
      return true;
    }
  }
  return false;
}

static std::string mangleParamNameImpl(StringRef funcName, const Type *recvType,
                                       StringRef paramName) {
  std::string name = paramName.toString();
  name += '+';
  name += funcName;
  if (recvType) {
    name += '@';
    if (isa<ArrayType>(recvType)) {
      name += "[]";
    } else if (isa<MapType>(recvType)) {
      name += "[:]";
    } else {
      name += recvType->getNameRef();
    }
  }
  return name;
}

std::string IndexBuilder::mangleParamName(StringRef funcName, const Handle &handle,
                                          StringRef paramName) const {
  const Type *recvType = nullptr;
  if (handle.isMethodHandle()) {
    auto &methodHandle = cast<MethodHandle>(handle);
    recvType = &this->getPool().get(methodHandle.getRecvTypeId());
    if (methodHandle.isConstructor()) {
      funcName = OP_INIT;
    }
  }
  return mangleParamNameImpl(funcName, recvType, paramName);
}

const DeclSymbol *IndexBuilder::addParamDecl(const NameInfo &info, const Type &type, Token token,
                                             StringRef funcName, const Handle &handle) {
  if (type.isUnresolved()) {
    return nullptr;
  }
  NameInfo newInfo(info.getToken(), mangleParamName(funcName, handle, info.getName()));
  std::string hover = normalizeTypeName(type);
  return this->addParamDeclImpl(newInfo, hover, token);
}

const Symbol *IndexBuilder::addNamedArgSymbol(const NameInfo &nameInfo, const Handle &handle,
                                              StringRef funcName) {
  NameInfo newNameInfo(nameInfo.getToken(),
                       this->mangleParamName(funcName, handle, nameInfo.getName()));
  auto *ret = this->addSymbol(newNameInfo, DeclSymbol::Kind::PARAM, &handle);
  if (ret) {
    if (handle.isMethodHandle() && cast<MethodHandle>(handle).isNative()) {
      auto &recv = this->getPool().get(handle.getTypeId());
      this->addParamTypeInfo(nameInfo.getToken(), recv);
    }
  }
  return ret;
}

bool IndexBuilder::addBuiltinMethod(const Type &recvType, unsigned int methodIndex,
                                    DummyTokenGenerator &tokenGen, StringRef name) {
  assert(isBuiltinMod(this->getModId()));
  const NameInfo nameInfo(tokenGen.next(), name.toString());
  if (recvType.typeKind() == TypeKind::Builtin) {
    std::string content;
    auto info = &nativeFuncInfoTable()[methodIndex];
    formatNativeMethodSignature(info, "T0;T1", content, [&](StringRef ref) {
      auto r = ref.find(": ");
      auto paramName = ref.substr(0, r);
      auto paramType = ref.substr(r + 2).toString();
      NameInfo newNameInfo(tokenGen.next(), mangleParamNameImpl(name, &recvType, paramName));
      this->addParamDeclImpl(newNameInfo, paramType, newNameInfo.getToken());
    });
    return this->addDeclImpl(&recvType, nameInfo, DeclSymbol::Kind::METHOD, content.c_str(),
                             nameInfo.getToken(), DeclInsertOp::BUILTIN);
  } else if (isa<ArrayType>(recvType) || isa<MapType>(recvType)) {
    auto content = std::to_string(methodIndex);
    unsigned int paramIndex = 0;
    iteratePackedParamNames(
        nativeFuncInfoTable()[methodIndex].params, [&](StringRef paramName, bool) {
          std::string cc = content;
          cc += ":";
          cc += std::to_string(paramIndex);
          NameInfo newNameInfo(tokenGen.next(), mangleParamNameImpl(name, &recvType, paramName));
          this->addDeclImpl(nullptr, newNameInfo, DeclSymbol::Kind::GENERIC_METHOD_PARAM,
                            cc.c_str(), newNameInfo.getToken(), DeclInsertOp::PARAM);
          paramIndex++;
          return true;
        });
    return this->addDeclImpl(&recvType, nameInfo, DeclSymbol::Kind::GENERIC_METHOD, content.c_str(),
                             nameInfo.getToken(), DeclInsertOp::BUILTIN);
  }
  return false;
}

const DeclSymbol *IndexBuilder::findDecl(const Symbol &symbol) const {
  if (symbol.getDeclModId() == this->getModId()) {
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

void IndexBuilder::addHereDocStartEnd(const NameInfo &start, Token end) {
  // insert here start
  DeclSymbol::Name name = {
      .token = start.getToken(),
      .name = CStrPtr(strdup(start.getName().c_str())),
  };
  auto *decl =
      this->insertNewDecl(DeclSymbol::Kind::HERE_START, DeclSymbol::Attr{}, std::move(name),
                          "here document start word", start.getToken(), DeclInsertOp::NONE);
  if (!decl) {
    return;
  }
  if (!this->insertNewSymbol(start.getToken(), decl)) {
    return;
  }

  // insert here end
  if (end.size > 0 && this->insertNewSymbol(end, decl)) {
    auto symbolRef = SymbolRef::create(end, this->getModId());
    assert(symbolRef.hasValue());
    decl->addRef(symbolRef.unwrap());
  }
}

static bool isMemberDecl(const DeclSymbol &decl) {
  auto kind = decl.getKind();
  if (kind == DeclSymbol::Kind::TYPE_ALIAS) {
    return true;
  }
  StringRef prefix = DeclSymbol::getVarDeclPrefix(kind);
  return !prefix.empty();
}

DeclSymbol *IndexBuilder::insertNewDecl(DeclSymbol::Kind k, DeclSymbol::Attr attr,
                                        DeclSymbol::Name &&name, const char *info, Token body,
                                        DeclInsertOp op) {
  // create DeclSymbol
  auto ret = DeclSymbol::create(k, attr, std::move(name), this->getModId(), info, body,
                                this->scope->scopeId);
  if (!ret.hasValue()) {
    return nullptr;
  }
  auto &decl = ret.unwrap();

  // register name to scope
  switch (op) {
  case DeclInsertOp::NORMAL:
    if (!this->scope->addDecl(decl)) {
      return nullptr;
    }
    break;
  case DeclInsertOp::BUILTIN:
  case DeclInsertOp::MEMBER:
  case DeclInsertOp::PARAM: {
    ScopeEntry *global = this->scope.get();
    while (!global->isGlobal()) {
      global = global->parent.get();
    }
    if (op == DeclInsertOp::MEMBER && !this->scope->isGlobal() && isMemberDecl(decl)) {
      /**
       * for field/type-alias access from constructor
       */
      auto mangledName = DeclSymbol::mangle(decl.getKind(), decl.toDemangledName());
      if (!this->scope->addDeclWithSpecifiedName(std::move(mangledName), decl)) {
        return nullptr;
      }
    }
    if (op == DeclInsertOp::PARAM && !this->scope->isGlobal()) {
      /**
       * for parameter access within function scope
       */
      auto mangledName = DeclSymbol::mangle(DeclSymbol::Kind::VAR, decl.toDemangledName());
      if (!this->scope->addDeclWithSpecifiedName(std::move(mangledName), decl)) {
        return nullptr;
      }
    }
    if (!global->addDecl(decl)) {
      return nullptr;
    }
    break;
  }
  case DeclInsertOp::NONE:
    break;
  }

  auto iter = std::lower_bound(this->decls.begin(), this->decls.end(), decl.getPos(),
                               DeclSymbol::Compare());
  if (iter != this->decls.end() && iter->getPos() == decl.getPos()) {
    LOG(LogLevel::ERROR, "try to add token: %s, but already added: %s\n",
        toString(decl.getToken()).c_str(), toString(iter->getToken()).c_str());
    return nullptr;
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
  if (iter != this->symbols.end() && iter->getPos() == symbol.getPos()) {
    LOG(LogLevel::ERROR, "try to add token: %s, but already added: %s\n",
        toString(symbol.getToken()).c_str(), toString(iter->getToken()).c_str());
    return nullptr;
  }
  iter = this->symbols.insert(iter, symbol);
  return &(*iter);
}

void IndexBuilder::addParamTypeInfo(Token token, const Type &type) {
  if (!isa<ArrayType>(type) && !isa<MapType>(type)) {
    return;
  }
  auto symbolRef = SymbolRef::create(token, this->getModId());
  if (symbolRef.hasValue()) {
    if (!this->packedParamTypesMap.addSymbol(symbolRef.unwrap(), type.typeId())) {
      LOG(LogLevel::ERROR, "try to add token: %s, but already added\n", toString(token).c_str());
      return;
    }
    if (!this->packedParamTypesMap.lookupByTypeId(type.typeId())) {
      std::string packed;
      if (isa<ArrayType>(type)) {
        packed += cast<ArrayType>(type).getElementType().getNameRef();
      } else {
        assert(isa<MapType>(type));
        auto &mapType = cast<MapType>(type);
        packed += mapType.getKeyType().getNameRef();
        packed += ";";
        packed += mapType.getValueType().getNameRef();
      }
      bool s = this->packedParamTypesMap.addPackedParamTypes(type.typeId(), std::move(packed));
      assert(s);
      (void)s;
    }
  }
}

// ###########################
// ##     SymbolIndexer     ##
// ###########################

void SymbolIndexer::visitBase(BaseTypeNode &node) {
  this->builder().addSymbol(NameInfo(node.getToken(), std::string(node.getTokenText())),
                            DeclSymbol::Kind::TYPE_ALIAS, node.getHandle());
}

void SymbolIndexer::visitQualified(QualifiedTypeNode &node) {
  this->visit(node.getRecvTypeNode());
  if (!node.isUntyped() && !node.getType().isUnresolved()) {
    NameInfo info(node.getNameTypeNode().getToken(), node.getNameTypeNode().getTokenText());
    this->builder().addMember(node.getRecvTypeNode().getType(), info, DeclSymbol::Kind::TYPE_ALIAS,
                              *node.getNameTypeNode().getHandle(), node.getToken());
  }
}

void SymbolIndexer::visitVarNode(VarNode &node) {
  if (!node.isUntyped() && !node.getType().isUnresolved() && !node.getType().isVoidType() &&
      !node.getType().isNothingType() && node.getHandle()) {
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
    auto &info = node.getField();
    this->builder().addMember(node.getRecvNode().getType(), info, DeclSymbol::Kind::VAR,
                              *node.getHandle(), node.getToken());
  }
}

void SymbolIndexer::visitApplyNode(ApplyNode &node) {
  const Handle *handle = nullptr;
  StringRef funcName;
  if (node.isMethodCall() && node.getHandle()) {
    auto &accessNode = cast<AccessNode>(node.getExprNode());
    this->visit(accessNode.getRecvNode());
    auto &nameInfo = accessNode.getField();
    this->builder().addMember(*node.getHandle(), nameInfo, node.getToken());
    handle = node.getHandle();
    funcName = nameInfo.getName();
  } else if (node.isFuncCall()) {
    this->visit(node.getExprNode());
    if (isa<VarNode>(node.getExprNode())) {
      const auto &varNode = cast<VarNode>(node.getExprNode());
      if (auto &hd = varNode.getHandle(); hd && hd->isFuncHandle()) {
        handle = hd.get();
        funcName = varNode.getVarName();
      }
    } else if (isa<AccessNode>(node.getExprNode())) {
      const auto &accessNode = cast<AccessNode>(node.getExprNode());
      if (auto &hd = accessNode.getHandle(); hd && hd->isFuncHandle()) {
        handle = hd.get();
        funcName = accessNode.getFieldName();
      }
    }
  }
  if (handle && node.getArgsNode().hasNamedArgs()) {
    this->visitNamedArgsNode(node.getArgsNode(), *handle, funcName);
  } else {
    this->visit(node.getArgsNode());
  }
}

void SymbolIndexer::visitNewNode(NewNode &node) {
  this->visit(node.getTargetTypeNode());
  if (node.getHandle() && node.getArgsNode().hasNamedArgs()) {
    this->visitNamedArgsNode(node.getArgsNode(), *node.getHandle(), OP_INIT);
  } else {
    this->visit(node.getArgsNode());
  }
}

void SymbolIndexer::visitNamedArgsNode(const ArgsNode &node, const Handle &handle,
                                       StringRef funcName) {
  for (auto &e : node.getNamedEntries()) {
    this->builder().addNamedArgSymbol(e.getNameInfo(), handle, funcName);
  }
  for (auto &e : node.getNodes()) {
    this->visit(*e);
  }
}

void SymbolIndexer::visitCmdNode(CmdNode &node) {
  const Symbol *symbol = nullptr;
  if (auto &cmdName = node.getNameNode().getValue(); !cmdName.empty()) {
    NameInfo info(node.getNameNode().getToken(), cmdName);
    symbol = this->builder().addCmd(info, node.getHandle());
  }
  if (auto nameInfo = getConstArg(node.getArgNodes()); symbol && nameInfo.hasValue()) {
    if (auto *decl = this->builder().findDecl(*symbol);
        decl && decl->is(DeclSymbol::Kind::MOD)) { // resolve sub-command
      auto ret = decl->getInfoAsModId();
      if (ret.second) {
        auto &type = *this->builder().getPool().getModTypeById(ret.first);
        auto handle =
            type.lookup(this->builder().getPool(), toCmdFullName(nameInfo.unwrap().getName()));
        this->builder().addSymbol(nameInfo.unwrap(), DeclSymbol::Kind::CMD, handle);
      }
    }
  }
  this->visitEach(node.getArgNodes());
}

void SymbolIndexer::visitRedirNode(RedirNode &node) {
  if (node.getHereStart()) {
    this->builder().addHereDocStartEnd(node.getHereStart(), node.getHereEnd());
  }
  this->visit(node.getTargetNode());
}

void SymbolIndexer::visitBlockNode(BlockNode &node) {
  auto block = this->builder().intoScope(ScopeKind::BLOCK, node.getToken());
  this->visitBlockWithCurrentScope(node);
}

void SymbolIndexer::visitTypeDefNode(TypeDefNode &node) {
  switch (node.getDefKind()) {
  case TypeDefNode::ALIAS:
    this->visit(node.getTargetTypeNode());
    if (this->builder().curScope().isConstructor()) {
      this->builder().addMemberDecl(*this->builder().curScope().getResolvedType(),
                                    node.getNameInfo(), node.getTargetTypeNode().getType(),
                                    DeclSymbol::Kind::TYPE_ALIAS, node.getToken());
    } else {
      this->builder().addDecl(node.getNameInfo(), node.getTargetTypeNode().getType(),
                              node.getToken(), DeclSymbol::Kind::TYPE_ALIAS);
    }
    break;
  case TypeDefNode::ERROR_DEF:
    if (this->isTopLevel()) {
      this->visit(node.getTargetTypeNode());
      this->builder().addDecl(node.getNameInfo(), node.getTargetTypeNode().getType(),
                              node.getToken(), DeclSymbol::Kind::ERROR_TYPE_DEF);
      if (auto &handle = node.getHandle()) {
        auto &type = this->builder().getPool().get(handle->getTypeId());
        this->builder().addTypeInheritance(type);
      }
    }
    break;
  }
}

void SymbolIndexer::visitLoopNode(LoopNode &node) {
  auto block = this->builder().intoScope(ScopeKind::BLOCK, node.getToken());
  this->visit(node.getInitNode());
  this->visit(node.getCondNode());
  this->visitBlockWithCurrentScope(node.getBlockNode());
  this->visit(node.getIterNode());
}

void SymbolIndexer::visitIfNode(IfNode &node) {
  switch (node.getIfLetKind()) {
  case IfNode::NOP:
    this->visit(node.getCondNode());
    break;
  case IfNode::ERROR:
    break;
  case IfNode::UNWRAP: {
    auto &exprNode = node.getIfLetUnwrap();
    this->visit(exprNode);
    break;
  }
  }
  this->visit(node.getThenNode());
  this->visit(node.getElseNode());
}

void SymbolIndexer::visitCatchNode(CatchNode &node) {
  auto block = this->builder().intoScope(ScopeKind::BLOCK, node.getToken());
  this->visit(node.getTypeNode());
  this->builder().addDecl(node.getNameInfo(), node.getTypeNode().getType(),
                          node.getNameInfo().getToken());
  this->visitBlockWithCurrentScope(node.getBlockNode());
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
  this->visitEach(node.getAttrNodes());
  this->visit(node.getExprNode());
  auto &type = node.getExprNode() ? node.getExprNode()->getType()
                                  : this->builder().getPool().get(TYPE::String);
  if (this->builder().curScope().isConstructor()) {
    if (auto *resolved = this->builder().curScope().getResolvedType()) {
      this->builder().addMemberDecl(*resolved, node.getNameInfo(), type,
                                    fromVarDeclKind(node.getKind()), node.getToken());
    }
  } else {
    this->builder().addDecl(node.getNameInfo(), type, node.getToken(),
                            fromVarDeclKind(node.getKind()));
  }
}

void SymbolIndexer::visitPrefixAssignNode(PrefixAssignNode &node) {
  if (node.getExprNode()) {
    auto block = this->builder().intoScope(ScopeKind::BLOCK, node.getToken());
    for (auto &e : node.getAssignNodes()) {
      this->visit(e->getRightNode());
      assert(isa<VarNode>(e->getLeftNode()));
      auto &leftNode = cast<VarNode>(e->getLeftNode());
      NameInfo info(leftNode.getToken(), leftNode.getVarName());
      this->builder().addDecl(info, leftNode.getType(), e->getToken(),
                              DeclSymbol::Kind::PREFIX_ENV);
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
    value += paramNode->getVarName();
    value += ": ";
    normalizeTypeName(paramNode->getExprNode()->getType(), value);
  }
  value += "): ";
  normalizeTypeName(node.getReturnTypeNode()->getType(), value);
  if (node.isMethod()) {
    value += " for ";
    normalizeTypeName(node.getRecvTypeNode()->getType(), value);
  }
  return value;
}

static std::string generateConstructorInfo(const TypePool &pool, const FunctionNode &node) {
  assert(node.isConstructor());

  std::string value;
  unsigned int size = node.getParamNodes().size();
  value += "(";
  for (unsigned int i = 0; i < size; i++) {
    auto &paramNode = node.getParamNodes()[i];
    if (i > 0) {
      value += ", ";
    }
    value += paramNode->getVarName();
    value += ": ";
    normalizeTypeName(paramNode->getExprNode()->getType(), value);
  }
  value += ") {\n";
  if (node.kind == FunctionNode::IMPLICIT_CONSTRUCTOR) {
    for (auto &e : node.getParamNodes()) {
      auto declKind = fromVarDeclKind(e->getKind());
      value += "    ";
      value += DeclSymbol::getVarDeclPrefix(declKind);
      value += " ";
      value += e->getVarName();
      value += ": ";
      normalizeTypeName(e->getExprNode()->getType(), value);
      value += "\n";
    }
  }
  for (auto &e : node.getBlockNode().getNodes()) {
    if (isa<VarDeclNode>(*e)) {
      auto &declNode = cast<VarDeclNode>(*e);
      auto declKind = fromVarDeclKind(declNode.getKind());
      value += "    ";
      value += DeclSymbol::getVarDeclPrefix(declKind);
      value += " ";
      value += declNode.getVarName();
      value += ": ";
      if (declNode.getExprNode()) {
        normalizeTypeName(declNode.getExprNode()->getType(), value);
      } else {
        normalizeTypeName(pool.get(TYPE::String), value);
      }
      value += "\n";
    } else if (isa<TypeDefNode>(*e)) {
      auto &defNode = cast<TypeDefNode>(*e);
      if (defNode.getDefKind() == TypeDefNode::ALIAS) {
        value += "    type ";
        value += defNode.getName();
        value += " = ";
        normalizeTypeName(defNode.getTargetTypeNode().getType(), value);
        value += "\n";
      }
    }
  }
  value += "}";
  if (isa<CLIRecordType>(node.getResolvedType())) {
    auto &type = cast<CLIRecordType>(*node.getResolvedType());
    if (auto &entries = type.getEntries(); !entries.empty()) {
      value += "---"; // dummy
      const auto parser = ArgParser::create("", entries, type.getDesc());
      if (auto ret = parser.formatUsage("", true); ret.hasValue()) {
        value += ret.unwrap();
      }
    }
  }
  return value;
}

static ScopeKind getScopeKind(const FunctionNode &node) {
  if (node.isConstructor()) {
    return ScopeKind::CONSTRUCTOR;
  }
  if (node.isMethod()) {
    return ScopeKind::METHOD;
  }
  return ScopeKind::FUNC;
}

void SymbolIndexer::visitFunctionImpl(FunctionNode &node, const FuncVisitOp op) {
  if (hasFlag(op, FuncVisitOp::VISIT_NAME)) {
    if (node.getType().isUnresolved() || (!this->builder().isGlobal() && !node.isAnonymousFunc())) {
      return;
    }

    this->visitEach(node.getAttrNodes());

    if (node.getHandle()) {
      if (node.isConstructor()) {
        auto value = generateConstructorInfo(this->builder().getPool(), node);
        this->builder().addDecl(node.getNameInfo(), DeclSymbol::Kind::CONSTRUCTOR, value.c_str(),
                                node.getToken());
        if (auto *type = node.getResolvedType()) {
          this->builder().addTypeInheritance(*type);
        }
      } else if (node.isMethod()) {
        auto value = generateFuncInfo(node);
        this->builder().addMemberDecl(node.getRecvTypeNode()->getType(), node.getNameInfo(),
                                      DeclSymbol::Kind::METHOD, value.c_str(), node.getToken());
      } else {
        auto value = generateFuncInfo(node);
        this->builder().addDecl(node.getNameInfo(), DeclSymbol::Kind::FUNC, value.c_str(),
                                node.getToken());
      }
    }
    for (auto &paramNode : node.getParamNodes()) {
      this->visit(paramNode->getExprNode());
    }
    this->visit(node.getReturnTypeNode());
    this->visit(node.getRecvTypeNode());
  }

  if (hasFlag(op, FuncVisitOp::VISIT_BODY)) {
    auto interval = ScopeInterval::create(node.getBlockNode().getToken());
    if (!node.getParamNodes().empty()) {
      interval.beginPos = node.getParamNodes()[0]->getPos();
    }
    auto func = this->builder().intoScope(getScopeKind(node), interval,
                                          node.isMethod() ? &node.getRecvTypeNode()->getType()
                                                          : node.getResolvedType());
    if (node.kind == FunctionNode::IMPLICIT_CONSTRUCTOR) {
      for (auto &e : node.getParamNodes()) {
        if (auto *resolved = this->builder().curScope().getResolvedType()) {
          this->builder().addMemberDecl(*resolved, e->getNameInfo(), e->getExprNode()->getType(),
                                        fromVarDeclKind(e->getKind()), e->getToken());
        }
      }
    } else {
      for (auto &paramNode : node.getParamNodes()) {
        if (paramNode->getExprNode()->isUntyped()) {
          continue;
        }
        if (node.getHandle()) {
          this->builder().addParamDecl(paramNode->getNameInfo(),
                                       paramNode->getExprNode()->getType(), node.getToken(),
                                       node.getFuncName(), *node.getHandle());
        } else {
          this->builder().addDecl(paramNode->getNameInfo(), paramNode->getExprNode()->getType(),
                                  node.getToken());
        }
      }
    }
    this->visitBlockWithCurrentScope(node.getBlockNode());
  }
}

void SymbolIndexer::visitFunctionNode(FunctionNode &node) {
  this->visitFunctionImpl(node, FuncVisitOp::VISIT_NAME | FuncVisitOp::VISIT_BODY);
}

void SymbolIndexer::visitUserDefinedCmdImpl(UserDefinedCmdNode &node, const FuncVisitOp op) {
  if (hasFlag(op, FuncVisitOp::VISIT_NAME)) {
    if (!this->isTopLevel() && !node.isAnonymousCmd()) {
      return;
    }
    if (node.getHandle()) {
      std::string hover = this->builder().getPool().get(TYPE::Bool).getNameRef().toString();
      if (node.getReturnTypeNode() && node.getReturnTypeNode()->getType().isNothingType()) {
        hover = node.getReturnTypeNode()->getType().getName();
      }
      if (node.getParamNode()) {
        auto &exprType = node.getParamNode()->getExprNode()->getType();
        if (isa<CLIRecordType>(exprType)) {
          StringRef cliName;
          if (auto attr = cast<CLIRecordType>(exprType).getAttr();
              hasFlag(attr, CLIRecordType::Attr::TOPLEVEL)) {
            cliName = "<$ARG0>";
          } else {
            cliName = node.getCmdName();
          }
          auto &cliType = cast<CLIRecordType>(exprType);
          if (auto &entries = cliType.getEntries(); !entries.empty()) {
            hover += "---"; // dummy
            const auto parser = ArgParser::create(cliName, entries, cliType.getDesc());
            if (auto ret = parser.formatUsage("", true); ret.hasValue()) {
              hover += ret.unwrap();
            }
          }
        }
      }
      this->builder().addDecl(node.getNameInfo(), DeclSymbol::Kind::CMD, hover.c_str(),
                              node.getToken());
    }
  }

  if (hasFlag(op, FuncVisitOp::VISIT_BODY)) {
    auto interval = ScopeInterval::create(node.getBlockNode().getToken());
    if (node.getParamNode()) {
      interval.beginPos = node.getParamNode()->getPos();
    }
    auto udc = this->builder().intoScope(ScopeKind::FUNC, interval);
    this->visit(node.getParamNode());
    this->visitBlockWithCurrentScope(node.getBlockNode());
  }
}

void SymbolIndexer::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
  this->visitUserDefinedCmdImpl(node, FuncVisitOp::VISIT_NAME | FuncVisitOp::VISIT_BODY);
}

void SymbolIndexer::visitFuncListNode(FuncListNode &node) {
  // register decl
  for (auto &e : node.getNodes()) {
    if (isa<FunctionNode>(*e)) {
      this->visitFunctionImpl(cast<FunctionNode>(*e), FuncVisitOp::VISIT_NAME);
    } else if (isa<UserDefinedCmdNode>(*e)) {
      this->visitUserDefinedCmdImpl(cast<UserDefinedCmdNode>(*e), FuncVisitOp::VISIT_NAME);
    }
  }

  // register body
  for (auto &e : node.getNodes()) {
    if (isa<FunctionNode>(*e)) {
      this->visitFunctionImpl(cast<FunctionNode>(*e), FuncVisitOp::VISIT_BODY);
    } else if (isa<UserDefinedCmdNode>(*e)) {
      this->visitUserDefinedCmdImpl(cast<UserDefinedCmdNode>(*e), FuncVisitOp::VISIT_BODY);
    }
  }
}

void SymbolIndexer::visitSourceNode(SourceNode &node) {
  if (!this->isTopLevel()) {
    return;
  }
  if (node.getSrcIndex() == 0) {
    this->visitEach(node.getPathNode().getSegmentNodes());
  }
  if (node.getNameInfo()) {
    assert(node.getSrcIndex() == 0);
    this->builder().addDecl(*node.getNameInfo(), DeclSymbol::Kind::MOD,
                            std::to_string(toUnderlying(node.getModType().getModId())).c_str(),
                            node.getToken());
  }
  this->builder().addLink(node.getPathToken(), node.getModType().getModId(),
                          node.getImportedModKind(), node.getPathName());
}

bool SymbolIndexer::enterModule(const SourcePtr &src, const std::shared_ptr<TypePool> &p) {
  this->builders.emplace_back(src, p, this->indexes, this->logger);
  if (!this->indexes.find(BUILTIN_MOD_ID)) {
    auto dummy = std::make_shared<Source>();
    assert(dummy->getSrcId() == BUILTIN_MOD_ID);
    this->builders.emplace_back(dummy, p, this->indexes, this->logger);
    this->addBuiltinSymbols();
    this->exitModule(nullptr);
  }
  return true;
}

bool SymbolIndexer::exitModule(const std::unique_ptr<Node> &node) {
  assert(!this->builders.empty());
  auto index = std::make_shared<SymbolIndex>(std::move(this->builder()).build());
  this->builders.pop_back();
  this->indexes.add(std::move(index));
  this->visit(node);
  return true;
}

static DeclSymbol::Kind resolveDeclKind(const std::pair<std::string, HandlePtr> &entry) {
  if (isTypeAliasFullName(entry.first)) {
    assert(entry.second->is(HandleKind::TYPE_ALIAS));
    return DeclSymbol::Kind::TYPE_ALIAS;
  }
  if (isCmdFullName(entry.first)) {
    return DeclSymbol::Kind::CMD;
  }
  switch (entry.second->getKind()) {
  case HandleKind::ENV:
    return DeclSymbol::Kind::IMPORT_ENV;
  case HandleKind::MOD_CONST:
    return DeclSymbol::Kind::MOD_CONST;
  case HandleKind::SYS_CONST:
  case HandleKind::SMALL_CONST:
    return DeclSymbol::Kind::CONST;
  default:
    break;
  }

  if (entry.second->has(HandleAttr::READ_ONLY)) {
    return DeclSymbol::Kind::LET;
  }
  return DeclSymbol::Kind::VAR;
}

void SymbolIndexer::addBuiltinSymbols() {
  IndexBuilder::DummyTokenGenerator tokenGen;

  // add builtin type (except for generic type)
  const Type *mapType = nullptr;
  for (auto &type : this->builder().getPool().getTypeTable()) {
    if (type->isMapType() && !mapType) {
      mapType = type;
    }
    if (type->typeKind() != TypeKind::Builtin && type->typeKind() != TypeKind::Error) {
      continue;
    }
    if (type->isUnresolved()) {
      continue;
    }
    NameInfo nameInfo(tokenGen.next(), type->getNameRef().toString());
    if (type->is(TYPE::Error) || isa<ErrorType>(type)) {
      if (isa<ErrorType>(type)) {
        auto &baseType = *type->getSuperType();
        this->builder().addDecl(nameInfo, baseType, nameInfo.getToken(),
                                DeclSymbol::Kind::ERROR_TYPE_DEF);
      } else {
        this->builder().addDecl(nameInfo, DeclSymbol::Kind::BUILTIN_TYPE, "", nameInfo.getToken());
      }
      this->builder().addTypeInheritance(*type);
    } else {
      assert(isa<BuiltinType>(type));
      this->builder().addDecl(nameInfo, DeclSymbol::Kind::BUILTIN_TYPE, "", nameInfo.getToken());
    }
  }
  // add type template
  for (auto &e : this->builder().getPool().getTemplateMap()) {
    NameInfo nameInfo(tokenGen.next(), e.first.toString());
    this->builder().addDecl(nameInfo, DeclSymbol::Kind::BUILTIN_TYPE, "", nameInfo.getToken());
  }
  // builtin method
  for (auto &e : this->builder().getPool().getMethodMap()) {
    const StringRef name = e.first.ref;
    if (isMagicMethodName(name)) {
      continue;
    }
    auto &recvType = this->builder().getPool().get(e.first.id);
    if (recvType.typeKind() != TypeKind::Builtin) {
      continue;
    }
    bool s = this->builder().addBuiltinMethod(
        recvType, e.second ? e.second.handle()->getIndex() : e.second.index(), tokenGen, name);
    (void)s;
    assert(s);
  }
  // array type method
  {
    auto &type = this->builder().getPool().get(TYPE::StringArray);
    const auto info = cast<ArrayType>(type).getNativeTypeInfo();
    for (unsigned int i = 0; i < info.methodSize; i++) {
      this->builder().addBuiltinMethod(type, info.getActualMethodIndex(i), tokenGen,
                                       info.getMethodInfo(i).funcName);
    }
  }
  // map type method
  {
    assert(mapType);
    const auto info = cast<MapType>(*mapType).getNativeTypeInfo();
    for (unsigned int i = 0; i < info.methodSize; i++) {
      this->builder().addBuiltinMethod(*mapType, info.getActualMethodIndex(i), tokenGen,
                                       info.getMethodInfo(i).funcName);
    }
  }

  // add builtin symbols (also defined in embed)
  auto &modType = this->builder().getPool().getBuiltinModType();
  for (auto &e : modType.getHandleMap()) {
    const auto kind = resolveDeclKind(e);
    NameInfo nameInfo(tokenGen.next(), DeclSymbol::demangle(kind, e.first));
    auto &type = this->builder().getPool().get(e.second->getTypeId());
    if (kind == DeclSymbol::Kind::CMD || kind == DeclSymbol::Kind::MOD_CONST) {
      this->builder().addDecl(nameInfo, kind, "", nameInfo.getToken());
    } else if (kind == DeclSymbol::Kind::CONST) {
      if (e.second->is(HandleKind::SYS_CONST)) {
        auto *ptr = this->sysConfig.lookup(nameInfo.getName());
        assert(ptr);
        assert(type.is(TYPE::String));
        std::string value = "'";
        value += *ptr;
        value += "'";
        this->builder().addDecl(nameInfo, DeclSymbol::Kind::CONST, value.c_str(),
                                nameInfo.getToken());
      } else if (e.second->is(HandleKind::SMALL_CONST)) {
        ConstEntry entry(e.second->getIndex());
        this->builder().addDecl(nameInfo, kind, toString(entry).c_str(), nameInfo.getToken());
      }
    } else {
      this->builder().addDecl(nameInfo, type, nameInfo.getToken(), kind);
    }
  }

  // add builtin command
  const auto range = getBuiltinCmdDescRange();
  for (auto &e : range) {
    NameInfo nameInfo(tokenGen.next(), e.name);
    this->builder().addDecl(nameInfo, DeclSymbol::Kind::BUILTIN_CMD, "", nameInfo.getToken());
  }
}

} // namespace arsh::lsp