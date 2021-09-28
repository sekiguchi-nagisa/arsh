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
}

void LSPServer::run() {
  while (true) {
    this->runOnlyOnce();
  }
}

SourcePtr LSPServer::resolveSource(const std::string &uriStr) {
  auto uri = uri::URI::fromString(uriStr);
  if (!uri) {
    LOG(LogLevel::ERROR, "broken uri: %s", uriStr.c_str());
    return nullptr;
  }
  if (uri.getScheme() != "file") {
    LOG(LogLevel::ERROR, "only support 'file' scheme at textDocument: %s", uriStr.c_str());
    return nullptr;
  }
  auto src = this->srcMan.find(uri.getPath());
  if (!src) {
    LOG(LogLevel::ERROR, "broken textDocument: %s", uriStr.c_str());
    return nullptr;
  }
  return src;
}

static Optional<SymbolRequest> toRequest(const Source &src, Position position) {
  auto pos = toTokenPos(src.getContent(), position);
  if (!pos.hasValue()) {
    return {};
  }
  return SymbolRequest{.modId = src.getSrcId(), .pos = pos.unwrap()};
}

void LSPServer::gotoDefinitionImpl(const Source &src, Position position,
                                   std::vector<Location> &result) {
  auto request = toRequest(src, position);
  if (!request.hasValue()) {
    return;
  }
  findDeclaration(this->indexes, request.unwrap(), [&](const FindDeclResult &ret) {
    auto s = this->srcMan.findById(ret.decl.getModId());
    assert(s);
    std::string uri = "file://";
    uri += s->getPath();
    auto range = toRange(s->getContent(), ret.decl.getToken());
    assert(range.hasValue());
    result.push_back(Location{.uri = std::move(uri), .range = range.unwrap()});
  });
}

void LSPServer::findReferenceImpl(const Source &src, Position position,
                                  std::vector<Location> &result) {
  auto request = toRequest(src, position);
  if (!request.hasValue()) {
    return;
  }
  findAllReferences(this->indexes, request.unwrap(), [&](const FindRefsResult &ret) {
    auto s = this->srcMan.findById(ret.symbol.getModId());
    assert(s);
    std::string uri = "file://";
    uri += s->getPath();
    auto range = toRange(s->getContent(), ret.symbol.getToken());
    assert(range.hasValue());
    result.push_back(Location{.uri = std::move(uri), .range = range.unwrap()});
  });
}

// RPC method definitions

Reply<InitializeResult> LSPServer::initialize(const InitializeParams &params) {
  LOG(LogLevel::INFO, "initialize server ....");
  if (this->init) {
    return newError(ErrorCode::InvalidRequest, "server has already initialized");
  }
  this->init = true;

  if (auto t = params.trace; t.hasValue()) {
    this->traceSetting = t.unwrap();
  } // FIXME: check client capability

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
    SymbolIndexer indexer(this->indexes);
    action.emitter.reset(&this->diagnosticEmitter);
    action.consumer.reset(&indexer);
    analyze(this->srcMan, this->archives, action, *src);
  }
}

void LSPServer::didCloseTextDocument(const DidCloseTextDocumentParams &params) {
  LOG(LogLevel::INFO, "close textDocument: %s", params.textDocument.uri.c_str());
  auto src = this->resolveSource(params.textDocument.uri);
  if (src) {
    this->archives.revertIfUnused(src->getSrcId());
  }
}

void LSPServer::didChangeTextDocument(const DidChangeTextDocumentParams &params) {
  LOG(LogLevel::INFO, "change textDocument: %s, %d", params.textDocument.uri.c_str(),
      params.textDocument.version);
  auto src = this->resolveSource(params.textDocument.uri);
  if (!src) {
    return;
  }
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
  SymbolIndexer indexer(this->indexes);
  action.emitter.reset(&this->diagnosticEmitter);
  action.consumer.reset(&indexer);
  analyze(this->srcMan, this->archives, action, *src);
}

Reply<std::vector<Location>> LSPServer::gotoDefinition(const DefinitionParams &params) {
  LOG(LogLevel::INFO, "definition at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  std::vector<Location> ret;
  if (auto src = this->resolveSource(params.textDocument.uri); src) {
    this->gotoDefinitionImpl(*src, params.position, ret);
  }
  return ret;
}

Reply<std::vector<Location>> LSPServer::findReference(const ReferenceParams &params) {
  LOG(LogLevel::INFO, "reference at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  std::vector<Location> ret;
  if (auto src = this->resolveSource(params.textDocument.uri); src) {
    this->findReferenceImpl(*src, params.position, ret);
  }
  return ret;
}

Reply<Union<Hover, std::nullptr_t>> LSPServer::hover(const HoverParams &params) {
  LOG(LogLevel::INFO, "hover at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  Union<Hover, std::nullptr_t> ret = nullptr;
  if (auto src = this->resolveSource(params.textDocument.uri); src) {
    if (auto request = toRequest(*src, params.position); request.hasValue()) {
      auto callback = [&](const FindDeclResult &value) {
        if (is<Hover>(ret)) {
          return;
        }
        ret = Hover{
            .contents =
                MarkupContent{
                    .kind = MarkupKind::Markdown,
                    .value = generateHoverContent(value.decl),
                },
            .range = toRange(src->getContent(), value.request.getToken()),
        };
      };
      findDeclaration(this->indexes, request.unwrap(), callback);
    }
  }
  return ret;
}

} // namespace ydsh::lsp