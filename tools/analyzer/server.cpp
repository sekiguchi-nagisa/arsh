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
#include "indexer.h"
#include "source.h"
#include "symbol.h"

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
  this->bind("textDocument/documentHighlight", &LSPServer::documentHighlight);
  this->bind("textDocument/hover", &LSPServer::hover);
  this->bind("textDocument/documentLink", &LSPServer::documentLink);
  this->bind("textDocument/documentSymbol", &LSPServer::documentSymbol);
  this->bind("textDocument/completion", &LSPServer::complete);
  this->bind("textDocument/semanticTokens/full", &LSPServer::semanticToken);
  this->bind("workspace/didChangeConfiguration", &LSPServer::didChangeConfiguration);
}

void LSPServer::run() {
  while (this->transport.available()) {
    auto s = this->transport.dispatch(*this, this->timeout);
    if (s == Transport::Status::TIMEOUT) {
      this->tryRebuild();
    }
  }
  LOG(LogLevel::ERROR, "io stream reach eof or fatal error. terminate immediately");
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
  auto uri = uri::URI::parse(doc.uri);
  if (!uri) {
    auto str = format("broken uri: %s", doc.uri.c_str());
    LOG(LogLevel::ERROR, "%s", str.get());
    return Err(std::move(str));
  }
  auto fullPath = resolveURI(*this->result.srcMan, uri);
  auto src = this->result.srcMan->find(fullPath);
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

std::vector<Location> LSPServer::gotoDefinitionImpl(const SymbolRequest &request) const {
  std::vector<Location> values;
  findDeclaration(this->result.indexes, request, [&](const FindDeclResult &ret) {
    if (ret.decl.getModId() == 0 || hasFlag(ret.decl.getAttr(), DeclSymbol::Attr::BUILTIN)) {
      /**
       * ignore builtin module symbols and builtin type fields/methods
       */
      return;
    }
    auto s = this->result.srcMan->findById(ret.decl.getModId());
    assert(s);
    std::string uri = "file://";
    uri += s->getPath();
    auto range = toRange(*s, ret.decl.getToken());
    assert(range.hasValue());
    values.push_back(Location{.uri = std::move(uri), .range = range.unwrap()});
  });
  return values;
}

std::vector<Location> LSPServer::findReferenceImpl(const SymbolRequest &request) const {
  std::vector<Location> values;
  findAllReferences(this->result.indexes, request, [&](const FindRefsResult &ret) {
    auto s = this->result.srcMan->findById(ret.symbol.getModId());
    assert(s);
    std::string uri = "file://";
    uri += s->getPath();
    auto range = toRange(*s, ret.symbol.getToken());
    assert(range.hasValue());
    values.push_back(Location{.uri = std::move(uri), .range = range.unwrap()});
  });
  return values;
}

std::vector<DocumentHighlight>
LSPServer::documentHighlightImpl(const SymbolRequest &request) const {
  const DeclSymbol *decl = nullptr;
  findDeclaration(this->result.indexes, request,
                  [&](const FindDeclResult &value) { decl = &value.decl; });
  if (!decl) {
    return {};
  }

  auto src = this->result.srcMan->findById(request.modId);
  assert(src);
  SymbolRequest req = {.modId = decl->getModId(), .pos = decl->getPos()};
  std::vector<DocumentHighlight> values;
  findAllReferences(
      this->result.indexes, req,
      [&](const FindRefsResult &value) {
        if (value.symbol.getModId() == request.modId) {
          auto range = toRange(*src, value.symbol.getToken());
          assert(range.hasValue());
          values.push_back(DocumentHighlight{
              .range = range.unwrap(),
              .kind = DocumentHighlightKind::Text,
          });
        }
      },
      false);
  return values;
}

Union<Hover, std::nullptr_t> LSPServer::hoverImpl(const Source &src,
                                                  const SymbolRequest &request) const {
  Union<Hover, std::nullptr_t> ret = nullptr;
  findDeclaration(this->result.indexes, request, [&](const FindDeclResult &value) {
    if (is<Hover>(ret)) {
      return;
    }
    ret = Hover{
        .contents =
            MarkupContent{
                .kind = this->markupKind,
                .value = generateHoverContent(*this->result.srcMan, src, value.decl,
                                              this->markupKind == MarkupKind::Markdown),
            },
        .range = toRange(src, value.request.getToken()),
    };
  });
  return ret;
}

DiagnosticEmitter LSPServer::newDiagnosticEmitter(std::shared_ptr<SourceManager> srcMan) {
  return {std::move(srcMan),
          [&](PublishDiagnosticsParams &&params) { this->publishDiagnostics(std::move(params)); },
          this->diagVersionSupport};
}

struct AnalyzerParam {
  std::reference_wrapper<const SysConfig> sysConfig;
  std::reference_wrapper<LoggerBase> logger;
  std::shared_ptr<CancelPoint> cancelPoint;
  AnalyzerResult ret;
  DiagnosticEmitter emitter;
};

static AnalyzerResult doRebuild(AnalyzerParam &&param) {
  // prepare
  {
    auto tmp(param.ret.modifiedSrcIds);
    param.ret.archives.revert(std::move(tmp));
  }

  AnalyzerAction action;
  SymbolIndexer indexer(param.sysConfig, param.ret.indexes);
  MultipleNodePass passes;
  passes.add(makeObserver(indexer));
  action.emitter.reset(&param.emitter);
  action.pass = makeObserver(passes);

  // rebuild
  Analyzer analyzer(param.sysConfig, *param.ret.srcMan, param.ret.archives, param.cancelPoint,
                    makeObserver(param.logger.get()));
  for (auto &e : param.ret.modifiedSrcIds) {
    if (param.ret.archives.find(e)) {
      continue;
    }
    auto src = param.ret.srcMan->findById(e);
    assert(src);
    if (!analyzer.analyze(*src, action)) {
      break;
    }
  }

  while (!param.cancelPoint->isCanceled()) {
    auto targetId = param.ret.archives.getFirstRevertedModId();
    if (!targetId.hasValue()) {
      break;
    }
    auto src = param.ret.srcMan->findById(targetId.unwrap());
    assert(src);
    analyzer.analyze(*src, action);
  }

  if (!param.cancelPoint->isCanceled()) {
    // close
    for (auto &id : param.ret.closingSrcIds) {
      if (param.ret.archives.removeIfUnused(id)) {
        auto src = param.ret.srcMan->findById(id);
        param.logger.get()(LogLevel::INFO, "close textDocument: %s", src->getPath().c_str());
        param.ret.indexes.remove(id);
      }
    }
    param.ret.modifiedSrcIds.clear();
    param.ret.closingSrcIds.clear();
  }
  return std::move(param.ret);
}

bool LSPServer::tryRebuild() {
  if (this->result.modifiedSrcIds.empty()) {
    this->timeout = -1;
    return false;
  }

  LOG(LogLevel::INFO, "tryRebuild...");

  if (this->cancelPoint) {
    this->cancelPoint->cancel();
  }
  if (this->futureResult.valid()) { // synchronize source from already running analyzer job
    auto tmp = this->futureResult.get();

    // merge changed src
    tmp.modifiedSrcIds.merge(this->result.modifiedSrcIds);
    this->result.modifiedSrcIds.clear();
    for (auto &id : tmp.modifiedSrcIds) {
      auto src = this->result.srcMan->findById(id);
      src = tmp.srcMan->add(src);
      assert(src);
      this->result.modifiedSrcIds.emplace(src->getSrcId());
    }

    // merge closing src
    tmp.closingSrcIds.merge(this->result.closingSrcIds);
    this->result.closingSrcIds.clear();
    for (auto &id : tmp.closingSrcIds) {
      auto src = this->result.srcMan->findById(id);
      src = tmp.srcMan->add(src);
      assert(src);
      this->result.closingSrcIds.emplace(src->getSrcId());
    }
    this->result.srcMan = std::move(tmp.srcMan);
  }

  AnalyzerParam param = ({
    this->cancelPoint = std::make_shared<CancelPoint>();
    auto ret = this->result.deepCopy();
    this->result.modifiedSrcIds.clear();
    this->result.closingSrcIds.clear();
    DiagnosticEmitter emitter = this->newDiagnosticEmitter(ret.srcMan);
    AnalyzerParam{
        .sysConfig = std::ref(this->sysConfig),
        .logger = this->logger,
        .cancelPoint = this->cancelPoint,
        .ret = std::move(ret),
        .emitter = std::move(emitter),
    };
  });
  this->futureResult =
      this->worker.addTask([p = std::move(param)]() mutable { return doRebuild(std::move(p)); });
  return true;
}

void LSPServer::updateSource(StringRef path, int newVersion, std::string &&newContent) {
  auto src = this->result.srcMan->update(path, newVersion, std::move(newContent));
  if (!src) {
    LOG(LogLevel::ERROR, "reach opened file limit");
    return;
  }
  this->timeout = this->defaultDebounceTime;
  this->result.modifiedSrcIds.emplace(src->getSrcId());
  this->result.closingSrcIds.erase(src->getSrcId());
}

void LSPServer::syncResult() {
  this->tryRebuild();
  if (this->futureResult.valid()) {
    this->result = this->futureResult.get(); // override current result
  }
}

static MarkupKind resolveMarkupKind(const std::vector<MarkupKind> &formats) {
  for (auto &kind : formats) {
    if (kind == MarkupKind::Markdown) {
      return kind;
    }
  }
  return MarkupKind::PlainText;
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
    if (textDocument.hover.hasValue()) {
      if (auto &hover = textDocument.hover.unwrap(); hover.contentFormat.hasValue()) {
        this->markupKind = resolveMarkupKind(hover.contentFormat.unwrap());
      }
    } // FIXME: check client supported semantic token options
  }

  InitializeResult ret;
  ret.capabilities.textDocumentSync = TextDocumentSyncOptions{
      .openClose = true,
      .change = TextDocumentSyncKind::Incremental,
      .willSave = {},
      .willSaveWaitUntil = {},
      .save = {},
  };
  ret.capabilities.definitionProvider = true;
  ret.capabilities.referencesProvider = true;
  ret.capabilities.documentHighlightProvider = true;
  ret.capabilities.documentSymbolProvider = true;
  ret.capabilities.hoverProvider = true;
  ret.capabilities.completionProvider = CompletionOptions{};
  ret.capabilities.completionProvider.unwrap().triggerCharacters =
      std::vector<std::string>{".", "$", "/"};
  ret.capabilities.documentLinkProvider = DocumentLinkOptions{};
  ret.capabilities.semanticTokensProvider = SemanticTokensOptions{};
  ret.capabilities.semanticTokensProvider.unwrap().legend = SemanticTokensLegend::create();
  ret.capabilities.semanticTokensProvider.unwrap().full = true;
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
  if (auto uri = uri::URI::parse(params.textDocument.uri)) {
    if (auto fullPath = resolveURI(*this->result.srcMan, uri); !fullPath.empty()) {
      this->updateSource(fullPath, params.textDocument.version,
                         std::string(params.textDocument.text));
      this->syncResult();
      return;
    }
  }
  LOG(LogLevel::ERROR, "broken uri: %s", uriStr);
}

void LSPServer::didCloseTextDocument(const DidCloseTextDocumentParams &params) {
  if (auto resolved = this->resolveSource(params.textDocument)) {
    auto &src = resolved.asOk();
    this->result.closingSrcIds.emplace(src->getSrcId());
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
  this->updateSource(src->getPath(), params.textDocument.version, std::move(content));
}

Reply<std::vector<Location>> LSPServer::gotoDefinition(const DefinitionParams &params) {
  LOG(LogLevel::INFO, "definition at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  this->syncResult();
  if (auto resolved = this->resolvePosition(params)) {
    return this->gotoDefinitionImpl(resolved.asOk().second);
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<std::vector<Location>> LSPServer::findReference(const ReferenceParams &params) {
  LOG(LogLevel::INFO, "reference at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  this->syncResult();
  if (auto resolved = this->resolvePosition(params)) {
    return this->findReferenceImpl(resolved.asOk().second);
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<std::vector<DocumentHighlight>>
LSPServer::documentHighlight(const DocumentHighlightParams &params) {
  LOG(LogLevel::INFO, "highlight at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  this->syncResult();
  if (auto resolved = this->resolvePosition(params)) {
    return this->documentHighlightImpl(resolved.asOk().second);
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<Union<Hover, std::nullptr_t>> LSPServer::hover(const HoverParams &params) {
  LOG(LogLevel::INFO, "hover at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  this->syncResult();
  if (auto resolved = this->resolvePosition(params)) {
    auto &src = *resolved.asOk().first;
    auto &request = resolved.asOk().second;
    return this->hoverImpl(src, request);
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
    auto newSrc = src.copyAndUpdate(src.getVersion(), std::string(src.getContent().c_str(), pos));
    ModuleArchives copiedArchives = this->result.archives;
    copiedArchives.revert({newSrc->getSrcId()});
    auto copiedSrcMan = this->result.srcMan->copy();
    Analyzer analyzer(this->sysConfig, *copiedSrcMan, copiedArchives);
    return analyzer.complete(*newSrc, this->cmdCompKind, this->cmdArgComp == BinaryFlag::enabled);
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

template <typename T, typename Func>
static void getOrShowError(LoggerBase &logger, const Union<T, JSON> &field, const char *fieldName,
                           Func &&callback) {
  if (!field.hasValue()) {
    return;
  }
  if (is<T>(field)) {
    callback(get<T>(field));
  } else if (is<JSON>(field)) {
    logger(LogLevel::WARNING, "field: `%s' is invalid type", fieldName);
  }
}

void LSPServer::didChangeConfiguration(const DidChangeConfigurationParams &params) {
  getOrShowError(
      this->logger, params.settings, "settings", [&](const ConfigSettingWrapper &wrapper) {
        getOrShowError(this->logger, wrapper.ydshd, "ydshd", [&](const ConfigSetting &setting) {
          getOrShowError(this->logger, setting.commandCompletion, "commandCompletion",
                         [&](CmdCompKind kind) { this->cmdCompKind = kind; });
          getOrShowError(this->logger, setting.commandArgumentCompletion,
                         "commandArgumentCompletion",
                         [&](BinaryFlag enabled) { this->cmdArgComp = enabled; });
          getOrShowError(this->logger, setting.logLevel, "logLevel",
                         [&](LogLevel level) { this->logger.get().setSeverity(level); });
          getOrShowError(this->logger, setting.semanticHighlight, "semanticHighlight",
                         [&](BinaryFlag enabled) { this->semanticHighlight = enabled; });
        });
      });
}

Reply<Union<SemanticTokens, std::nullptr_t>>
LSPServer::semanticToken(const SemanticTokensParams &params) {
  LOG(LogLevel::INFO, "semantic token at: %s", params.textDocument.uri.c_str());
  if (auto resolved = this->resolveSource(params.textDocument)) {
    Union<SemanticTokens, std::nullptr_t> ret = nullptr;
    if (this->semanticHighlight == BinaryFlag::enabled) {
      auto &src = *resolved.asOk();
      SemanticTokenEmitter emitter(this->encoder, src);
      emitter.tokenizeAndEmit();
      ret = std::move(emitter).take();
    }
    return ret;
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<std::vector<DocumentLink>> LSPServer::documentLink(const DocumentLinkParams &params) {
  LOG(LogLevel::INFO, "document link at: %s", params.textDocument.uri.c_str());
  this->syncResult();
  if (auto resolved = this->resolveSource(params.textDocument)) {
    std::vector<DocumentLink> ret;
    if (auto index = this->result.indexes.find(resolved.asOk()->getSrcId())) {
      for (auto &e : index->getLinks()) {
        auto range = toRange(*resolved.asOk(), e.first.getToken());
        assert(range.hasValue());
        auto src = this->result.srcMan->findById(e.first.getModId());
        assert(src);
        ret.push_back(DocumentLink{
            .range = range.unwrap(),
            .target = toURI(*this->result.srcMan, src->getPath()).toString(),
            .tooltip = e.second,
        });
      }
    }
    return ret;
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<std::vector<DocumentSymbol>> LSPServer::documentSymbol(const DocumentSymbolParams &params) {
  LOG(LogLevel::INFO, "document symbol at: %s", params.textDocument.uri.c_str());
  this->syncResult();
  if (auto resolved = this->resolveSource(params.textDocument)) {
    std::vector<DocumentSymbol> ret;
    if (auto index = this->result.indexes.find(resolved.asOk()->getSrcId())) {
      for (auto &decl : index->getDecls()) {
        if (decl.getModId() == 0 || hasFlag(decl.getAttr(), DeclSymbol::Attr::BUILTIN) ||
            !hasFlag(decl.getAttr(), DeclSymbol::Attr::GLOBAL)) {
          continue;
        }

        auto selectionRange = toRange(*resolved.asOk(), decl.getToken());
        auto range = toRange(*resolved.asOk(), decl.getBody());
        auto name = DeclSymbol::demangle(decl.getKind(), decl.getAttr(), decl.getMangledName());
        assert(selectionRange.hasValue());
        assert(range.hasValue());
        ret.push_back(DocumentSymbol{
            .name = std::move(name),
            .detail = {}, // FIXME:
            .kind = toSymbolKind(decl.getKind()),
            .range = range.unwrap(),
            .selectionRange = selectionRange.unwrap(),
        });
      }
    }
    return ret;
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

} // namespace ydsh::lsp