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
#include <format_signature.h>
#include <misc/files.hpp>
#include <misc/format.hpp>

#include "analyzer.h"
#include "symbol.h"

namespace arsh::lsp {

// #############################
// ##     AnalyzerContext     ##
// #############################

struct EmptyConsumer {
  void operator()(const Handle &, int64_t) {}

  void operator()(const Handle &, const std::string &) {}

  void operator()(const Handle &, FILE *) {}

  void operator()(const Handle &, const Type &) {}
};

static const ModType &createBuiltin(const SysConfig &config, TypePool &pool,
                                    unsigned int &gvarCount) {
  unsigned int modIndex = gvarCount++;
  auto builtin = NameScopePtr::create(gvarCount, modIndex, BUILTIN_MOD_ID);
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

AnalyzerContext::AnalyzerContext(const SysConfig &config, SourcePtr src)
    : pool(std::make_shared<TypePool>()), src(std::move(src)) {
  auto &builtin = createBuiltin(config, this->getPool(), this->gvarCount);
  unsigned int modIndex = this->gvarCount++;
  this->scope = NameScopePtr::create(std::ref(this->gvarCount), modIndex, this->src->getSrcId());
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
    if (isBuiltinMod(type.getModId())) { // skip builtin module
      continue;
    }
    auto archive = archives.find(type.getModId());
    assert(archive);
    imported.emplace_back(e.kind(), std::move(archive));
  }

  auto archive =
      std::make_shared<ModuleArchive>(modType.getModId(), this->getSrc()->getVersion(),
                                      modType.getAttr(), std::move(handles), std::move(imported));
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
  LOG(LogLevel::INFO, "exit module: id=%d, version=%d", toUnderlying(archive->getModId()),
      archive->getVersion());
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
    src =
        this->srcMan.update(fullPath, src->getVersion(), std::move(content), SourceAttr::FROM_DISK);
    auto &ctx = this->addNew(src);
    auto lex = createLexer(*src);
    return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lex), ctx->getScope());
  } else {
    assert(is<unsigned int>(ret));
    auto id = ModId{static_cast<unsigned short>(get<unsigned int>(ret))};
    auto src = this->srcMan.findById(id);
    assert(src);
    if (auto archive = this->archives.find(src->getSrcId()); archive) {
      auto *modType = loadFromArchive(this->current()->getPool(), *archive);
      assert(modType);
      return modType;
    } else { // re-parse
      auto &ctx = this->addNew(src);
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

const AnalyzerContextPtr &Analyzer::addNew(const SourcePtr &src) {
  assert(src);
  LOG(LogLevel::INFO, "enter module: id=%d, version=%d, path=%s", toUnderlying(src->getSrcId()),
      src->getVersion(), src->getPath().c_str());
  auto ptr = std::make_unique<AnalyzerContext>(this->sysConfig, src);
  this->ctxs.push_back(std::move(ptr));
  this->archives.reserve(src->getSrcId());
  return this->current();
}

ModResult Analyzer::addNewModEntry(CStrPtr &&ptr) {
  StringRef path = ptr.get();
  auto src = this->srcMan.find(path);
  if (src) { // already loaded
    if (auto archive = this->archives.find(src->getSrcId()); archive && archive->isEmpty()) {
      return ModLoadingError(ModLoadingError::CIRCULAR_LOAD); // nested import
    }
    return toUnderlying(src->getSrcId());
  } else {
    src = this->srcMan.update(path, 0, "", SourceAttr::FROM_DISK); // dummy
    if (!src) {
      return ModLoadingError(ModLoadingError::MOD_LIMIT);
    }
    return src->getPath().c_str();
  }
}

ModuleArchivePtr Analyzer::analyze(const SourcePtr &src, AnalyzerAction &action) {
  this->reset();

  // prepare
  this->addNew(src);
  FrontEnd frontEnd(*this, createLexer(*src),
                    FrontEndOption::ERROR_RECOVERY | FrontEndOption::REPORT_WARN, nullptr);
  action.pass &&action.pass->enterModule(this->current()->getSrc(), this->current()->getPoolPtr());
  if (action.emitter) {
    frontEnd.setErrorListener(*action.emitter);
    action.emitter->enterModule(this->current()->getSrc());
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
      action.pass &&action.pass->enterModule(this->current()->getSrc(),
                                             this->current()->getPoolPtr());
      action.emitter &&action.emitter->enterModule(this->current()->getSrc());
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
  if (frontEnd.isPrevTypeNothing()) {
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
  auto range = cur->src->toRange(parseError.getErrorToken());
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

bool DiagnosticEmitter::handleTypeError(ModId modId, const TypeCheckError &checkError) {
  auto *cur = this->findContext(modId);
  assert(cur);
  auto range = cur->src->toRange(checkError.getToken());
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

bool DiagnosticEmitter::exitModule() {
  if (this->contexts.empty()) {
    return false;
  }

  if (this->callback) {
    PublishDiagnosticsParams params = {
        .uri = this->srcMan->toURI(this->contexts.back().src->getPath()).toString(),
        .version = {},
        .diagnostics = std::move(this->contexts.back().diagnostics),
    };
    if (this->supportVersion) {
      params.version = this->contexts.back().src->getVersion();
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
  bool labelDetail{false};

public:
  explicit CompletionItemCollector(std::shared_ptr<TypePool> pool) : pool(std::move(pool)) {}

  void setLabelDetail(bool set) { this->labelDetail = set; }

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
    std::string signature = candidate.formatTypeSignature(pool);
    if (signature.empty()) {
      return {};
    }
    return CompletionItemLabelDetails{
        .detail = std::move(signature),
        .description = {},
    };
  }

  void operator()(CompCandidate &&candidate) override {
    if (candidate.value.empty()) {
      return;
    }
    Optional<CompletionItemLabelDetails> details;
    if (this->labelDetail) {
      details = formatLabelDetail(*this->pool, candidate);
    }
    this->items.push_back(CompletionItem{
        .label = std::move(candidate.value),
        .labelDetails = std::move(details),
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

std::vector<CompletionItem> Analyzer::complete(const SourcePtr &src, unsigned int offset,
                                               ExtraCompOp extraOp) {
  this->reset();

  std::string workDir = toDirName(src->getPath());
  auto &ptr = this->addNew(src);
  CompletionItemCollector collector(ptr->getPoolPtr());
  CodeCompleter codeCompleter(collector,
                              makeObserver(static_cast<FrontEnd::ModuleProvider &>(*this)),
                              this->sysConfig, ptr->getPool(), workDir);

  // set completion options
  const CodeCompOp ignoredOp = hasFlag(extraOp, ExtraCompOp::FILE_NAME)
                                   ? CodeCompOp{} // allow all completions (may complete file name)
                                   : CodeCompOp::EXTERNAL | CodeCompOp::FILE | CodeCompOp::EXEC;
  collector.setLabelDetail(hasFlag(extraOp, ExtraCompOp::SIGNATURE));

  // do code completion
  StringRef source = src->getContent();
  source = source.substr(0, offset);
  codeCompleter(ptr->getScope(), src->getPath(), source, ignoredOp);
  return std::move(collector).finalize();
}

static LexerPtr lex(const Source &src, unsigned int offset) {
  std::string workDir = toDirName(src.getPath());
  StringRef ref = src.getContent();
  ref = ref.substr(0, offset);
  ByteBuffer buf(ref.begin(), ref.end());
  if (!buf.empty() && buf.back() == '\n') {
    buf += '\n'; // explicitly append newline for command name completion
  }
  return LexerPtr::create(src.getPath().c_str(), std::move(buf), CStrPtr(strdup(workDir.c_str())));
}

Optional<SignatureInformation> Analyzer::collectSignature(const SourcePtr &src,
                                                          unsigned int offset) {
  this->reset();

  auto &ctx = this->addNew(src);
  auto workDir = toDirName(src->getPath());
  CodeCompletionContext dummyCtx(ctx->getScope(), workDir); // dummy
  FrontEnd frontEnd(static_cast<FrontEnd::ModuleProvider &>(*this), lex(*src, offset),
                    FrontEndOption::ERROR_RECOVERY | FrontEndOption::COLLECT_SIGNATURE,
                    makeObserver(dummyCtx));
  Optional<SignatureInformation> info;
  frontEnd.setSignatureHandler([&](const CallSignature &signature, const unsigned int paramIndex) {
    Optional<unsigned int> activeParamIndex;
    if (signature.paramSize) {
      if (paramIndex < signature.paramSize) {
        activeParamIndex = paramIndex;
      }
    }
    std::vector<ParameterInformation> params;
    auto callback = [&params](StringRef ref) {
      params.push_back(ParameterInformation{
          .label = ref.toString(),
      });
    };
    std::string out;
    if (!signature.handle) {
      if (!signature.returnType->isUnresolved()) {
        if (StringRef name = signature.name; name == OP_INIT) {
          out += "type "; // for Array, Map, Option constructor
          normalizeTypeName(*signature.returnType, out);
          out += "()";
        } else { // for indirect function call without variable name
          formatFuncSignature(*signature.returnType, signature.paramSize, signature.paramTypes, out,
                              callback);
        }
      }
    } else if (signature.handle->isFuncHandle()) {
      auto *funcHandle = cast<FuncHandle>(signature.handle);
      auto &type = ctx->getPool().get(funcHandle->getTypeId());
      assert(isa<FunctionType>(type));
      out += "function ";
      if (signature.name) {
        out += signature.name;
      }
      formatFuncSignature(cast<FunctionType>(type), *funcHandle, out, callback);
    } else if (signature.handle->isMethodHandle()) {
      String methodName = signature.name;
      auto *methodHandle = cast<MethodHandle>(signature.handle);
      auto &recvType = ctx->getPool().get(methodHandle->getTypeId());
      const bool constructor = methodName == OP_INIT;
      if (constructor) {
        out += "type ";
        normalizeTypeName(recvType, out);
      } else {
        out += "function ";
        out += methodName;
      }
      formatMethodSignature(recvType, *methodHandle, out, constructor, callback);
    } else if (!signature.returnType->isUnresolved()) {
      formatFuncSignature(*signature.returnType, signature.paramSize, signature.paramTypes, out,
                          callback); // indirect function call with variable name
    }

    if (out.empty()) {
      return;
    }
    if (!activeParamIndex.hasValue()) {
      params.clear();
    }
    info = SignatureInformation{
        .label = std::move(out),
        .documentation = {},
        .parameters = std::move(params),
        .activeParameter = activeParamIndex,
    };
  });
  consumeAllInput(frontEnd);
  return info;
}

} // namespace arsh::lsp