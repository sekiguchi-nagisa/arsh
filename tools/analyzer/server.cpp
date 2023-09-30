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
#include "extra_checker.h"
#include "indexer.h"
#include "rename.h"
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
  this->bind("textDocument/signatureHelp", &LSPServer::signatureHelp);
  this->bind("textDocument/semanticTokens/full", &LSPServer::semanticToken);
  this->bind("textDocument/rename", &LSPServer::rename);
  this->bind("textDocument/prepareRename", &LSPServer::prepareRename);
  this->bind("workspace/didChangeConfiguration", &LSPServer::didChangeConfiguration);
}

void LSPServer::run() {
  while (true) {
    auto s = this->transport.dispatch(*this, this->timeout);
    if (s == Transport::Status::TIMEOUT) {
      LOG(LogLevel::INFO, "tryRebuild due to timeout...");
      if (!this->tryRebuild()) {
        LOG(LogLevel::INFO, "rebuild is not performed");
      }
    } else if (s == Transport::Status::ERROR) {
      break;
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
  auto fullPath = this->result.srcMan->resolveURI(uri);
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
    if (isBuiltinMod(ret.decl.getModId()) ||
        hasFlag(ret.decl.getAttr(), DeclSymbol::Attr::BUILTIN)) {
      /**
       * ignore builtin module symbols and builtin type fields/methods
       */
      return;
    }
    auto s = this->result.srcMan->findById(ret.decl.getModId());
    assert(s);
    auto range = s->toRange(ret.decl.getToken());
    assert(range.hasValue());
    values.push_back(Location{.uri = this->result.srcMan->toURI(s->getPath()).toString(),
                              .range = range.unwrap()});
  });
  return values;
}

std::vector<Location> LSPServer::findReferenceImpl(const SymbolRequest &request) const {
  std::vector<Location> values;
  findAllReferences(this->result.indexes, request, [&](const FindRefsResult &ret) {
    if (auto s = this->result.srcMan->findById(ret.symbol.getModId())) {
      auto range = s->toRange(ret.symbol.getToken());
      assert(range.hasValue());
      values.push_back(Location{.uri = this->result.srcMan->toURI(s->getPath()).toString(),
                                .range = range.unwrap()});
    }
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
          auto range = src->toRange(value.symbol.getToken());
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
        .range = src.toRange(value.request.getToken()),
    };
  });
  return ret;
}

Result<WorkspaceEdit, std::string> LSPServer::renameImpl(const SymbolRequest &request,
                                                         const std::string &newName) const {
  std::unordered_map<unsigned int, std::vector<TextEdit>> changes;
  auto status = RenameValidationStatus::DO_NOTHING;
  if (this->renameSupport == BinaryFlag::enabled) {
    status = validateRename(this->result.indexes, request, newName, [&](const RenameResult &ret) {
      if (ret) {
        auto &target = ret.asOk();
        changes[toUnderlying(target.symbol.getModId())].push_back(
            target.toTextEdit(*this->result.srcMan));
      }
    });
  }
  switch (status) {
  case RenameValidationStatus::CAN_RENAME:
  case RenameValidationStatus::DO_NOTHING:
    break;
  case RenameValidationStatus::INVALID_SYMBOL:
    return Err(std::string("rename target must be symbol"));
  case RenameValidationStatus::INVALID_NAME: {
    std::string v = "invalid new name: ";
    v += newName;
    return Err(std::move(v));
  }
  case RenameValidationStatus::BUILTIN:
    return Err(std::string("cannot perform rename since builtin symbol"));
  case RenameValidationStatus::NAME_CONFLICT: {
    std::string v = newName;
    v += " is already exists in current scope";
    return Err(std::move(v));
  }
  }

  WorkspaceEdit workspaceEdit;
  if (!changes.empty()) {
    workspaceEdit.init();
  }
  for (auto &e : changes) {
    auto src = this->result.srcMan->findById(static_cast<ModId>(e.first));
    assert(src);
    auto uri = this->result.srcMan->toURI(src->getPath());
    workspaceEdit.insert(uri, std::move(e.second));
  }
  return Ok(std::move(workspaceEdit));
}

DiagnosticEmitter LSPServer::newDiagnosticEmitter(std::shared_ptr<SourceManager> srcMan) {
  return {std::move(srcMan),
          [&](PublishDiagnosticsParams &&params) { this->publishDiagnostics(std::move(params)); },
          this->diagVersionSupport};
}

struct AnalyzerTask {
  std::reference_wrapper<const SysConfig> sysConfig;
  std::reference_wrapper<LoggerBase> logger;
  std::shared_ptr<CancelPoint> cancelPoint;
  AnalyzerResult ret;
  DiagnosticEmitter emitter;

  AnalyzerResult doRebuild();
};

AnalyzerResult AnalyzerTask::doRebuild() {
  LOG(LogLevel::INFO, "rebuild started");

  // prepare
  {
    auto tmp(this->ret.modifiedSrcIds);
    this->ret.archives.revert(std::move(tmp));
  }

  AnalyzerAction action;
  SymbolIndexer indexer(this->sysConfig, this->ret.indexes);
  ExtraChecker extraChecker(this->emitter);
  MultipleNodePass passes;
  passes.add(makeObserver(extraChecker));
  passes.add(makeObserver(indexer));
  action.emitter.reset(&this->emitter);
  action.pass = makeObserver(passes);

  // rebuild
  Analyzer analyzer(this->sysConfig, *this->ret.srcMan, this->ret.archives, this->cancelPoint,
                    makeObserver(this->logger.get()));
  for (auto &e : this->ret.modifiedSrcIds) {
    if (this->ret.archives.find(e)) {
      continue;
    }
    auto src = this->ret.srcMan->findById(e);
    assert(src);
    LOG(LogLevel::INFO, "analyze modified src: id=%d, version=%d, path=%s",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    auto r = analyzer.analyze(*src, action);
    LOG(LogLevel::INFO, "analyze %s: id=%d, version=%d, path=%s", r ? "finished" : "canceled",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    if (!r) {
      break;
    }
  }

  while (!this->cancelPoint->isCanceled()) {
    auto targetId = this->ret.archives.getFirstRevertedModId();
    if (!targetId.hasValue()) {
      break;
    }
    auto src = this->ret.srcMan->findById(targetId.unwrap());
    assert(src);
    LOG(LogLevel::INFO, "analyze revered src: id=%d, version=%d, path=%s",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    auto r = analyzer.analyze(*src, action);
    LOG(LogLevel::INFO, "analyze %s: id=%d, version=%d, path=%s", r ? "finished" : "canceled",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    if (!r) {
      break;
    }
  }

  if (!this->cancelPoint->isCanceled()) {
    // close
    for (auto &id : this->ret.closingSrcIds) {
      if (this->ret.archives.removeIfUnused(id)) {
        auto src = this->ret.srcMan->findById(id);
        LOG(LogLevel::INFO, "close textDocument: %s", src->getPath().c_str());
        this->ret.indexes.remove(id);
      }
    }
    this->ret.modifiedSrcIds.clear();
    this->ret.closingSrcIds.clear();
  }
  LOG(LogLevel::INFO, "rebuild finished");
  return std::move(this->ret);
}

bool LSPServer::tryRebuild() {
  if (this->result.modifiedSrcIds.empty()) {
    this->timeout = -1;
    return false;
  }

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

  AnalyzerTask param = ({
    this->cancelPoint = std::make_shared<CancelPoint>();
    auto ret = this->result.deepCopy();
    this->result.modifiedSrcIds.clear();
    this->result.closingSrcIds.clear();
    DiagnosticEmitter emitter = this->newDiagnosticEmitter(ret.srcMan);
    AnalyzerTask{
        .sysConfig = std::ref(this->sysConfig),
        .logger = this->logger,
        .cancelPoint = this->cancelPoint,
        .ret = std::move(ret),
        .emitter = std::move(emitter),
    };
  });
  this->futureResult =
      this->worker.addTask([p = std::move(param)]() mutable { return p.doRebuild(); });
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
  LOG(LogLevel::INFO, "tryRebuild...");
  if (!this->tryRebuild()) {
    LOG(LogLevel::INFO, "rebuild is not performed");
  }
  if (this->futureResult.valid()) {
    this->result = this->futureResult.get(); // override current result
  }
}

std::pair<std::shared_ptr<SourceManager>, ModuleArchives> LSPServer::snapshot() const {
  return {this->result.srcMan->copy(), ModuleArchives(this->result.archives)};
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
    if (textDocument.completion.hasValue()) {
      if (auto &completion = textDocument.completion.unwrap();
          completion.completionItem.hasValue()) {
        auto &compItem = completion.completionItem.unwrap();
        if (compItem.labelDetailsSupport.hasValue()) {
          this->labelDetailSupport = compItem.labelDetailsSupport.unwrap();
        }
      }
    }
  }

  InitializeResult ret;
  ret.capabilities.signatureHelpProvider.triggerCharacters = {"(", ","};
  ret.capabilities.completionProvider.triggerCharacters = {".", "$", "/"};
  ret.capabilities.semanticTokensProvider.legend = SemanticTokensLegend::create();
  ret.capabilities.semanticTokensProvider.full = true;
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
    if (auto fullPath = this->result.srcMan->resolveURI(uri); !fullPath.empty()) {
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
    auto [copiedSrcMan, copiedArchives] = this->snapshot();
    copiedArchives.revert({src.getSrcId()});
    Analyzer analyzer(this->sysConfig, *copiedSrcMan, copiedArchives);
    Analyzer::ExtraCompOp extraCompOp{};
    if (this->cmdArgComp == BinaryFlag::enabled) {
      setFlag(extraCompOp, Analyzer::ExtraCompOp::CMD_ARG_COMP);
    }
    if (this->labelDetailSupport) {
      setFlag(extraCompOp, Analyzer::ExtraCompOp::SIGNATURE);
    }
    return analyzer.complete(src, pos, this->cmdCompKind, extraCompOp);
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
          getOrShowError(this->logger, setting.rename, "rename",
                         [&](BinaryFlag enabled) { this->renameSupport = enabled; });
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
        auto range = resolved.asOk()->toRange(e.first.getToken());
        assert(range.hasValue());
        auto src = this->result.srcMan->findById(e.first.getModId());
        assert(src);
        ret.push_back(DocumentLink{
            .range = range.unwrap(),
            .target = this->result.srcMan->toURI(src->getPath()).toString(),
            .tooltip = e.second.getPathName(),
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
        if (isBuiltinMod(decl.getModId()) || hasFlag(decl.getAttr(), DeclSymbol::Attr::BUILTIN) ||
            !hasFlag(decl.getAttr(), DeclSymbol::Attr::GLOBAL)) {
          continue;
        }

        auto selectionRange = resolved.asOk()->toRange(decl.getToken());
        auto range = resolved.asOk()->toRange(decl.getBody());
        auto name = decl.toDemangledName();
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

Reply<Union<SignatureHelp, std::nullptr_t>>
LSPServer::signatureHelp(const SignatureHelpParams &params) {
  LOG(LogLevel::INFO, "signature help at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  if (auto resolved = this->resolvePosition(params)) {
    auto &src = *resolved.asOk().first;
    auto pos = resolved.asOk().second.pos;
    auto [copiedSrcMan, copiedArchives] = this->snapshot();
    copiedArchives.revert({src.getSrcId()});
    Analyzer analyzer(this->sysConfig, *copiedSrcMan, copiedArchives);
    Union<SignatureHelp, std::nullptr_t> ret = nullptr;
    auto info = analyzer.collectSignature(src, pos);
    if (info.hasValue()) {
      SignatureHelp value;
      value.activeParameter = info.unwrap().activeParameter;
      value.activeSignature = 0;
      value.signatures.push_back(std::move(info.unwrap()));
      ret = std::move(value);
    }
    return ret;
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<WorkspaceEdit> LSPServer::rename(const RenameParams &params) {
  LOG(LogLevel::INFO, "rename at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  this->syncResult();
  if (auto resolved = this->resolvePosition(params)) {
    auto &request = resolved.asOk().second;
    auto ret = this->renameImpl(request, params.newName);
    if (ret) {
      return std::move(ret).take();
    } else {
      return newError(LSPErrorCode::RequestFailed, std::move(ret).takeError());
    }
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

Reply<Union<PrepareRename, std::nullptr_t>>
LSPServer::prepareRename(const PrepareRenameParams &params) {
  LOG(LogLevel::INFO, "prepare rename at: %s:%s", params.textDocument.uri.c_str(),
      params.position.toString().c_str());
  this->syncResult();
  if (auto resolved = this->resolvePosition(params)) {
    auto renameLocation = resolveRenameLocation(this->result.indexes, resolved.asOk().second);
    Union<PrepareRename, std::nullptr_t> ret = nullptr;
    if (renameLocation.hasValue()) {
      Token token = renameLocation.unwrap().request.getToken();
      auto &src = *resolved.asOk().first;
      if (auto range = src.toRange(token); range.hasValue()) {
        ret = PrepareRename{
            .range = range.unwrap(),
            .placeholder = src.toStrRef(src.stripAppliedNameSigil(token)).toString(),
        };
      }
    }
    return ret;
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

} // namespace ydsh::lsp