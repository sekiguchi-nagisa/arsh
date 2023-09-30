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
#include <scope.h>
#include <type_pool.h>

#include "archive.h"
#include "lsp.h"
#include "pass.h"
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
    std::vector<Diagnostic> diagnostics;

    explicit Context(SourcePtr src) : src(std::move(src)) {}
  };

  std::vector<Context> contexts;

public:
  DiagnosticEmitter(std::shared_ptr<SourceManager> &&srcMan, DiagnosticCallback &&callback,
                    bool supportVersion)
      : srcMan(std::move(srcMan)), callback(std::move(callback)), supportVersion(supportVersion) {}

  ~DiagnosticEmitter() override;

  bool handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                        const ParseError &parseError) override;
  bool handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                       const TypeCheckError &checkError, bool firstAppear) override;

  bool handleTypeError(ModId modId, const TypeCheckError &checkError);

  bool enterModule(const SourcePtr &src) {
    this->contexts.emplace_back(src);
    return true;
  }

  bool exitModule();

private:
  Context *findContext(ModId srcId) {
    for (auto iter = this->contexts.rbegin(); iter != this->contexts.rend(); ++iter) {
      if (iter->src->getSrcId() == srcId) {
        return &*iter;
      }
    }
    return nullptr;
  }
};

struct AnalyzerAction {
  ObserverPtr<DiagnosticEmitter> emitter;
  ObserverPtr<NodeDumper> dumper;
  ObserverPtr<NodePass> pass;
};

class CancelPoint : public CancelToken {
private:
  std::atomic<bool> value{false};

public:
  void cancel() { this->value.store(true); }

  bool isCanceled() const { return this->value.load(); }

  bool operator()() override { return this->isCanceled(); }
};

class AnalyzerContext {
private:
  std::shared_ptr<TypePool> pool;
  NameScopePtr scope;
  SourcePtr src;
  unsigned int gvarCount{0};
  TypeDiscardPoint typeDiscardPoint{};

public:
  NON_COPYABLE(AnalyzerContext);

  AnalyzerContext(const SysConfig &config, SourcePtr src);

  const NameScopePtr &getScope() const { return this->scope; }

  TypePool &getPool() { return *this->pool; }

  const auto &getPoolPtr() const { return this->pool; }

  ModId getModId() const { return this->scope->modId; }

  const auto &getSrc() const { return this->src; }

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
  ObserverPtr<LoggerBase> logger;

public:
  Analyzer(const SysConfig &config, SourceManager &src, ModuleArchives &archives,
           std::shared_ptr<CancelPoint> cancelPoint = nullptr,
           ObserverPtr<LoggerBase> logger = nullptr)
      : ModuleLoaderBase(config), srcMan(src), archives(archives),
        cancelPoint(std::move(cancelPoint)), logger(logger) {}

  ~Analyzer() override = default;

protected:
  std::unique_ptr<FrontEnd::Context> newContext(LexerPtr lexer) override;

  const ModType &
  newModTypeFromCurContext(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx) override;

  Ret load(const char *scriptDir, const char *modPath) override;

  ModResult addNewModEntry(CStrPtr &&ptr) override;

  const SysConfig &getSysConfig() const override;

  std::reference_wrapper<CancelToken> getCancelToken() const override;

private:
  const AnalyzerContextPtr &addNew(const SourcePtr &src);

  const AnalyzerContextPtr &current() const { return this->ctxs.back(); }

  void unwind() { // FIXME: future may be removed
    while (this->ctxs.size() > 1) {
      this->ctxs.pop_back();
    }
  }

public:
  void reset() { this->ctxs.clear(); }

  ModuleArchivePtr analyze(const SourcePtr &src, AnalyzerAction &action);

  enum class ExtraCompOp : unsigned int {
    CMD_ARG_COMP = 1u << 0u,
    SIGNATURE = 1u << 1u,
  };

  std::vector<CompletionItem> complete(const SourcePtr &src, unsigned int offset, CmdCompKind ckind,
                                       ExtraCompOp extraOp);

  Optional<SignatureInformation> collectSignature(const SourcePtr &src, unsigned int offset);
};

} // namespace ydsh::lsp

namespace ydsh {

template <>
struct allow_enum_bitop<lsp::Analyzer::ExtraCompOp> : std::true_type {};

} // namespace ydsh

#endif // YDSH_TOOLS_ANALYZER_ANALYZER_H
