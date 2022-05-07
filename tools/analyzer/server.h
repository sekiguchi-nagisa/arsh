/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_ANALYZER_SERVER_H
#define YDSH_TOOLS_ANALYZER_SERVER_H

#include "../json/jsonrpc.h"
#include "analyzer.h"
#include "index.h"
#include "lsp.h"
#include "semantic_token.h"
#include "transport.h"
#include "worker.h"

namespace ydsh::lsp {

using namespace rpc;

struct LSPLogger : public LoggerBase {
  LSPLogger() : LoggerBase("YDSHD") {}
};

class AnalyzerResult {
public:
  std::shared_ptr<SourceManager> srcMan;
  ModuleArchives archives;
  SymbolIndexes indexes;
  std::unordered_set<unsigned short> modifiedSrcIds;
  std::unordered_set<unsigned short> closingSrcIds;

  NON_COPYABLE(AnalyzerResult);

private:
  AnalyzerResult(std::shared_ptr<SourceManager> srcMan, ModuleArchives archives,
                 SymbolIndexes indexes, std::unordered_set<unsigned short> modifiedSrcIds,
                 std::unordered_set<unsigned short> willCloseSrcIds)
      : srcMan(std::move(srcMan)), archives(std::move(archives)), indexes(std::move(indexes)),
        modifiedSrcIds(std::move(modifiedSrcIds)), closingSrcIds(std::move(willCloseSrcIds)) {}

public:
  explicit AnalyzerResult(std::shared_ptr<SourceManager> srcMan) : srcMan(std::move(srcMan)) {}

  AnalyzerResult(AnalyzerResult &&o) noexcept
      : srcMan(std::move(o.srcMan)), archives(std::move(o.archives)), indexes(std::move(o.indexes)),
        modifiedSrcIds(std::move(o.modifiedSrcIds)), closingSrcIds(std::move(o.closingSrcIds)) {}

  AnalyzerResult &operator=(AnalyzerResult &&o) noexcept {
    if (this != std::addressof(o)) {
      this->~AnalyzerResult();
      new (this) AnalyzerResult(std::move(o));
    }
    return *this;
  }

  AnalyzerResult deepCopy() const {
    return {this->srcMan->copy(), this->archives, this->indexes, this->modifiedSrcIds,
            this->closingSrcIds};
  }
};

class LSPServer : public Handler {
private:
  const SysConfig sysConfig;
  SemanticTokenEncoder encoder;
  LSPTransport transport;
  AnalyzerResult result;
  BackgroundWorker worker;
  std::future<AnalyzerResult> futureResult;
  std::shared_ptr<CancelPoint> cancelPoint;
  const int defaultDebounceTime;
  int timeout{defaultDebounceTime};
  bool init{false};
  bool willExit{false};
  TraceValue traceSetting{TraceValue::off};
  MarkupKind markupKind{MarkupKind::PlainText};
  bool diagVersionSupport{false};
  CmdCompKind cmdCompKind{CmdCompKind::default_};
  bool cmdArgCompEnabled{false};

public:
  LSPServer(LoggerBase &logger, FilePtr &&in, FilePtr &&out, int time)
      : Handler(logger), encoder(SemanticTokensLegend::create()),
        transport(logger, std::move(in), std::move(out)), result(std::make_shared<SourceManager>()),
        defaultDebounceTime(time) {
    this->bindAll();
  }

  void setTestWorkDir(std::string &&dir) { this->result.srcMan->setTestWorkDir(std::move(dir)); }

  ReplyImpl onCall(const std::string &name, JSON &&param) override;

  /**
   * normally not return
   */
  void run();

private:
  /**
   * bind all of methods
   */
  void bindAll();

  template <typename Ret, typename Param>
  void bind(const std::string &name, Reply<Ret> (LSPServer::*method)(const Param &)) {
    Handler::bind(name, this, method);
  }

  template <typename Ret>
  void bind(const std::string &name, Reply<Ret> (LSPServer::*method)()) {
    Handler::bind(name, this, method);
  }

  template <typename Param>
  void bind(const std::string &name, void (LSPServer::*method)(const Param &)) {
    Handler::bind(name, this, method);
  }

  void bind(const std::string &name, void (LSPServer::*method)()) {
    Handler::bind(name, this, method);
  }

  template <typename Ret, typename Param, typename Func, typename Error>
  auto call(const std::string &name, const Param &param, Func callback, Error ecallback) {
    return Handler::call<Ret>(this->transport, name, param, std::forward<Func>(callback),
                              std::forward<Error>(ecallback));
  }

  template <typename Param>
  void notify(const std::string &name, Param &&param) {
    Handler::notify(this->transport, name, param);
  }

  Result<SourcePtr, CStrPtr> resolveSource(const TextDocumentIdentifier &doc);

  Result<std::pair<SourcePtr, SymbolRequest>, CStrPtr>
  resolvePosition(const TextDocumentPositionParams &params);

  std::vector<Location> gotoDefinitionImpl(const SymbolRequest &request) const;

  std::vector<Location> findReferenceImpl(const SymbolRequest &request) const;

  Union<Hover, std::nullptr_t> hoverImpl(const Source &src, const SymbolRequest &request) const;

  DiagnosticEmitter newDiagnosticEmitter(std::shared_ptr<SourceManager> srcMan);

  bool tryRebuild();

  void updateSource(StringRef path, int newVersion, std::string &&newContent);

  void syncResult();

public:
  // RPC method definitions

  Reply<InitializeResult> initialize(const InitializeParams &params);

  void initialized(const InitializedParams &params);

  Reply<void> shutdown();

  void exit();

  void setTrace(const SetTraceParams &param);

  void didOpenTextDocument(const DidOpenTextDocumentParams &params);

  void didCloseTextDocument(const DidCloseTextDocumentParams &params);

  void didChangeTextDocument(const DidChangeTextDocumentParams &params);

  Reply<std::vector<Location>> gotoDefinition(const DefinitionParams &params);

  Reply<std::vector<Location>> findReference(const ReferenceParams &params);

  Reply<Union<Hover, std::nullptr_t>> hover(const HoverParams &params);

  Reply<std::vector<CompletionItem>> complete(const CompletionParams &params);

  void didChangeConfiguration(const DidChangeConfigurationParams &params);

  Reply<Union<SemanticTokens, std::nullptr_t>> semanticToken(const SemanticTokensParams &params);

  // server to client method
  void publishDiagnostics(PublishDiagnosticsParams &&params) {
    this->notify("textDocument/publishDiagnostics", params);
  }
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SERVER_H
