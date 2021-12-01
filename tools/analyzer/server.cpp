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

#include "server.h"
#include "hover.h"
#include "indexer.h"
#include "source.h"

namespace ydsh::lsp {

// #######################
// ##     LSPServer     ##
// #######################

#define LOG(L, ...)                                                                                \
  do {                                                                                             \
    this->logger.get().enabled(L) && (this->logger.get())(L, __VA_ARGS__);                         \
  } while (false)

ReplyImpl LSPServer::onCall(const std::string &name, JSON &&param) {
  if (!this->init && name != "initialize") {
    LOG(LogLevel::ERROR, "must be initialized");
    return newError(LSPErrorCode::ServerNotInitialized, "server not initialized!!");
  }
  return Handler::onCall(name, std::move(param));
}

void LSPServer::bindAll() {
  this->bind("shutdown", &LSPServer::shutdown);
  this->bind("exit", &LSPServer::exit);
  this->bind("initialize", &LSPServer::initialize);
  this->bind("initialized", &LSPServer::initialized);
  this->bind("$/setTrace", &LSPServer::setTrace);
  this->bind("textDocument/didOpen", &LSPServer::didOpenTextDocument);
  this->bind("textDocument/didClose", &LSPServer::didCloseTextDocument);
  this->bind("textDocument/didChange", &LSPServer::didChangeTextDocument);
  this->bind("textDocument/definition", &LSPServer::gotoDefinition);
  this->bind("textDocument/references", &LSPServer::findReference);
  this->bind("textDocument/hover", &LSPServer::hover);
  this->bind("textDocument/completion", &LSPServer::complete);
}

void LSPServer::run() {
  while (true) {
    this->runOnlyOnce();
  }
}

static CStrPtr format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static CStrPtr format(const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    fatal_perror("");
  }
  va_end(arg);
  return CStrPtr(str);
}

Result<SourcePtr, CStrPtr> LSPServer::resolveSource(const TextDocumentIdentifier &doc) {
  auto uri = uri::URI::fromString(doc.uri);
  if (!uri) {
    auto str = format("broken uri: %s", doc.uri.c_str());
    LOG(LogLevel::ERROR, "%s", str.get());
    return Err(std::move(str));
  }
  if (uri.getScheme() != "file") {
    auto str = format("only support 'file' scheme at textDocument: %s", doc.uri.c_str());
    LOG(LogLevel::ERROR, "%s", str.get());
    return Err(std::move(str));
  }
  auto src = this->srcMan.find(uri.getPath());
  if (!src) {
    auto str = format("broken textDocument: %s", doc.uri.c_str());
    LOG(LogLevel::ERROR, "%s", str.get());
    return Err(std::move(str));
  }
  return Ok(std::move(src));
}

static Optional<SymbolRequest> toRequest(const Source &src, Position position) {
  auto pos = toTokenPos(src.getContent(), position);
  if (!pos.hasValue()) {
    return {};
  }
  return SymbolRequest{.modId = src.getSrcId(), .pos = pos.unwrap()};
}

Result<std::pair<SourcePtr, SymbolRequest>, CStrPtr>
LSPServer::resolvePosition(const TextDocumentPositionParams &params) {
  auto resolved = this->resolveSource(params.textDocument);
  if (!resolved) {
    return Err(std::move(resolved).takeError());
  }
  auto src = std::move(resolved).take();
  assert(src);
  auto req = toRequest(*src, params.position);
  if (!req.hasValue()) {
    auto str = format("broken position at: %s:%s", params.textDocument.uri.c_str(),
                      params.position.toString().c_str());
    LOG(LogLevel::ERROR, "%s", str.get());
    return Err(std::move(str));
  }
  return Ok(std::make_pair(std::move(src), req.unwrap()));
}

std::vector<Location> LSPServer::gotoDefinitionImpl(const SymbolRequest &request) {
  std::vector<Location> result;
  findDeclaration(this->indexes, request, [&](const FindDeclResult &ret) {
    if (ret.decl.getModId() == 0) { // ignore builtin module symbol
      return;
    }
    auto s = this->srcMan.findById(ret.decl.getModId());
    assert(s);
    std::string uri = "file://";
    uri += s->getPath();
    auto range = toRange(s->getContent(), ret.decl.getToken());
    assert(range.hasValue());
    result.push_back(Location{.uri = std::move(uri), .range = range.unwrap()});
  });
  return result;
}

std::vector<Location> LSPServer::findReferenceImpl(const SymbolRequest &request) {
  std::vector<Location> result;
  findAllReferences(this->indexes, request, [&](const FindRefsResult &ret) {
    auto s = this->srcMan.findById(ret.symbol.getModId());
    assert(s);
    std::string uri = "file://";
    uri += s->getPath();
    auto range = toRange(s->getContent(), ret.symbol.getToken());
    assert(range.hasValue());
    result.push_back(Location{.uri = std::move(uri), .range = range.unwrap()});
  });
  return result;
}

DiagnosticEmitter LSPServer::newDiagnosticEmitter() {
  return {this->srcMan,
          [&](PublishDiagnosticsParams &&params) { this->publishDiagnostics(std::move(params)); },
          this->diagVersionSupport};
}

// RPC method definitions

Reply<InitializeResult> LSPServer::initialize(const InitializeParams &params) {
  LOG(LogLevel::INFO, "initialize server ....");
  if (this->init) {
    LOG(LogLevel::ERROR, "server has already initialized");
    return newError(ErrorCode::InvalidRequest, "server has already initialized");
  }
  this->init = true;

  // check client capability
  if (auto t = params.trace; t.hasValue()) {
    this->traceSetting = t.unwrap();
  }
  if (params.capabilities.textDocument.hasValue()) {
    auto &textDocument = params.capabilities.textDocument.unwrap();
    if (textDocument.publishDiagnostics.hasValue()) {
      auto &diag = textDocument.publishDiagnostics.unwrap();
      if (diag.versionSupport.hasValue()) {
        this->diagVersionSupport = diag.versionSupport.unwrap();
      }
    }
  }

  InitializeResult ret;
  ret.capabilities.textDocumentSync = TextDocumentSyncOptions{
      .openClose = true,
      .change = TextDocumentSyncKind::Full,
      .willSave = {},
      .willSaveWaitUntil = {},
      .save = {},
  };
  ret.capabilities.definitionProvider = true;
  ret.capabilities.referencesProvider = true;
  ret.capabilities.hoverProvider = true;
  ret.capabilities.completionProvider = CompletionOptions{
      .resolveProvider = {},
      .triggerCharacters = std::vector<std::string>{".", "$", "/"},
  };
  return ret;
}

void LSPServer::initialized(const InitializedParams &) {
  LOG(LogLevel::INFO, "server initialized!!");
}

Reply<void> LSPServer::shutdown() {
  LOG(LogLevel::INFO, "try to shutdown ....");
  this->willExit = true;
  return nullptr;
}

void LSPServer::exit() {
  int s = this->willExit ? 0 : 1;
  LOG(LogLevel::INFO, "exit server: %d", s);
  std::exit(s); // always success
}

void LSPServer::setTrace(const SetTraceParams &param) {
  LOG(LogLevel::INFO, "change trace setting '%s' to '%s'", toString(this->traceSetting),
      toString(param.value));
  this->traceSetting = param.value;
}

void LSPServer::didOpenTextDocument(const DidOpenTextDocumentParams &params) {
  const char *uriStr = params.textDocument.uri.c_str();
  LOG(LogLevel::INFO, "open textDocument: %s", uriStr);
  auto uri = uri::URI::fromString(params.textDocument.uri);
  if (!uri) {
    LOG(LogLevel::ERROR, "broken uri: %s", uriStr);
    return;
  }
  auto src = this->srcMan.find(uri.getPath());
  if (src) {
    LOG(LogLevel::INFO, "already opened textDocument: %s", uriStr);
  } else {
    src = this->srcMan.update(uri.getPath(), params.textDocument.version,
                              std::string(params.textDocument.text));
    if (!src) {
      LOG(LogLevel::ERROR, "reach opened file limit"); // FIXME: report to client?
      return;
    }
    AnalyzerAction action;
    DiagnosticEmitter emitter = this->newDiagnosticEmitter();
    SymbolIndexer indexer(this->indexes);
    action.emitter.reset(&emitter);
    action.consumer.reset(&indexer);
    analyze(this->srcMan, this->archives, action, *src);
  }
}

void LSPServer::didCloseTextDocument(const DidCloseTextDocumentParams &params) {
  if (auto resolved = this->resolveSource(params.textDocument)) {
    auto &src = resolved.asOk();
    if (this->archives.revertIfUnused(src->getSrcId())) {
      LOG(LogLevel::INFO, "close textDocument: %s", params.textDocument.uri.c_str());
      this->indexes.remove(src->getSrcId());
    }
  }
}

void LSPServer::didChangeTextDocument(const DidChangeTextDocumentParams &params) {
  LOG(LogLevel::INFO, "change textDocument: %s, %d", params.textDocument.uri.c_str(),
      params.textDocument.version);
  auto resolved = this->resolveSource(params.textDocument);
  if (!resolved) {
    return;
  }
  auto src = std::move(resolved).take();
  std::string content = src->getContent();
  for (auto &change : params.contentChanges) {
    if (!applyChange(content, change)) {
      LOG(LogLevel::ERROR, "textDocument may lack consistency");
      return;
    }
  }
  src = this->srcMan.update(src->getPath(), params.textDocument.version, std::move(content));
  this->archives.revert({src->getSrcId()});
  AnalyzerAction action;
  DiagnosticEmitter emitter = this->newDiagnosticEmitter();
  SymbolIndexer indexer(this->indexes);
  action.emitter.reset(&emitter);
  action.consumer.reset(&indexer);
  analyze(this->srcMan, this->archives, action, *src);
}

Reply<std::vector<Location>> LSPServer::gotoDefinition(const DefinitionParams &params) {
  LOG(LogLevel::INFO, "definition at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  if (auto resolved = this->resolvePosition(params)) {
    return this->gotoDefinitionImpl(resolved.asOk().second);
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<std::vector<Location>> LSPServer::findReference(const ReferenceParams &params) {
  LOG(LogLevel::INFO, "reference at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  if (auto resolved = this->resolvePosition(params)) {
    return this->findReferenceImpl(resolved.asOk().second);
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<Union<Hover, std::nullptr_t>> LSPServer::hover(const HoverParams &params) {
  LOG(LogLevel::INFO, "hover at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  if (auto resolved = this->resolvePosition(params)) {
    auto &src = *resolved.asOk().first;
    auto &request = resolved.asOk().second;
    Union<Hover, std::nullptr_t> ret = nullptr;
    auto callback = [&](const FindDeclResult &value) {
      if (is<Hover>(ret)) {
        return;
      }
      ret = Hover{
          .contents =
              MarkupContent{
                  .kind = MarkupKind::Markdown,
                  .value = generateHoverContent(this->srcMan, src, value.decl),
              },
          .range = toRange(src.getContent(), value.request.getToken()),
      };
    };
    findDeclaration(this->indexes, request, callback);
    return ret;
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<std::vector<CompletionItem>> LSPServer::complete(const CompletionParams &params) {
  LOG(LogLevel::INFO, "completion at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  if (auto resolved = this->resolvePosition(params)) {
    auto &src = *resolved.asOk().first;
    auto pos = resolved.asOk().second.pos;
    Source newSrc(src.getPath(), src.getSrcId(), std::string(src.getContent().c_str(), pos),
                  src.getVersion());
    auto copied = this->archives.copy();
    copied.revert({newSrc.getSrcId()});
    return doCompletion(this->srcMan, copied, newSrc);
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

} // namespace ydsh::lsp