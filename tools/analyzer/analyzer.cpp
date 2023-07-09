/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include <binder.h>
#include <complete.h>
#include <embed.h>
#include <misc/files.h>
#include <misc/format.hpp>

#include "analyzer.h"
#include "symbol.h"

namespace ydsh::lsp {

// #############################
// ##     AnalyzerContext     ##
// #############################

static void consumeAllInput(FrontEnd &frontEnd) {
  while (frontEnd) {
    if (!frontEnd()) {
      break;
    }
  }
}

struct EmptyConsumer {
  void operator()(const Handle &, int64_t) {}

  void operator()(const Handle &, const std::string &) {}

  void operator()(const Handle &, FILE *) {}

  void operator()(const Handle &, const DSType &) {}
};

static const ModType &createBuiltin(const SysConfig &config, TypePool &pool,
                                    unsigned int &gvarCount) {
  unsigned int modIndex = gvarCount++;
  auto builtin = NameScopePtr::create(gvarCount, modIndex, 0);
  EmptyConsumer emptyConsumer;
  bindBuiltins(emptyConsumer, config, pool, *builtin);

  ModuleLoader loader(config); // dummy
  const char *embed = embed_script;
  auto lexer = LexerPtr::create("(builtin)", ByteBuffer(embed, embed + strlen(embed)), getCWD());
  DefaultModuleProvider provider(loader, pool, builtin, std::make_unique<CancelToken>());
  FrontEnd frontEnd(provider, std::move(lexer));
  consumeAllInput(frontEnd);
  gvarCount += 1; // reserve module object entry (root)
  return builtin->toModType(pool);
}

AnalyzerContext::AnalyzerContext(const SysConfig &config, const Source &src)
    : pool(std::make_shared<TypePool>()), version(src.getVersion()) {
  auto &builtin = createBuiltin(config, this->getPool(), this->gvarCount);
  unsigned int modIndex = this->gvarCount++;
  this->scope = NameScopePtr::create(std::ref(this->gvarCount), modIndex, src.getSrcId());
  this->scope->importForeignHandles(this->getPool(), builtin, ImportedModKind::GLOBAL);
  this->typeDiscardPoint = this->getPool().getDiscardPoint();
}

ModuleArchivePtr AnalyzerContext::buildArchive(ModuleArchives &archives) && {
  // pack handles
  auto &modType = this->getScope()->toModType(this->getPool());
  Archiver archiver(this->getPool(), this->typeDiscardPoint.typeIdOffset);
  std::vector<Archive> handles;
  for (auto &e : modType.getHandleMap()) {
    handles.push_back(archiver.pack(e.first, *e.second));
  }

  // resolve imported modules
  std::vector<std::pair<ImportedModKind, ModuleArchivePtr>> imported;
  unsigned int size = modType.getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto e = modType.getChildAt(i);
    auto &type = cast<ModType>(this->getPool().get(e.typeId()));
    if (type.getModId() == 0) { // skip builtin module
      continue;
    }
    auto archive = archives.find(type.getModId());
    assert(archive);
    imported.emplace_back(e.kind(), std::move(archive));
  }

  auto archive =
      std::make_shared<ModuleArchive>(modType.getModId(), this->getVersion(), modType.getAttr(),
                                      std::move(handles), std::move(imported));
  archives.add(archive);
  return archive;
}

// ######################
// ##     Analyzer     ##
// ######################

#define LOG(L, ...)                                                                                \
  do {                                                                                             \
    if (this->logger) {                                                                            \
      auto &_logger = (*this->logger);                                                             \
      _logger.enabled(L) && (_logger)(L, __VA_ARGS__);                                             \
    }                                                                                              \
  } while (false)

std::unique_ptr<FrontEnd::Context> Analyzer::newContext(LexerPtr lexer) {
  auto &ctx = this->current();
  return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lexer), ctx->getScope());
}

const ModType &
Analyzer::newModTypeFromCurContext(const std::vector<std::unique_ptr<FrontEnd::Context>> &) {
  auto archive = std::move(*this->current()).buildArchive(this->archives);
  this->ctxs.pop_back();
  LOG(LogLevel::INFO, "exit module: id=%d, version=%d", archive->getModId(), archive->getVersion());
  auto *modType = loadFromArchive(this->current()->getPool(), *archive);
  assert(modType);
  return *modType;
}

static LexerPtr createLexer(const Source &src) {
  const char *fullPath = src.getPath().c_str();
  const char *ptr = src.getContent().c_str();
  return Lexer::fromFullPath(fullPath, ByteBuffer(ptr, ptr + src.getContent().size()));
}

FrontEnd::ModuleProvider::Ret Analyzer::load(const char *scriptDir, const char *modPath) {
  FilePtr filePtr;
  auto ret =
      ModuleLoaderBase::load(scriptDir, modPath, filePtr, ModLoadOption::IGNORE_NON_REG_FILE);
  if (is<ModLoadingError>(ret)) {
    return get<ModLoadingError>(ret);
  } else if (is<const char *>(ret)) {
    std::string content;
    if (!readAll(filePtr, content, SYS_LIMIT_INPUT_SIZE)) {
      return ModLoadingError(errno);
    }
    const char *fullPath = get<const char *>(ret);
    auto src = this->srcMan.find(fullPath);
    src = this->srcMan.update(fullPath, src->getVersion(), std::move(content));
    auto &ctx = this->addNew(*src);
    auto lex = createLexer(*src);
    return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lex), ctx->getScope());
  } else {
    assert(is<unsigned int>(ret));
    auto src = this->srcMan.findById(get<unsigned int>(ret));
    assert(src);
    if (auto archive = this->archives.find(src->getSrcId()); archive) {
      auto *modType = loadFromArchive(this->current()->getPool(), *archive);
      assert(modType);
      return modType;
    } else { // re-parse
      auto &ctx = this->addNew(*src);
      auto lex = createLexer(*src);
      return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lex), ctx->getScope());
    }
  }
}

const SysConfig &Analyzer::getSysConfig() const { return this->sysConfig; }

std::reference_wrapper<CancelToken> Analyzer::getCancelToken() const {
  static CancelToken cancelToken;

  if (this->cancelPoint) {
    return std::ref(static_cast<CancelToken &>(*this->cancelPoint));
  } else {
    return std::ref(cancelToken);
  }
}

const AnalyzerContextPtr &Analyzer::addNew(const Source &src) {
  LOG(LogLevel::INFO, "enter module: id=%d, version=%d, path=%s", src.getSrcId(), src.getVersion(),
      src.getPath().c_str());
  auto ptr = std::make_unique<AnalyzerContext>(this->sysConfig, src);
  this->ctxs.push_back(std::move(ptr));
  this->archives.reserve(src.getSrcId());
  return this->current();
}

ModResult Analyzer::addNewModEntry(CStrPtr &&ptr) {
  StringRef path = ptr.get();
  auto src = this->srcMan.find(path);
  if (src) { // already loaded
    if (auto archive = this->archives.find(src->getSrcId()); archive && archive->isEmpty()) {
      return ModLoadingError(ModLoadingError::CIRCULAR_LOAD); // nested import
    }
    return src->getSrcId();
  } else {
    src = this->srcMan.update(path, 0, ""); // dummy
    if (!src) {
      fatal("module id reaches limit(%u)\n", MAX_MOD_NUM); // FIXME: propagate error
    }
    return src->getPath().c_str();
  }
}

ModuleArchivePtr Analyzer::analyze(const Source &src, AnalyzerAction &action) {
  this->reset();

  // prepare
  this->addNew(src);
  FrontEnd frontEnd(*this, createLexer(src),
                    FrontEndOption::ERROR_RECOVERY | FrontEndOption::REPORT_WARN, nullptr);
  action.pass &&action.pass->enterModule(this->current()->getModId(), this->current()->getVersion(),
                                         this->current()->getPoolPtr());
  if (action.emitter) {
    frontEnd.setErrorListener(*action.emitter);
    action.emitter->enterModule(this->current()->getModId(), this->current()->getVersion());
  }
  if (action.dumper) {
    frontEnd.setASTDumper(*action.dumper);
  }

  // run front end
  frontEnd.setupASTDump();
  while (frontEnd) {
    if (this->cancelPoint && this->cancelPoint->isCanceled()) {
      return nullptr;
    }
    auto ret = frontEnd();
    switch (ret.kind) {
    case FrontEndResult::IN_MODULE:
      action.pass &&action.pass->consume(ret.node);
      break;
    case FrontEndResult::ENTER_MODULE:
      action.pass &&action.pass->enterModule(this->current()->getModId(),
                                             this->current()->getVersion(),
                                             this->current()->getPoolPtr());
      action.emitter &&action.emitter->enterModule(this->current()->getModId(),
                                                   this->current()->getVersion());
      break;
    case FrontEndResult::EXIT_MODULE:
      action.pass &&action.pass->exitModule(ret.node);
      action.emitter &&action.emitter->exitModule();
      break;
    case FrontEndResult::FAILED:
      fatal("unreachable\n");
    }
  }
  frontEnd.teardownASTDump();
  action.pass &&action.pass->exitModule(nullptr);
  action.emitter &&action.emitter->exitModule();
  if (auto *prevType = frontEnd.getPrevType(); prevType && prevType->isNothingType()) {
    this->current()->getScope()->updateModAttr(ModAttr::UNREACHABLE);
  }
  return std::move(*this->current()).buildArchive(this->archives);
}

// ###############################
// ##     DiagnosticEmitter     ##
// ###############################

DiagnosticEmitter::~DiagnosticEmitter() {
  while (!this->contexts.empty()) {
    this->exitModule();
  }
}

bool DiagnosticEmitter::handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                         const ParseError &parseError) {
  auto *cur = this->findContext(ctx.back()->scope->modId);
  assert(cur);
  auto range = toRange(*cur->src, parseError.getErrorToken());
  if (!range.hasValue()) {
    return false;
  }
  std::string code = "syntax error: ";
  code += parseError.getErrorKind();
  cur->diagnostics.push_back(Diagnostic{
      .range = range.unwrap(),
      .severity = DiagnosticSeverity::Error,
      .code = std::move(code),
      .message = parseError.getMessage(),
      .relatedInformation = {},
  });
  return true;
}

static DiagnosticSeverity resolveSeverity(TypeCheckError::Type type) {
  switch (type) {
  case TypeCheckError::Type::ERROR:
    return DiagnosticSeverity::Error;
  case TypeCheckError::Type::WARN:
    return DiagnosticSeverity::Warning;
  }
  return DiagnosticSeverity::Error; // unreachable (due to suppress gcc warning)
}

bool DiagnosticEmitter::handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                        const TypeCheckError &checkError, bool) {
  return this->handleTypeError(ctx.back()->scope->modId, checkError);
}

bool DiagnosticEmitter::handleTypeError(unsigned short modId, const TypeCheckError &checkError) {
  auto *cur = this->findContext(modId);
  assert(cur);
  auto range = toRange(*cur->src, checkError.getToken());
  if (!range.hasValue()) {
    return false;
  }
  std::string code = "semantic error: ";
  code += checkError.getKind();
  cur->diagnostics.push_back(Diagnostic{
      .range = range.unwrap(),
      .severity = resolveSeverity(checkError.getType()),
      .code = std::move(code),
      .message = checkError.getMessage(),
      .relatedInformation = {},
  });
  return true;
}

bool DiagnosticEmitter::enterModule(unsigned short modId, int version) {
  auto src = this->srcMan->findById(modId);
  assert(src);
  this->contexts.emplace_back(src, version);
  return true;
}

bool DiagnosticEmitter::exitModule() {
  if (this->contexts.empty()) {
    return false;
  }

  if (this->callback) {
    PublishDiagnosticsParams params = {
        .uri = toURI(*this->srcMan, this->contexts.back().src->getPath()).toString(),
        .version = {},
        .diagnostics = std::move(this->contexts.back().diagnostics),
    };
    if (this->supportVersion) {
      params.version = this->contexts.back().version;
    }
    this->callback(std::move(params));
  }
  this->contexts.pop_back();
  return true;
}

class CompletionItemCollector : public CompCandidateConsumer {
private:
  std::shared_ptr<TypePool> pool;
  std::vector<CompletionItem> items;

public:
  explicit CompletionItemCollector(std::shared_ptr<TypePool> pool) : pool(std::move(pool)) {}

  static CompletionItemKind toItemKind(const CompCandidate &candidate) {
    switch (candidate.kind) {
    case CompCandidateKind::VAR:
    case CompCandidateKind::VAR_IN_CMD_ARG:
      if (candidate.getHandle()->isFuncHandle()) {
        return CompletionItemKind::Function;
      } else {
        return CompletionItemKind::Variable;
      }
    case CompCandidateKind::FIELD:
      return CompletionItemKind::Field;
    case CompCandidateKind::METHOD:
    case CompCandidateKind::UNINIT_METHOD:
      return CompletionItemKind::Method;
    case CompCandidateKind::KEYWORD:
      return CompletionItemKind::Keyword;
    case CompCandidateKind::TYPE:
      return CompletionItemKind::Class;
    default:
      return CompletionItemKind::Text;
    }
  }

  static Optional<CompletionItemLabelDetails> formatLabelDetail(TypePool &pool,
                                                                const CompCandidate &candidate) {
    const auto itemKind = toItemKind(candidate);
    if (itemKind == CompletionItemKind::Variable) {
      auto &type = pool.get(candidate.getHandle()->getTypeId());
      std::string value;
      formatVarSignature(type, value);
      return CompletionItemLabelDetails{
          .detail = std::move(value),
          .description = {},
      };
    } else if (itemKind == CompletionItemKind::Function) {
      assert(candidate.getHandle()->isFuncHandle());
      auto &handle = cast<FuncHandle>(*candidate.getHandle());
      auto &type = pool.get(handle.getTypeId());
      assert(type.isFuncType());
      std::string value;
      formatFuncSignature(type, handle, value);
      return CompletionItemLabelDetails{
          .detail = std::move(value),
          .description = {},
      };
    } else if (itemKind == CompletionItemKind::Field) {
      auto &info = candidate.getFieldInfo();
      auto &recvType = pool.get(info.recvTypeId);
      auto &type = pool.get(info.typeId);
      std::string value;
      formatFieldSignature(recvType, type, value);
      return CompletionItemLabelDetails{
          .detail = std::move(value),
          .description = {},
      };
    } else if (itemKind == CompletionItemKind::Method) {
      std::string signature;
      if (candidate.kind == CompCandidateKind::UNINIT_METHOD) {
        auto &info = candidate.getNativeMethodInfo();
        auto &recvType = pool.get(info.typeId);
        auto handle = pool.allocNativeMethodHandle(recvType, info.methodIndex);
        if (!handle) {
          return {}; // normally unreachable
        }
        formatMethodSignature(recvType, *handle, signature);
      } else {
        auto *hd = candidate.getHandle();
        assert(hd);
        assert(hd->isMethodHandle());
        formatMethodSignature(pool.get(hd->getTypeId()), *cast<MethodHandle>(hd), signature);
      }
      return CompletionItemLabelDetails{
          .detail = std::move(signature),
          .description = {},
      };
    }
    return {};
  }

  void operator()(const CompCandidate &candidate) override {
    if (candidate.value.empty()) {
      return;
    }
    this->items.push_back(CompletionItem{
        .label = candidate.quote(),
        .labelDetails = formatLabelDetail(*this->pool, candidate),
        .kind = toItemKind(candidate),
        .sortText = {},
        .priority = candidate.priority,
    });
  }

  std::vector<CompletionItem> finalize() && {
    std::sort(this->items.begin(), this->items.end(),
              [](const CompletionItem &x, const CompletionItem &y) {
                return x.priority < y.priority || (x.priority == y.priority && x.label < y.label);
              });
    auto iter = std::unique(this->items.begin(), this->items.end(),
                            [](const CompletionItem &x, const CompletionItem &y) {
                              return x.priority == y.priority && x.label == y.label;
                            });
    this->items.erase(iter, this->items.end());

    // fill sortText
    int prioCount = 0;
    if (!this->items.empty()) {
      int prevPrio = this->items[0].priority;
      for (auto &item : this->items) {
        if (item.priority != prevPrio) {
          prioCount++;
          prevPrio = item.priority;
        }
        item.priority = prioCount;
      }
    }
    if (prioCount > 0) {
      unsigned int maxDigits = countDigits(prioCount);
      for (auto &item : this->items) {
        item.sortText = padLeft(item.priority, maxDigits, '0');
      }
    }
    return std::move(this->items);
  }
};

static std::string toDirName(const std::string &fullPath) {
  StringRef ref = fullPath;
  auto pos = ref.lastIndexOf("/");
  ref = ref.slice(0, pos);
  return ref.empty() ? "/" : ref.toString();
}

std::vector<CompletionItem> Analyzer::complete(const Source &src, unsigned int offset,
                                               CmdCompKind ckind, bool cmdArgComp) {
  this->reset();

  std::string workDir = toDirName(src.getPath());
  auto &ptr = this->addNew(src);
  CompletionItemCollector collector(ptr->getPoolPtr());
  CodeCompleter codeCompleter(collector,
                              makeObserver(static_cast<FrontEnd::ModuleProvider &>(*this)),
                              this->sysConfig, ptr->getPool(), workDir);
  CodeCompOp ignoredOp{};
  switch (ckind) {
  case CmdCompKind::disabled_:
    ignoredOp = CodeCompOp::COMMAND | CodeCompOp::FILE | CodeCompOp::EXEC;
    break;
  case CmdCompKind::default_:
    ignoredOp = CodeCompOp::EXTERNAL | CodeCompOp::FILE | CodeCompOp::EXEC;
    break;
  case CmdCompKind::all_:
    break; // allow all
  }
  if (!cmdArgComp) {
    setFlag(ignoredOp, CodeCompOp::HOOK);
  }
  StringRef source = src.getContent();
  source = source.substr(0, offset);
  codeCompleter(ptr->getScope(), src.getPath(), source, ignoredOp);
  return std::move(collector).finalize();
}

} // namespace ydsh::lsp