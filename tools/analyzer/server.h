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
#include "transport.h"

namespace ydsh::lsp {

using namespace rpc;

struct LSPLogger : public LoggerBase {
  LSPLogger() : LoggerBase("YDSHD") {}
};

class LSPServer : public Handler {
private:
  LSPTransport transport;
  SourceManager srcMan;
  ModuleArchives archives;
  SymbolIndexes indexes;
  bool init{false};
  bool willExit{false};
  TraceValue traceSetting{TraceValue::off};
  MarkupKind markupKind{MarkupKind::PlainText};
  bool diagVersionSupport{false};

public:
  LSPServer(LoggerBase &logger, FilePtr &&in, FilePtr &&out)
      : Handler(logger), transport(logger, std::move(in), std::move(out)) {
    this->bindAll();
  }

  const LSPTransport &getTransport() const { return this->transport; }

  ReplyImpl onCall(const std::string &name, JSON &&param) override;

  /**
   *
   * @return
   * if cannot receive request, return false
   */
  bool runOnlyOnce() { return this->transport.dispatch(*this) == Transport::Status::DISPATCHED; }

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

  std::vector<Location> gotoDefinitionImpl(const SymbolRequest &request);

  std::vector<Location> findReferenceImpl(const SymbolRequest &request);

  Union<Hover, std::nullptr_t> hoverImpl(const Source &src, const SymbolRequest &request);

  DiagnosticEmitter newDiagnosticEmitter();

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

  // server to client method
  void publishDiagnostics(PublishDiagnosticsParams &&params) {
    this->notify("textDocument/publishDiagnostics", params);
  }
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SERVER_H
