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

#ifndef ARSH_TOOLS_ANALYZER_INDEXER_H
#define ARSH_TOOLS_ANALYZER_INDEXER_H

#include <sysconfig.h>

#include "index.h"
#include "pass.h"

namespace arsh::lsp {

enum class ScopeKind : unsigned char {
  GLOBAL,
  FUNC,
  CONSTRUCTOR,
  METHOD,
  BLOCK,
};

class IndexBuilder {
private:
  const SourcePtr src;
  std::vector<DeclSymbol> decls;
  std::vector<Symbol> symbols;
  std::vector<ForeignDecl> foreign;
  std::vector<std::pair<SymbolRef, IndexLink>> links;
  std::vector<ScopeInterval> scopeIntervals;
  PackedParamTypesMap packedParamTypesMap;
  std::unordered_set<std::string> externalCmdSet;
  TypeInheritanceMap inheritanceMap;

  class ScopeEntry : public RefCount<ScopeEntry> {
  private:
    /**
     * mangled name => DeclSymbol reference
     */
    std::unordered_map<std::string, SymbolRef> map;

    const Type *resolvedType{nullptr}; // for constructor

  public:
    const IntrusivePtr<ScopeEntry> parent;

    const ScopeKind kind;

    const unsigned int scopeId;

    ScopeEntry(const IntrusivePtr<ScopeEntry> &parent, ScopeKind kind, unsigned int scopeId,
               const Type *type)
        : resolvedType(type), parent(parent), kind(kind), scopeId(scopeId) {}

    explicit ScopeEntry(unsigned int scopeId)
        : ScopeEntry(nullptr, ScopeKind::GLOBAL, scopeId, nullptr) {}

    bool isGlobal() const { return !this->parent; }

    bool isConstructor() const { return this->kind == ScopeKind::CONSTRUCTOR; }

    const ScopeEntry *findMethodScope() const {
      for (auto *cur = this; cur != nullptr; cur = cur->parent.get()) {
        if (cur->kind == ScopeKind::METHOD) {
          return cur;
        }
      }
      return nullptr;
    }

    const Type *getResolvedType() const { return this->resolvedType; }

    /**
     * low-level api, normally unused
     * @param name
     * must be proper mangled name
     * @param decl
     * @return
     * if successfully insert, return true
     */
    bool addDeclWithSpecifiedName(std::string &&name, const DeclSymbol &decl) {
      return this->map.emplace(std::move(name), decl.toRef()).second;
    }

    bool addDecl(const DeclSymbol &decl);

    const SymbolRef *find(const std::string &name) const {
      for (auto *cur = this; cur != nullptr; cur = cur->parent.get()) {
        auto iter = cur->map.find(name);
        if (iter != cur->map.end()) {
          return &iter->second;
        }
      }
      return nullptr;
    }

    auto take() && { return std::move(this->map); }
  };

  IntrusivePtr<ScopeEntry> scope;

  std::shared_ptr<const TypePool> pool; // do not mutate it

  const SymbolIndexes &indexes;

  const ObserverPtr<LoggerBase> logger;

public:
  IndexBuilder(SourcePtr src, std::shared_ptr<TypePool> pool, const SymbolIndexes &indexes,
               ObserverPtr<LoggerBase> logger)
      : src(std::move(src)), scope(IntrusivePtr<ScopeEntry>::create(0)), pool(std::move(pool)),
        indexes(indexes), logger(logger) {
    this->scopeIntervals.push_back({0, static_cast<unsigned int>(this->src->getContent().size())});
  }

  SymbolIndex build() && {
    return {this->src->getSrcId(),
            this->src->getVersion(),
            std::move(this->decls),
            std::move(this->symbols),
            std::move(this->foreign),
            std::move(*this->scope).take(),
            std::move(this->links),
            std::move(this->scopeIntervals),
            std::move(this->packedParamTypesMap),
            std::move(this->externalCmdSet),
            std::move(this->inheritanceMap)};
  }

  const TypePool &getPool() const { return *this->pool; }

  const ScopeEntry &curScope() const { return *this->scope; }

  [[nodiscard]] auto intoScope(ScopeKind kind, ScopeInterval interval, const Type *type = nullptr) {
    this->scope =
        IntrusivePtr<ScopeEntry>::create(this->scope, kind, this->scopeIntervals.size(), type);
    this->scopeIntervals.push_back(interval);
    return finally([&] { this->scope = this->scope->parent; });
  }

  [[nodiscard]] auto intoScope(ScopeKind kind, Token token) {
    return this->intoScope(kind, ScopeInterval::create(token));
  }

  bool isGlobal() const { return this->scope->isGlobal(); }

  bool isReceiver(const std::string &varName, const Handle &handle) const {
    return this->curScope().findMethodScope() && varName == VAR_THIS && handle.getIndex() == 0;
  }

  const DeclSymbol *addDecl(const NameInfo &info, const Type &type, Token token,
                            DeclSymbol::Kind kind = DeclSymbol::Kind::VAR);

  const DeclSymbol *addDecl(const NameInfo &info, DeclSymbol::Kind kind, const char *hover,
                            Token token) {
    return this->addDeclImpl(nullptr, info, kind, hover, token, DeclInsertOp::NORMAL);
  }

  const Symbol *addSymbol(const NameInfo &info, DeclSymbol::Kind kind, const Handle *handle) {
    return this->addSymbolImpl(nullptr, info, kind, handle);
  }

  const Symbol *addSymbol(const NameInfo &info, DeclSymbol::Kind kind, const HandlePtr &hd) {
    return this->addSymbol(info, kind, hd.get());
  }

  const Symbol *addCmd(const NameInfo &info, const HandlePtr &hd);

  void addTypeInheritance(const Type &type);

  bool addThis(const NameInfo &info, const HandlePtr &hd);

  bool addMember(const Type &recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                 const Handle &handle, Token token);

  bool addMember(const MethodHandle &handle, const NameInfo &nameInfo, Token token) {
    return this->addMember(this->getPool().get(handle.getRecvTypeId()), nameInfo,
                           DeclSymbol::Kind::METHOD, handle, token);
  }

  const DeclSymbol *addMemberDecl(const Type &recv, const NameInfo &nameInfo, const Type &type,
                                  DeclSymbol::Kind kind, Token token);

  const DeclSymbol *addMemberDecl(const Type &recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                                  const char *info, Token token) {
    if (recv.isUnresolved()) {
      return nullptr;
    }
    return this->addDeclImpl(&recv, nameInfo, kind, info, token, DeclInsertOp::MEMBER);
  }

  std::string mangleParamName(StringRef funcName, const Handle &handle, StringRef paramName) const;

  /**
   *
   * @param info
   * @param type
   * @param token
   * @param funcName
   * @param handle
   * @return
   */
  const DeclSymbol *addParamDecl(const NameInfo &info, const Type &type, Token token,
                                 StringRef funcName, const Handle &handle);

  const DeclSymbol *addParamDeclImpl(const NameInfo &info, const std::string &paramType,
                                     Token token) {
    return this->addDeclImpl(nullptr, info, DeclSymbol::Kind::PARAM, paramType.c_str(), token,
                             DeclInsertOp::PARAM);
  }

  const Symbol *addNamedArgSymbol(const NameInfo &nameInfo, const Handle &handle,
                                  StringRef funcName);

  class DummyTokenGenerator {
  private:
    unsigned int offset{0};

  public:
    Token next() {
      unsigned int pos = this->offset;
      this->offset += 5;
      return {
          .pos = pos,
          .size = 1,
      };
    }
  };

  bool addBuiltinMethod(const Type &recvType, unsigned int methodIndex,
                        DummyTokenGenerator &tokenGen, StringRef name);

  const DeclSymbol *findDecl(const Symbol &symbol) const;

  void addLink(Token token, ModId targetModId, ImportedModKind modKind, const std::string &link) {
    auto ref = SymbolRef::create(token, targetModId);
    if (ref.hasValue()) {
      IndexLink::ImportAttr importAttr{};
      if (hasFlag(modKind, ImportedModKind::GLOBAL)) {
        setFlag(importAttr, IndexLink::ImportAttr::GLOBAL);
      }
      if (hasFlag(modKind, ImportedModKind::INLINED)) {
        setFlag(importAttr, IndexLink::ImportAttr::INLINED);
      }

      this->links.emplace_back(ref.unwrap(), IndexLink(targetModId, importAttr, std::string(link)));
    }
  }

  void addHereDocStartEnd(const NameInfo &start, Token end);

  /**
   *
   * @param alias
   * must be porper mangled name
   * @param decl
   * @return
   */
  bool addAliasToCurScope(std::string &&alias, const DeclSymbol &decl) {
    return this->scope->addDeclWithSpecifiedName(std::move(alias), decl);
  }

private:
  ModId getModId() const { return this->src->getSrcId(); }

  const SymbolRef *lookup(const std::string &mangledName, DeclSymbol::Kind kind,
                          const Handle *handle) const;

  enum class DeclInsertOp : unsigned char {
    NORMAL,
    BUILTIN,
    MEMBER,
    PARAM,
    NONE, // no scope check
  };

  const DeclSymbol *addDeclImpl(const Type *recv, const NameInfo &info, DeclSymbol::Kind kind,
                                const char *hover, Token body, DeclInsertOp op);

  const Symbol *addSymbolImpl(const Type *recv, const NameInfo &nameInfo, DeclSymbol::Kind kind,
                              const Handle *handle);

  /**
   * create new DeclSymbol and insert to decl list
   * @param k
   * @param attr
   * @param name
   * @param info
   * @param body
   * @param op
   * @return
   */
  DeclSymbol *insertNewDecl(DeclSymbol::Kind k, DeclSymbol::Attr attr, DeclSymbol::Name &&name,
                            const char *info, Token body, DeclInsertOp op);

  /**
   * create new Symbol from DeclVBase and insert to symbol list
   * @param token
   * @param decl
   * @return
   */
  const Symbol *insertNewSymbol(Token token, const DeclBase *decl);

  /**
   * for generic method
   * @param token
   * @param type
   */
  void addParamTypeInfo(Token token, const Type &type);
};

class SymbolIndexer : public NodePass {
private:
  const SysConfig &sysConfig;
  SymbolIndexes &indexes;
  std::vector<IndexBuilder> builders;
  ObserverPtr<LoggerBase> logger;

public:
  SymbolIndexer(const SysConfig &config, SymbolIndexes &indexes)
      : sysConfig(config), indexes(indexes) {}

  ~SymbolIndexer() override = default;

  void setLogger(ObserverPtr<LoggerBase> o) { this->logger = o; }

  bool enterModule(const SourcePtr &src, const std::shared_ptr<TypePool> &pool) override;
  bool exitModule(const std::unique_ptr<Node> &node) override;

protected:
  void visitBase(BaseTypeNode &node) override;
  void visitQualified(QualifiedTypeNode &node) override;

  void visitVarNode(VarNode &node) override;
  void visitAccessNode(AccessNode &node) override;
  void visitApplyNode(ApplyNode &node) override;
  void visitNewNode(NewNode &node) override;
  void visitCmdNode(CmdNode &node) override;
  void visitRedirNode(RedirNode &node) override;
  void visitBlockNode(BlockNode &node) override;
  void visitTypeDefNode(TypeDefNode &node) override;
  void visitLoopNode(LoopNode &node) override;
  void visitIfNode(IfNode &node) override;
  void visitCatchNode(CatchNode &node) override;
  void visitVarDeclNode(VarDeclNode &node) override;
  void visitPrefixAssignNode(PrefixAssignNode &node) override;
  void visitFunctionNode(FunctionNode &node) override;
  void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
  void visitFuncListNode(FuncListNode &node) override;
  void visitSourceNode(SourceNode &node) override;

private:
  void visitBlockWithCurrentScope(const BlockNode &blockNode) {
    this->visitEach(blockNode.getNodes());
  }

  enum class FuncVisitOp : unsigned char {
    VISIT_NAME = 1u << 0u,
    VISIT_BODY = 1u << 1u,
  };

  void visitFunctionImpl(FunctionNode &node, FuncVisitOp op);

  void visitUserDefinedCmdImpl(UserDefinedCmdNode &node, FuncVisitOp op);

  void visitNamedArgsNode(const ArgsNode &node, const Handle &handle, StringRef funcName);

  IndexBuilder &builder() { return this->builders.back(); }

  void addBuiltinSymbols();
};

} // namespace arsh::lsp

template <>
struct arsh::allow_enum_bitop<arsh::lsp::SymbolIndexer::FuncVisitOp> : std::true_type {};

#endif // ARSH_TOOLS_ANALYZER_INDEXER_H
