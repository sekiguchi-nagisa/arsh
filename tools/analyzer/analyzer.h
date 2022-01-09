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

#include <atomic>

#include <frontend.h>
#include <node.h>
#include <scope.h>
#include <type_pool.h>

#include "archive.h"
#include "lsp.h"
#include "source.h"

namespace ydsh::lsp {

using DiagnosticCallback = std::function<void(PublishDiagnosticsParams &&)>;

class DiagnosticEmitter : public FrontEnd::ErrorListener {
private:
  std::shared_ptr<SourceManager> srcMan;
  DiagnosticCallback callback;
  bool supportVersion;

  struct Context {
    SourcePtr src;
    int version;
    std::vector<Diagnostic> diagnostics;

    Context(SourcePtr src, int version) : src(std::move(src)), version(version) {}
  };

  std::vector<Context> contexts;

public:
  DiagnosticEmitter(std::shared_ptr<SourceManager> &&srcMan, DiagnosticCallback &&callback,
                    bool supportVersion)
      : srcMan(std::move(srcMan)), callback(std::move(callback)), supportVersion(supportVersion) {}

  ~DiagnosticEmitter() override = default;

  bool handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                        const ParseError &parseError) override;
  bool handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                       const TypeCheckError &checkError, bool firstAppear) override;

  bool enterModule(unsigned short modId, int version);

  bool exitModule();
};

struct NodeConsumer {
  virtual ~NodeConsumer() = default;

  virtual bool enterModule(unsigned short modID, int version,
                           const std::shared_ptr<TypePool> &pool) = 0;
  virtual bool exitModule(std::unique_ptr<Node> &&node) = 0;
  virtual bool consume(std::unique_ptr<Node> &&node) = 0;
};

struct AnalyzerAction {
  ObserverPtr<DiagnosticEmitter> emitter;
  ObserverPtr<NodeDumper> dumper;
  ObserverPtr<NodeConsumer> consumer;
};

class CancelPoint {
private:
  std::atomic<bool> value{false};

public:
  void cancel() { this->value.store(true); }

  bool isCanceled() const { return this->value.load(); }
};

class AnalyzerContext {
private:
  std::shared_ptr<TypePool> pool;
  NameScopePtr scope;
  int version;
  unsigned int gvarCount{0};
  TypeDiscardPoint typeDiscardPoint;

public:
  NON_COPYABLE(AnalyzerContext);

  explicit AnalyzerContext(const Source &src);

  const NameScopePtr &getScope() const { return this->scope; }

  TypePool &getPool() { return *this->pool; }

  const auto &getPoolPtr() const { return this->pool; }

  unsigned int getModId() const { return this->scope->modId; }

  int getVersion() const { return this->version; }

  unsigned int getTypeIdOffset() const { return this->typeDiscardPoint.typeIdOffset; }

  ModuleArchivePtr buildArchive(ModuleArchives &archives) &&;
};

using AnalyzerContextPtr = std::unique_ptr<AnalyzerContext>;

class Analyzer : protected FrontEnd::ModuleProvider, protected ModuleLoaderBase {
private:
  SourceManager &srcMan;
  ModuleArchives &archives;
  std::shared_ptr<CancelPoint> cancelPoint;
  std::vector<AnalyzerContextPtr> ctxs;

public:
  Analyzer(SourceManager &src, ModuleArchives &archives,
           std::shared_ptr<CancelPoint> cancelPoint = nullptr)
      : srcMan(src), archives(archives), cancelPoint(std::move(cancelPoint)) {}

  ~Analyzer() override = default;

protected:
  std::unique_ptr<FrontEnd::Context>
  newContext(Lexer &&lexer, FrontEndOption option,
             ObserverPtr<CodeCompletionHandler> ccHandler) override;

  const ModType &
  newModTypeFromCurContext(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx) override;

  Ret load(const char *scriptDir, const char *modPath, FrontEndOption option) override;

  ModResult addNewModEntry(CStrPtr &&ptr) override;

private:
  const AnalyzerContextPtr &addNew(const Source &src);

  const AnalyzerContextPtr &current() const { return this->ctxs.back(); }

  void unwind() { // FIXME: future may be removed
    while (this->ctxs.size() > 1) {
      this->ctxs.pop_back();
    }
  }

public:
  ModuleArchivePtr analyze(const Source &src, AnalyzerAction &action);

  std::vector<CompletionItem> complete(const Source &src, CmdCompKind ckind, bool cmdArgComp);
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_ANALYZER_H
