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

#include "index.h"
#include "lsp.h"
#include "source.h"

namespace ydsh::lsp {

class IndexMap {
private:
  StrRefMap<ModuleIndexPtr> map;

public:
  ModuleIndexPtr find(const Source &src) const {
    auto iter = this->map.find(src.getPath());
    return iter != this->map.end() ? iter->second : nullptr;
  }

  void add(const Source &src, ModuleIndexPtr index) {
    assert(!index || src.getSrcId() == index->getModId());
    this->map[src.getPath()] = std::move(index);
  }

  size_t size() const { return this->map.size(); }

  void revert(std::unordered_set<unsigned short> &&revertingModIdSet);

  /**
   * revert sepcified index if unused (not imported from other indexes)
   * @param id
   * @return
   * if unused, return true
   */
  bool revertIfUnused(unsigned short id);
};

class ASTContext {
private:
  std::unique_ptr<TypePool> pool;
  IntrusivePtr<NameScope> scope;
  std::vector<std::unique_ptr<Node>> nodes;
  int version;
  unsigned int gvarCount{0};
  TypeDiscardPoint typeDiscardPoint;

public:
  NON_COPYABLE(ASTContext);

  explicit ASTContext(const Source &src);

  const IntrusivePtr<NameScope> &getScope() const { return this->scope; }

  TypePool &getPool() { return *this->pool; }

  unsigned int getModId() const { return this->scope->modId; }

  int getVersion() const { return this->version; }

  unsigned int getTypeIdOffset() const { return this->typeDiscardPoint.typeIdOffset; }

  void addNode(std::unique_ptr<Node> &&node) { this->nodes.push_back(std::move(node)); }

  ModuleIndexPtr buildIndex(const SourceManager &srcMan, const IndexMap &indexMap) &&;
};

using ASTContextPtr = std::unique_ptr<ASTContext>;

class ASTContextProvider : public FrontEnd::ModuleProvider, public ModuleLoaderBase {
private:
  SourceManager &srcMan;
  IndexMap &indexMap;
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

  const ASTContextPtr &addNew(const Source &src);

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

struct AnalyzerAction {
  ObserverPtr<DiagnosticEmitter> emitter;
  ObserverPtr<NodeDumper> dumper;
};

ModuleIndexPtr buildIndex(SourceManager &srcMan, IndexMap &indexMap, AnalyzerAction &action,
                          const Source &src);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_ANALYZER_H
