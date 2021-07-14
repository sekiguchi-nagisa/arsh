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
#include "source.h"

namespace ydsh::lsp {

// #######################
// ##     LSPServer     ##
// #######################

#define LOG(L, ...) (this->logger.get())(L, __VA_ARGS__)

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
}

void LSPServer::run() {
  while (true) {
    this->runOnlyOnce();
  }
}

Reply<InitializeResult> LSPServer::initialize(const InitializeParams &params) {
  LOG(LogLevel::INFO, "initialize server ....");
  if (this->init) {
    return newError(ErrorCode::InvalidRequest, "server has already initialized");
  }
  this->init = true;

  if (auto t = params.trace; t.hasValue()) {
    this->traceSetting = t.unwrap();
  } // FIXME: check client capability

  InitializeResult ret; // FIXME: set supported capabilities
  ret.capabilities.textDocumentSync = TextDocumentSyncOptions{
      .openClose = true,
      .change = TextDocumentSyncKind::Full,
      .willSave = {},
      .willSaveWaitUntil = {},
      .save = {},
  };
  return std::move(ret);
}

void LSPServer::initialized(const ydsh::lsp::InitializedParams &) {
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
  auto ctx = this->provider.find(uri);
  if (ctx) {
    LOG(LogLevel::INFO, "already opened textDocument: %s", uriStr);
  } else {
    auto *src = this->srcMan.update(uri.getPath(), params.textDocument.version,
                                    std::string(params.textDocument.text));
    if (!src) {
      LOG(LogLevel::ERROR, "reach opened file limit"); // FIXME: report to client?
      return;
    }
    ctx = this->provider.addNew(uri, *src);
    buildIndex(this->provider, this->diagnosticEmitter, ctx);
  }
}

void LSPServer::didCloseTextDocument(const DidCloseTextDocumentParams &params) {
  LOG(LogLevel::INFO, "close textDocument: %s", params.textDocument.uri.c_str());
  // FIXME: check dependency
}

void LSPServer::didChangeTextDocument(const DidChangeTextDocumentParams &params) {
  const char *uriStr = params.textDocument.uri.c_str();
  LOG(LogLevel::INFO, "change textDocument: %s, %d", uriStr, params.textDocument.version);
  auto uri = uri::URI::fromString(params.textDocument.uri);
  if (!uri) {
    LOG(LogLevel::ERROR, "broken uri: %s", uriStr);
    return;
  }
  auto ctx = this->provider.find(uri);
  if (!ctx) {
    LOG(LogLevel::ERROR, "broken textDocument: %s", uriStr);
    return;
  }
  std::string content = ctx->getContent(); //FIXME:
  for (auto &change : params.contentChanges) {
    if (!applyChange(content, change)) {
      LOG(LogLevel::ERROR, "textDocument may lack consistency");
      return;
    }
  }
  auto *src = this->srcMan.update(uri.getPath(), params.textDocument.version, std::move(content));
  revertIndexMap(this->indexMap, {src->getSrcId()});
  ctx->updateContent(std::string(src->getContent()), params.textDocument.version);
  buildIndex(this->provider, this->diagnosticEmitter, ctx);
}

} // namespace ydsh::lsp