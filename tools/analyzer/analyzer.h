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

#ifndef YDSH_TOOLS_ANALYZER_ANALYZER_H
#define YDSH_TOOLS_ANALYZER_ANALYZER_H

#include <frontend.h>
#include <node.h>
#include <scope.h>
#include <type_pool.h>

#include "../tools/uri/uri.h"
#include "index.h"
#include "lsp.h"
#include "source.h"

namespace ydsh::lsp {

class ASTContext : public RefCount<ASTContext> {
private:
  std::string fullPath;
  std::string content;
  int version;
  IntrusivePtr<NameScope> scope;
  TypePool pool;
  std::vector<std::unique_ptr<Node>> nodes;
  unsigned int gvarCount{0};
  unsigned int oldGvarCount;
  TypeDiscardPoint typeDiscardPoint;
  ScopeDiscardPoint scopeDiscardPoint;

public:
  NON_COPYABLE(ASTContext);

  ASTContext(unsigned int modID, const uri::URI &uri, std::string &&content, int version = 0);

  const std::string &getFullPath() const { return this->fullPath; }

  const std::string &getContent() const { return this->content; }

  const IntrusivePtr<NameScope> &getScope() const { return this->scope; }

  const TypePool &getPool() const { return this->pool; }

  TypePool &getPool() { return this->pool; }

  void updateContent(std::string &&c, int v = 0);

  unsigned int getModId() const { return this->scope->modId; }

  void addNode(std::unique_ptr<Node> &&node) { this->nodes.push_back(std::move(node)); }

  const std::vector<std::unique_ptr<Node>> &getNodes() const { return this->nodes; }
};

using ASTContextPtr = IntrusivePtr<ASTContext>;

using IndexMap = std::unordered_map<std::string, ModuleIndexPtr>;

class ASTContextProvider : public FrontEnd::ModuleProvider, public ModuleLoaderBase {
private:
  SourceManager &srcMan;
  IndexMap &indexMap;
  //  std::vector<ModuleIndexPtr> fetchedIndexes;
  StrRefMap<ASTContextPtr> ctxMap; // fullpath to ASTContext mapping
  std::vector<ASTContextPtr> ctxs;

public:
  ASTContextProvider(SourceManager &src, IndexMap &indexMap) : srcMan(src), indexMap(indexMap) {}

  ~ASTContextProvider() override = default;

  std::unique_ptr<FrontEnd::Context>
  newContext(Lexer &&lexer, FrontEndOption option,
             ObserverPtr<CodeCompletionHandler> ccHandler) override;

  const ModType &
  newModTypeFromCurContext(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx) override;

  Ret load(const char *scriptDir, const char *modPath, FrontEndOption option) override;

  ASTContextPtr find(StringRef ref) const;

  ASTContextPtr find(const uri::URI &uri) const { return this->find(uri.getPath()); }

  ASTContextPtr addNew(const uri::URI &uri, const Source &src);

  const SourceManager &getSourceManager() const { return this->srcMan; }

  const auto &getIndexMap() const { return this->indexMap; }

  const ASTContextPtr &current() const { return this->ctxs.back(); }

private:
  ModResult addNewModEntry(CStrPtr &&ptr) override;
};

class DiagnosticEmitter : public FrontEnd::ErrorListener {
public:
  ~DiagnosticEmitter() override = default;

  bool handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                        const ParseError &parseError) override;
  bool handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                       const TypeCheckError &checkError) override;
};

void buildIndex(ASTContextProvider &provider, DiagnosticEmitter &emitter, ASTContextPtr ctx);

void revertIndexMap(IndexMap &indexMap, std::unordered_set<unsigned short> &&revertingModIdSet);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_ANALYZER_H
