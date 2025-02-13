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

namespace arsh::lsp {

// #######################
// ##     LSPServer     ##
// #######################

#define LLOG(logger, L, ...)                                                                       \
  do {                                                                                             \
    (logger).enabled(L) && (logger)(L, __VA_ARGS__);                                               \
  } while (false)

#define LOG(L, ...) LLOG(this->logger.get(), L, __VA_ARGS__)

void LSPServer::onCall(Transport &, Request &&req) {
  if (!this->init && req.method != "initialize") {
    LOG(LogLevel::ERROR, "must be initialized");
    this->transport.reply(std::move(req.id),
                          Error(LSPErrorCode::ServerNotInitialized, "server not initialized!!"));
    return;
  }
  auto context = this->contextMan.addNew(req.id);
  this->rpcHandlerWorker.addNoreturnTask(
      [this, ctx = std::move(context), q = std::move(req)]() mutable {
        if (ctx->getCancelPoint()->isCanceled()) {
          LOG(LogLevel::INFO, "do cancel: request id=%s", toStringCancelId(q.id).c_str());
          this->transport.reply(std::move(q.id.unwrap()),
                                Error(LSPErrorCode::RequestCancelled, "cancelled"));
        } else {
          this->currentCtx = std::move(ctx);
          Handler::onCall(this->transport, std::move(q));
        }
        this->currentCtx = nullptr;
      });
}

void LSPServer::onNotify(Request &&req) {
  if (req.method == "$/cancelRequest") {
    Handler::onNotify(std::move(req)); // cancelRequest bypass rpcHandlerWorker
    return;
  }
  this->rpcHandlerWorker.addNoreturnTask(
      [this, q = std::move(req)]() mutable { Handler::onNotify(std::move(q)); });
}

void LSPServer::onResponse(Response &&res) {
  this->rpcHandlerWorker.addNoreturnTask(
      [this, r = std::move(res)]() mutable { Handler::onResponse(std::move(r)); });
}

void LSPServer::bindAll() {
  this->bind("shutdown", &LSPServer::shutdown);
  this->bind("exit", &LSPServer::exit);
  this->bind("initialize", &LSPServer::initialize);
  this->bind("initialized", &LSPServer::initialized);
  this->bind("$/cancelRequest", &LSPServer::tryCancel);
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
    auto s = this->dispatch(this->transport, this->timeout.load());
    if (s == Status::TIMEOUT) {
      LOG(LogLevel::INFO, "tryRebuild due to timeout...");
      if (!this->tryRebuild()) {
        LOG(LogLevel::INFO, "rebuild is not performed");
      }
    } else if (s == Status::ERROR) {
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
    if (isBuiltinMod(ret.decl.getModId()) || ret.decl.has(DeclSymbol::Attr::BUILTIN)) {
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
  findAllReferences(this->result.indexes, req, [&](const FindRefsResult &value) {
    if (value.symbol.getModId() == request.modId) {
      auto range = src->toRange(value.symbol.getToken());
      assert(range.hasValue());
      values.push_back(DocumentHighlight{
          .range = range.unwrap(),
          .kind = DocumentHighlightKind::Text,
      });
    }
  });
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
                .value = generateHoverContent(*this->result.srcMan, this->result.indexes, src,
                                              value, this->markupKind == MarkupKind::Markdown),
            },
        .range = src.toRange(value.request.getToken()),
    };
  });
  return ret;
}

Result<WorkspaceEdit, std::string> LSPServer::renameImpl(const SymbolRequest &request,
                                                         const std::string &newName) const {
  std::unordered_map<ModId, std::vector<TextEdit>> changes;
  auto status = RenameValidationStatus::DO_NOTHING;
  bool publicToPrivate = false;
  if (this->renameSupport == BinaryFlag::enabled) {
    status = validateRename(
        this->result.indexes, request, newName,
        [&](const DeclSymbol &decl, const RenameResult &ret) {
          if (ret) {
            auto &target = ret.asOk();
            changes[target.symbol.getModId()].push_back(target.toTextEdit(*this->result.srcMan));
            if (target.publicToPrivate && decl.getModId() != target.symbol.getModId()) {
              publicToPrivate = true;
            }
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
  case RenameValidationStatus::KEYWORD: {
    std::string v = newName;
    v += " conflict with keyword";
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
  if (hasFlag(this->supportedCapability, SupportedCapability::CHANGE_ANNOTATION) &&
      hasFlag(this->supportedCapability, SupportedCapability::RENAME_CHANGE_ANNOTATION)) {
    if (!changes.empty()) {
      workspaceEdit.initTextDocumentEdit();
    }
    if (publicToPrivate) {
      workspaceEdit.initChangeAnnotations();
      workspaceEdit.changeAnnotations.unwrap().emplace(
          "public-to-private", ChangeAnnotation{
                                   .label = "",
                                   .needsConfirmation = true,
                                   .description = "rename public symbol to private",
                               });
    }
    for (auto &[modId, edits] : changes) {
      auto src = this->result.srcMan->findById(modId);
      assert(src);
      TextDocumentEdit edit;
      edit.textDocument.version = nullptr;
      edit.textDocument.uri = this->result.srcMan->toURI(src->getPath()).toString();
      if (!src->has(SourceAttr::FROM_DISK)) {
        edit.textDocument.version = src->getVersion();
      }
      for (auto &e : edits) {
        if (publicToPrivate) {
          edit.edits.emplace_back(AnnotatedTextEdit::from(std::move(e), "public-to-private"));
        } else {
          edit.edits.emplace_back(std::move(e));
        }
      }
      workspaceEdit.documentChanges.unwrap().push_back(std::move(edit));
    }
  } else {
    if (!changes.empty()) {
      workspaceEdit.initTextEdit();
    }
    for (auto &e : changes) {
      auto src = this->result.srcMan->findById(e.first);
      assert(src);
      auto uri = this->result.srcMan->toURI(src->getPath());
      workspaceEdit.insert(uri, std::move(e.second));
    }
  }
  return Ok(std::move(workspaceEdit));
}

DiagnosticEmitter LSPServer::newDiagnosticEmitter(std::shared_ptr<SourceManager> srcMan) {
  return {std::move(srcMan),
          [&](PublishDiagnosticsParams &&params) { this->publishDiagnostics(std::move(params)); },
          hasFlag(this->supportedCapability, SupportedCapability::DIAG_VERSION)};
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
  indexer.setLogger(makeObserver(this->logger.get()));
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
    auto r = analyzer.analyze(src, action);
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
    auto r = analyzer.analyze(src, action);
    LOG(LogLevel::INFO, "analyze %s: id=%d, version=%d, path=%s", r ? "finished" : "canceled",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    if (!r) {
      break;
    }
  }
  if (this->cancelPoint->isCanceled()) {
    LOG(LogLevel::INFO, "rebuild canceled");
  } else {
    this->ret.modifiedSrcIds.clear();
    LOG(LogLevel::INFO, "rebuild finished");
  }
  return std::move(this->ret);
}

bool LSPServer::tryRebuild() {
  std::unique_lock lock(this->rebuildMutex);
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
    this->result.srcMan = std::move(tmp.srcMan);
  }

  AnalyzerTask param = ({
    this->cancelPoint = std::make_shared<CancelPoint>();
    auto ret = this->result.deepCopy();
    this->result.modifiedSrcIds.clear();
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
      this->analyzerWorker.addTask([p = std::move(param)]() mutable { return p.doRebuild(); });
  return true;
}

void LSPServer::updateSource(StringRef path, int newVersion, std::string &&newContent) {
  auto src = this->result.srcMan->update(path, newVersion, std::move(newContent));
  if (!src) {
    LOG(LogLevel::ERROR, "reach opened file limit");
    return;
  }
  {
    std::unique_lock lock(this->rebuildMutex);
    this->timeout = this->defaultDebounceTime;
    this->result.modifiedSrcIds.emplace(src->getSrcId());
  }
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
  auto semanticTokensLegend = SemanticTokensLegend::create();
  if (auto t = params.trace; t.hasValue()) {
    this->traceSetting = t.unwrap();
  }
  if (params.capabilities.textDocument.hasValue()) {
    auto &textDocument = params.capabilities.textDocument.unwrap();
    if (textDocument.publishDiagnostics.hasValue()) {
      auto &diag = textDocument.publishDiagnostics.unwrap();
      if (diag.versionSupport.hasValue() && diag.versionSupport.unwrap()) {
        setFlag(this->supportedCapability, SupportedCapability::DIAG_VERSION);
      }
    }
    if (textDocument.hover.hasValue()) {
      if (auto &hover = textDocument.hover.unwrap(); hover.contentFormat.hasValue()) {
        this->markupKind = resolveMarkupKind(hover.contentFormat.unwrap());
      }
    }
    if (textDocument.semanticTokens.hasValue()) {
      auto &semanticTokens = textDocument.semanticTokens.unwrap();
      if (semanticTokens.dynamicRegistration.hasValue() &&
          semanticTokens.dynamicRegistration.unwrap()) {
        setFlag(this->supportedCapability, SupportedCapability::SEMANTIC_TOKEN_REGISTRATION);
      }
      // fitLegendToClient(semanticTokensLegend, semanticTokens.tokenTypes); // not check client
    }
    if (textDocument.completion.hasValue()) {
      if (auto &completion = textDocument.completion.unwrap();
          completion.completionItem.hasValue()) {
        auto &compItem = completion.completionItem.unwrap();
        if (compItem.labelDetailsSupport.hasValue() && compItem.labelDetailsSupport.unwrap()) {
          setFlag(this->supportedCapability, SupportedCapability::LABEL_DETAIL);
        }
      }
    }
    if (textDocument.rename.hasValue()) {
      auto &rename = textDocument.rename.unwrap();
      if (rename.prepareSupport.hasValue() && rename.prepareSupport.unwrap()) {
        setFlag(this->supportedCapability, SupportedCapability::PREPARE_RENAME);
      }
      if (rename.honorsChangeAnnotations.hasValue() && rename.honorsChangeAnnotations.unwrap()) {
        setFlag(this->supportedCapability, SupportedCapability::RENAME_CHANGE_ANNOTATION);
      }
    }
  }
  if (params.capabilities.workspace.hasValue()) {
    auto &workspace = params.capabilities.workspace.unwrap();
    if (workspace.configuration.hasValue() && workspace.configuration.unwrap()) {
      setFlag(this->supportedCapability, SupportedCapability::WORKSPACE_CONFIG);
    }
    if (workspace.workspaceEdit.hasValue()) {
      auto &workspaceEdit = workspace.workspaceEdit.unwrap();
      if (workspaceEdit.documentChanges.hasValue() && workspaceEdit.documentChanges.unwrap()) {
        setFlag(this->supportedCapability, SupportedCapability::VERSIONED_DOCUMENT_CHANGE);
      }
      if (workspaceEdit.changeAnnotationSupport.hasValue()) {
        setFlag(this->supportedCapability, SupportedCapability::CHANGE_ANNOTATION);
      }
    }
  }
  this->encoder = SemanticTokenEncoder(std::move(semanticTokensLegend));

  // generate server capability
  InitializeResult ret;
  ret.capabilities.completionProvider.completionItem.labelDetailsSupport =
      hasFlag(this->supportedCapability, SupportedCapability::LABEL_DETAIL);
  ret.capabilities.renameProvider.prepareProvider =
      hasFlag(this->supportedCapability, SupportedCapability::PREPARE_RENAME);
  if (hasFlag(this->supportedCapability, SupportedCapability::SEMANTIC_TOKEN_REGISTRATION)) {
    auto options = SemanticTokensRegistrationOptions::createStatic(this->idGenerator("reg"),
                                                                   this->encoder.getLegend());
    this->registrationMap.registerCapability(options);
    ret.capabilities.semanticTokensProvider = std::move(options);
  } else {
    ret.capabilities.semanticTokensProvider =
        SemanticTokensOptions::create(this->encoder.getLegend());
  }
  return ret;
}

static constexpr const char *configSections[] = {
#define GEN_TABLE(N, T) "arshd." #N,
    EACH_CONFIG_SETTING(GEN_TABLE)
#undef GEN_TABLE
};

template <typename T, typename Func>
static void getOrShowError(LoggerBase &logger, const Union<T, JSON> &field, const char *fieldName,
                           Func &&callback) {
  if (!field.hasValue()) {
    return;
  }
  if (is<T>(field)) {
    callback(get<T>(field));
  } else if (is<JSON>(field)) {
    LLOG(logger, LogLevel::WARNING, "field: `%s' is invalid type", fieldName);
  }
}

void LSPServer::loadConfigSetting(const ConfigSetting &setting) {
  RegistrationParam registrationParam;
  UnregistrationParam unregistrationParam;

  getOrShowError(this->logger, setting.fileNameCompletion, "fileNameCompletion",
                 [&](BinaryFlag enabled) { this->fileNameComp = enabled; });
  getOrShowError(this->logger, setting.logLevel, "logLevel",
                 [&](LogLevel level) { this->logger.get().setSeverity(level); });
  getOrShowError(
      this->logger, setting.semanticHighlight, "semanticHighlight", [&](BinaryFlag enabled) {
        if (hasFlag(this->supportedCapability, SupportedCapability::SEMANTIC_TOKEN_REGISTRATION) &&
            this->semanticHighlight != enabled) {
          if (enabled == BinaryFlag::enabled) {
            auto registration = this->registrationMap.registrSemanticTokensCapability(
                this->idGenerator, this->encoder.getLegend());
            if (registration) {
              registrationParam.registrations.push_back(std::move(registration));
            }
          } else {
            auto unregistration = this->registrationMap.unregisterCapability(
                RegistrationMap::Capability::SEMANTIC_TOKENS);
            if (unregistration) {
              unregistrationParam.unregistrations.push_back(std::move(unregistration));
            }
          }
        }
        this->semanticHighlight = enabled;
      });
  getOrShowError(this->logger, setting.rename, "rename",
                 [&](BinaryFlag enabled) { this->renameSupport = enabled; });

  if (!registrationParam.registrations.empty()) {
    this->registerCapability(std::move(registrationParam));
  }
  if (!unregistrationParam.unregistrations.empty()) {
    this->unregisterCapability(std::move(unregistrationParam));
  }
}

static ConfigSetting deserializeConfigSetting(LoggerBase &logger, const std::vector<JSON> &values) {
  ConfigSetting setting;
  const unsigned int size = std::size(configSections);
  if (values.size() != size) {
    LLOG(logger, LogLevel::ERROR,
         "broken response of 'workspace/configuration', expect: %d items, but actual is: %d", size,
         static_cast<unsigned int>(values.size()));
  } else {
    if (logger.enabled(LogLevel::DEBUG)) {
      json::Object map;
      for (unsigned int i = 0; i < size; i++) {
        map[configSections[i]] = values[i];
      }
      logger(LogLevel::DEBUG, "response of 'workspace/configuration':\n%s",
             JSON(std::move(map)).serialize(2).c_str());
    }
    JSON json = ({
      json::Object map;
      for (unsigned int i = 0; i < size; i++) {
        StringRef key = configSections[i];
        auto r = key.find('.');
        assert(r != StringRef::npos);
        key = key.substr(r + 1);
        map[key.toString()] = values[i];
      }
      std::forward<decltype(map)>(map);
    });
    JSONDeserializer deserializer(std::move(json));
    deserializer(setting);
  }
  return setting;
}

void LSPServer::initialized(const InitializedParams &) {
  LOG(LogLevel::INFO, "server initialized!!");

  if (hasFlag(this->supportedCapability, SupportedCapability::WORKSPACE_CONFIG)) {
    ConfigurationParams params;
    for (auto &c : configSections) {
      params.items.push_back({ConfigurationItem{.section = c}});
    }
    this->call<std::vector<JSON>>(
        "workspace/configuration", std::move(params),
        [&](const std::vector<JSON> &ret) {
          auto setting = deserializeConfigSetting(this->logger.get(), ret);
          this->loadConfigSetting(setting);
        },
        [&](const Error &error) {
          LOG(LogLevel::ERROR, "'workspace/configuration' failed, %s", error.toString().c_str());
        });
  }
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

// only called from main thread (must be thread safe)
void LSPServer::tryCancel(const CancelParams &param) {
  auto id = cancelIdToJSON(param.id);
  LOG(LogLevel::INFO, "try cancel: request id=%s", toStringCancelId(id).c_str());
  this->contextMan.cancel(id);
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
  LOG(LogLevel::INFO, "try close textDocument: %s", params.textDocument.uri.c_str());
  this->syncResult(); // always synchronize state, before actual close operation
  if (auto resolved = this->resolveSource(params.textDocument)) {
    auto &src = resolved.asOk();
    /**
     * only close unused module.
     * remove archive and index, but source is still existing
     */
    if (this->result.archives.removeIfUnused(src->getSrcId())) {
      LOG(LogLevel::INFO, "do close textDocument: %s", src->getPath().c_str());
      this->result.indexes.remove(src->getSrcId());
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
    auto &src = resolved.asOk().first;
    auto pos = resolved.asOk().second.pos;
    auto [copiedSrcMan, copiedArchives] = this->snapshot();
    copiedArchives.revert({src->getSrcId()});
    Analyzer analyzer(this->sysConfig, *copiedSrcMan, copiedArchives);
    Analyzer::ExtraCompOp extraCompOp{};
    if (this->fileNameComp == BinaryFlag::enabled) {
      setFlag(extraCompOp, Analyzer::ExtraCompOp::FILE_NAME);
    }
    if (hasFlag(this->supportedCapability, SupportedCapability::LABEL_DETAIL)) {
      setFlag(extraCompOp, Analyzer::ExtraCompOp::SIGNATURE);
    }
    return analyzer.complete(src, pos, extraCompOp);
  } else {
    return newError(ErrorCode::InvalidParams, std::string(resolved.asErr().get()));
  }
}

void LSPServer::didChangeConfiguration(const DidChangeConfigurationParams &params) {
  getOrShowError(
      this->logger, params.settings, "settings", [&](const ConfigSettingWrapper &wrapper) {
        getOrShowError(this->logger, wrapper.arshd, "arshd",
                       [&](const ConfigSetting &setting) { this->loadConfigSetting(setting); });
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
        if (isBuiltinMod(decl.getModId()) || decl.has(DeclSymbol::Attr::BUILTIN) ||
            !decl.has(DeclSymbol::Attr::GLOBAL)) {
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
    auto &src = resolved.asOk().first;
    auto pos = resolved.asOk().second.pos;
    auto [copiedSrcMan, copiedArchives] = this->snapshot();
    copiedArchives.revert({src->getSrcId()});
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

} // namespace arsh::lsp