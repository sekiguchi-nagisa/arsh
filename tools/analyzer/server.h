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

#ifndef ARSH_TOOLS_ANALYZER_SERVER_H
#define ARSH_TOOLS_ANALYZER_SERVER_H

#include "../json/jsonrpc.h"
#include "analyzer.h"
#include "analyzer_worker.h"
#include "index.h"
#include "lsp.h"
#include "registration.h"
#include "semantic_token.h"
#include "transport.h"
#include "worker.h"

namespace arsh::lsp {

using namespace rpc;

struct LSPLogger : public LoggerBase {
  LSPLogger() : LoggerBase("ARSHD") {}
};

class AnalyzerResult {
public:
  std::shared_ptr<SourceManager> srcMan;
  ModuleArchives archives;
  SymbolIndexes indexes;
  std::unordered_set<ModId> modifiedSrcIds;

  NON_COPYABLE(AnalyzerResult);

private:
  AnalyzerResult(std::shared_ptr<SourceManager> srcMan, ModuleArchives archives,
                 SymbolIndexes indexes, std::unordered_set<ModId> modifiedSrcIds)
      : srcMan(std::move(srcMan)), archives(std::move(archives)), indexes(std::move(indexes)),
        modifiedSrcIds(std::move(modifiedSrcIds)) {}

public:
  explicit AnalyzerResult(std::shared_ptr<SourceManager> srcMan) : srcMan(std::move(srcMan)) {}

  AnalyzerResult(AnalyzerResult &&o) noexcept
      : srcMan(std::move(o.srcMan)), archives(std::move(o.archives)), indexes(std::move(o.indexes)),
        modifiedSrcIds(std::move(o.modifiedSrcIds)) {}

  AnalyzerResult &operator=(AnalyzerResult &&o) noexcept {
    if (this != std::addressof(o)) {
      this->~AnalyzerResult();
      new (this) AnalyzerResult(std::move(o));
    }
    return *this;
  }

  AnalyzerResult deepCopy() const {
    return {this->srcMan->copy(), this->archives, this->indexes, this->modifiedSrcIds};
  }
};

enum class SupportedCapability : unsigned short {
  DIAG_VERSION = 1u << 0u,
  LABEL_DETAIL = 1u << 1u,
  WORKSPACE_CONFIG = 1u << 2u,
  SEMANTIC_TOKEN_REGISTRATION = 1u << 3u,
  PREPARE_RENAME = 1u << 4u,
  RENAME_CHANGE_ANNOTATION = 1u << 5u,
  CHANGE_ANNOTATION = 1u << 6u,
  VERSIONED_DOCUMENT_CHANGE = 1u << 7u,
};

class LSPServer : public Handler {
private:
  const SysConfig sysConfig;
  SemanticTokenEncoder encoder;
  IDGenerator idGenerator;
  RegistrationMap registrationMap;
  LSPTransport transport;
  SingleBackgroundWorker rpcHandlerWorker;
  std::mutex rebuildMutex;
  AnalyzerResult result;
  SingleBackgroundWorker analyzerWorker;
  std::future<AnalyzerResult> futureResult;
  std::shared_ptr<CancelPoint> cancelPoint;
  const int defaultDebounceTime;
  std::atomic_int timeout{defaultDebounceTime};
  bool init{false};
  bool willExit{false};
  TraceValue traceSetting{TraceValue::off};
  MarkupKind markupKind{MarkupKind::PlainText};
  BinaryFlag fileNameComp{BinaryFlag::disabled}; // complete filename (also external command)
  BinaryFlag semanticHighlight{BinaryFlag::enabled};
  BinaryFlag renameSupport{BinaryFlag::enabled};
  SupportedCapability supportedCapability{};
  ContextManager contextMan;
  std::shared_ptr<Context> currentCtx; // only available within rpc worker

public:
  LSPServer(LoggerBase &logger, int inFd, int outFd, int time, uint64_t seed = 42)
      : Handler(logger, seed), idGenerator(seed), transport(logger, inFd, outFd),
        rpcHandlerWorker(16), result(std::make_shared<SourceManager>()), defaultDebounceTime(time) {
    this->bindAll();
  }

  void setTestWorkDir(std::string &&dir) { this->result.srcMan->setTestWorkDir(std::move(dir)); }

  void onCall(Transport &transport, Request &&req) override;

  void onNotify(Request &&req) override;

  void onResponse(Response &&res) override;

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
  auto call(const std::string &name, Param &&param, Func callback, Error ecallback) {
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

  std::vector<DocumentHighlight> documentHighlightImpl(const SymbolRequest &request) const;

  Union<Hover, std::nullptr_t> hoverImpl(const Source &src, const SymbolRequest &request) const;

  Result<WorkspaceEdit, std::string> renameImpl(const SymbolRequest &request,
                                                const std::string &newName) const;

  void loadConfigSetting(const ConfigSetting &setting);

  DiagnosticEmitter newDiagnosticEmitter(std::shared_ptr<SourceManager> srcMan);

  bool tryRebuild();

  void updateSource(StringRef path, int newVersion, std::string &&newContent);

  void syncResult();

  std::pair<std::shared_ptr<SourceManager>, ModuleArchives> snapshot() const;

public:
  // RPC method definitions

  Reply<InitializeResult> initialize(const InitializeParams &params);

  void initialized(const InitializedParams &params);

  Reply<void> shutdown();

  void exit();

  void tryCancel(const CancelParams &param); // only called from main thread

  void setTrace(const SetTraceParams &param);

  void didOpenTextDocument(const DidOpenTextDocumentParams &params);

  void didCloseTextDocument(const DidCloseTextDocumentParams &params);

  void didChangeTextDocument(const DidChangeTextDocumentParams &params);

  Reply<std::vector<Location>> gotoDefinition(const DefinitionParams &params);

  Reply<std::vector<Location>> findReference(const ReferenceParams &params);

  Reply<std::vector<DocumentHighlight>> documentHighlight(const DocumentHighlightParams &params);

  Reply<Union<Hover, std::nullptr_t>> hover(const HoverParams &params);

  Reply<std::vector<CompletionItem>> complete(const CompletionParams &params);

  void didChangeConfiguration(const DidChangeConfigurationParams &params);

  Reply<Union<SemanticTokens, std::nullptr_t>> semanticToken(const SemanticTokensParams &params);

  Reply<std::vector<DocumentLink>> documentLink(const DocumentLinkParams &params);

  Reply<std::vector<DocumentSymbol>> documentSymbol(const DocumentSymbolParams &params);

  Reply<Union<SignatureHelp, std::nullptr_t>> signatureHelp(const SignatureHelpParams &params);

  Reply<WorkspaceEdit> rename(const RenameParams &params);

  Reply<Union<PrepareRename, std::nullptr_t>> prepareRename(const PrepareRenameParams &params);

  // server to client method
  void publishDiagnostics(PublishDiagnosticsParams &&params) {
    this->notify("textDocument/publishDiagnostics", std::move(params));
  }

  void registerCapability(RegistrationParam &&param) {
    this->notify("client/registerCapability", std::move(param));
  }

  void unregisterCapability(UnregistrationParam &&param) {
    this->notify("client/unregisterCapability", std::move(param));
  }
};

} // namespace arsh::lsp

template <>
struct arsh::allow_enum_bitop<arsh::lsp::SupportedCapability> : std::true_type {};

#endif // ARSH_TOOLS_ANALYZER_SERVER_H
