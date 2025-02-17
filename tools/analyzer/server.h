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
  SemanticTokenEncoder encoder;
  IDGenerator idGenerator;
  RegistrationMap registrationMap;
  LSPTransport transport;
  SingleBackgroundWorker rpcHandlerWorker;
  const std::string testDir;
  std::unique_ptr<AnalyzerWorker> worker;
  const unsigned int defaultDebounceTime;
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
  LSPServer(LoggerBase &logger, int inFd, int outFd, unsigned int time, const std::string &testDir,
            uint64_t seed = 42)
      : Handler(logger, seed), idGenerator(seed), transport(logger, inFd, outFd),
        rpcHandlerWorker(16), testDir(testDir), defaultDebounceTime(time) {
    this->bindAll();
  }

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

  void replyCancelError(JSON &&id);

  template <typename Func, enable_when<AnalyzerWorker::reader_requirement_v<Func>> = nullptr>
  ErrHolder<Error> setAnalyzerFinishedCallback(Func func) {
    this->worker->asyncStateWith(
        [this, func, ctx = this->currentCtx](const AnalyzerWorker::State &state) {
          if (ctx->getCancelPoint()->isCanceled()) {
            this->replyCancelError(JSON(ctx->getId()));
          } else {
            ReplyImpl ret = func(state);
            this->reply(this->transport, JSON(ctx->getId()), std::move(ret));
          }
        });
    return newError(LSPErrorCode::NONBlock, "");
  }

  static std::vector<Location> gotoDefinitionImpl(const AnalyzerWorker::State &state,
                                                  const SymbolRequest &request);

  static std::vector<Location> findReferenceImpl(const AnalyzerWorker::State &state,
                                                 const SymbolRequest &request);

  static std::vector<DocumentHighlight> documentHighlightImpl(const AnalyzerWorker::State &state,
                                                              const SymbolRequest &request);

  static Union<Hover, std::nullptr_t> hoverImpl(const AnalyzerWorker::State &state,
                                                MarkupKind markupKind, const Source &src,
                                                const SymbolRequest &request);

  static Result<WorkspaceEdit, std::string>
  renameImpl(const AnalyzerWorker::State &state, SupportedCapability capability, bool supportRename,
             const SymbolRequest &request, const std::string &newName);

  void loadConfigSetting(const ConfigSetting &setting);

protected:
  void reply(Transport &transport, JSON &&id, ReplyImpl &&ret) override;

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
