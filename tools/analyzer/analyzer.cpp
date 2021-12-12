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

#include <cctype>

#include <binder.h>
#include <complete.h>
#include <embed.h>
#include <misc/files.h>

#include "analyzer.h"

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
  void operator()(const FieldHandle &, int64_t) {}

  void operator()(const FieldHandle &, const std::string &) {}

  void operator()(const FieldHandle &, FILE *) {}

  void operator()(const FieldHandle &, const DSType &) {}
};

static const ModType &createBuiltin(TypePool &pool, unsigned int &gvarCount) {
  auto builtin = NameScopePtr::create(gvarCount);
  EmptyConsumer emptyConsumer;
  bindBuiltins(emptyConsumer, pool, *builtin);

  ModuleLoader loader; // dummy
  const char *embed = embed_script;
  Lexer lexer("(builtin)", ByteBuffer(embed, embed + strlen(embed)), getCWD());
  DefaultModuleProvider provider(loader, pool, builtin);
  FrontEnd frontEnd(provider, std::move(lexer));
  consumeAllInput(frontEnd);
  gvarCount++; // reserve module object entry
  return builtin->toModType(pool);
}

AnalyzerContext::AnalyzerContext(const Source &src)
    : pool(std::make_shared<TypePool>()), version(src.getVersion()) {
  auto &builtin = createBuiltin(this->getPool(), this->gvarCount);
  this->scope = NameScopePtr::create(std::ref(this->gvarCount), src.getSrcId());
  this->scope->importForeignHandles(this->getPool(), builtin, ImportedModKind::GLOBAL);
  this->typeDiscardPoint = this->getPool().getDiscardPoint();
}

ModuleArchivePtr AnalyzerContext::buildArchive(ModuleArchives &archives) && {
  // pack handles
  auto &modType = this->getScope()->toModType(this->getPool());
  std::vector<Archive> handles;
  for (auto &e : modType.getHandleMap()) {
    handles.push_back(
        Archive::pack(this->getPool(), this->typeDiscardPoint.typeIdOffset, e.first, e.second));
  }

  // resolve imported modules
  std::vector<std::pair<ImportedModKind, ModuleArchivePtr>> imported;
  unsigned int size = modType.getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto e = modType.getChildAt(i);
    auto &type = cast<ModType>(this->getPool().get(e.typeId()));
    if (type.getModID() == 0) { // skip builtin module
      continue;
    }
    auto archive = archives.find(type.getModID());
    assert(archive);
    imported.emplace_back(e.kind(), std::move(archive));
  }

  auto archive = std::make_shared<ModuleArchive>(this->getModId(), this->getVersion(),
                                                 std::move(handles), std::move(imported));
  archives.add(archive);
  return archive;
}

// #####################################
// ##     AnalyzerContextProvider     ##
// #####################################

std::unique_ptr<FrontEnd::Context>
AnalyzerContextProvider::newContext(Lexer &&lexer, FrontEndOption option,
                                    ObserverPtr<CodeCompletionHandler> ccHandler) {
  auto &ctx = this->current();
  return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lexer), ctx->getScope(),
                                             option, ccHandler);
}

const ModType &AnalyzerContextProvider::newModTypeFromCurContext(
    const std::vector<std::unique_ptr<FrontEnd::Context>> &) {
  auto archive = std::move(*this->current()).buildArchive(this->archives);
  this->ctxs.pop_back();
  auto *modType = loadFromArchive(this->current()->getPool(), *archive);
  assert(modType);
  return *modType;
}

static Lexer createLexer(const Source &src) {
  const char *fullpath = src.getPath().c_str();
  const char *ptr = src.getContent().c_str();
  return Lexer::fromFullPath(fullpath, ByteBuffer(ptr, ptr + strlen(ptr)));
}

FrontEnd::ModuleProvider::Ret
AnalyzerContextProvider::load(const char *scriptDir, const char *modPath, FrontEndOption option) {
  FilePtr filePtr;
  auto ret =
      ModuleLoaderBase::load(scriptDir, modPath, filePtr, ModLoadOption::IGNORE_NON_REG_FILE);
  if (is<ModLoadingError>(ret)) {
    return get<ModLoadingError>(ret);
  } else if (is<const char *>(ret)) {
    std::string content;
    if (!readAll(filePtr, content)) {
      return ModLoadingError(errno);
    }
    const char *fullpath = get<const char *>(ret);
    auto src = this->srcMan.find(fullpath);
    src = this->srcMan.update(fullpath, src->getVersion(), std::move(content));
    auto &ctx = this->addNew(*src);
    auto lex = createLexer(*src);
    return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lex), ctx->getScope(),
                                               option, nullptr);
  } else {
    assert(is<unsigned int>(ret));
    auto src = this->srcMan.findById(get<unsigned int>(ret));
    assert(src);
    if (auto archive = this->archives.find(src->getSrcId()); archive) {
      return loadFromArchive(this->current()->getPool(), *archive);
    } else { // re-parse
      auto &ctx = this->addNew(*src);
      auto lex = createLexer(*src);
      return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lex), ctx->getScope(),
                                                 option, nullptr);
    }
  }
}

const AnalyzerContextPtr &AnalyzerContextProvider::addNew(const Source &src) {
  auto ptr = std::make_unique<AnalyzerContext>(src);
  this->ctxs.push_back(std::move(ptr));
  this->archives.reserve(src.getSrcId());
  return this->current();
}

ModResult AnalyzerContextProvider::addNewModEntry(CStrPtr &&ptr) {
  StringRef path = ptr.get();
  auto src = this->srcMan.find(path);
  if (src) { // already loaded
    if (auto archive = this->archives.find(src->getSrcId()); archive && archive->isEmpty()) {
      return ModLoadingError(0); // nested import
    }
    return src->getSrcId();
  } else {
    src = this->srcMan.update(path, 0, ""); // dummy
    if (!src) {
      fatal("module id reaches limit(%u)\n", MAX_MOD_NUM);
    }
    return src->getPath().c_str();
  }
}

// ###############################
// ##     DiagnosticEmitter     ##
// ###############################

bool DiagnosticEmitter::handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                         const ParseError &parseError) {
  assert(ctx.back()->scope->modId == this->contexts.back().src->getSrcId());
  (void)ctx;
  auto range = toRange(this->contexts.back().src->getContent(), parseError.getErrorToken());
  if (!range.hasValue()) {
    return false;
  }
  this->contexts.back().diagnostics.push_back(Diagnostic{
      .range = range.unwrap(),
      .severity = DiagnosticSeverity::Error,
      .message = parseError.getMessage(),
      .relatedInformation = {},
  });
  return true;
}

bool DiagnosticEmitter::handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                        const TypeCheckError &checkError, bool firstAppear) {
  if (!firstAppear) {
    return false;
  }
  assert(ctx.back()->scope->modId == this->contexts.back().src->getSrcId());
  (void)ctx;
  auto range = toRange(this->contexts.back().src->getContent(), checkError.getToken());
  if (!range.hasValue()) {
    return false;
  }
  this->contexts.back().diagnostics.push_back(Diagnostic{
      .range = range.unwrap(),
      .severity = DiagnosticSeverity::Error, // FIXME: support warning ?
      .message = checkError.getMessage(),
      .relatedInformation = {},
  });
  return true;
}

bool DiagnosticEmitter::enterModule(unsigned short modId, int version) {
  auto src = this->srcMan.findById(modId);
  assert(src);
  this->contexts.emplace_back(src, version);
  return true;
}

bool DiagnosticEmitter::exitModule() {
  if (this->callback) {
    PublishDiagnosticsParams params = {
        .uri = this->contexts.back().src->getPath(),
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

ModuleArchivePtr analyze(SourceManager &srcMan, ModuleArchives &archives, AnalyzerAction &action,
                         const Source &src) {
  // prepare
  AnalyzerContextProvider provider(srcMan, archives);
  provider.addNew(src);
  FrontEnd frontEnd(provider, createLexer(src), FrontEndOption::ERROR_RECOVERY, nullptr);
  if (action.emitter) {
    frontEnd.setErrorListener(*action.emitter);
    action.emitter->enterModule(provider.current()->getModId(), provider.current()->getVersion());
  }
  if (action.dumper) {
    frontEnd.setASTDumper(*action.dumper);
  }
  action.consumer &&action.consumer->enterModule(provider.current()->getModId(),
                                                 provider.current()->getVersion(),
                                                 provider.current()->getPoolPtr());

  // run front end
  frontEnd.setupASTDump();
  while (frontEnd) {
    auto ret = frontEnd();
    if (!ret) {
      provider.unwind(); // FIXME: future may be removed
      break;
    }
    switch (ret.kind) {
    case FrontEndResult::IN_MODULE:
      action.consumer &&action.consumer->consume(std::move(ret.node));
      break;
    case FrontEndResult::ENTER_MODULE:
      action.emitter &&action.emitter->enterModule(provider.current()->getModId(),
                                                   provider.current()->getVersion());
      action.consumer &&action.consumer->enterModule(provider.current()->getModId(),
                                                     provider.current()->getVersion(),
                                                     provider.current()->getPoolPtr());
      break;
    case FrontEndResult::EXIT_MODULE:
      action.emitter &&action.emitter->exitModule();
      action.consumer &&action.consumer->exitModule(std::move(ret.node));
      break;
    case FrontEndResult::FAILED:
      break;
    }
  }
  frontEnd.teardownASTDump();
  action.emitter &&action.emitter->exitModule();
  action.consumer &&action.consumer->exitModule(nullptr);
  return std::move(*provider.current()).buildArchive(archives);
}

static CompletionItemKind toItemKind(CompCandidateKind kind) {
  switch (kind) {
  case CompCandidateKind::VAR:
    return CompletionItemKind::Variable;
  case CompCandidateKind::FIELD:
    return CompletionItemKind::Field;
  case CompCandidateKind::METHOD:
    return CompletionItemKind::Method;
  case CompCandidateKind::KEYWORD:
    return CompletionItemKind::Keyword;
  case CompCandidateKind::TYPE:
    return CompletionItemKind::Class;
  case CompCandidateKind::SIGNAL:
    return CompletionItemKind::Event;
  default:
    return CompletionItemKind::Text;
  }
}

class CompletionItemCollector : public CompCandidateConsumer {
private:
  std::vector<CompletionItem> items;

public:
  void consume(std::string &&value, CompCandidateKind kind, int priority) override {
    if (kind == CompCandidateKind::ENV) {
      unsigned int count = 0;
      for (auto ch : value) {
        if (count == 0) {
          if (!std::isalpha(ch) && ch != '_') {
            return;
          }
        }
        if (!std::isalnum(ch) && ch != '_') {
          return;
        }
        count++;
      }
    }

    this->items.push_back(CompletionItem{
        .label = std::move(value),
        .kind = toItemKind(kind),
        .sortText = {},
        .priority = priority,
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
      unsigned int maxDigits = std::to_string(prioCount).size();
      for (auto &item : this->items) {
        item.sortText = formatPrio(item.priority, maxDigits);
      }
    }
    return this->items;
  }

private:
  static std::string formatPrio(int priority, unsigned int digits) {
    std::string value = std::to_string(priority);
    unsigned int diff = digits - value.size();
    std::string ret;
    for (unsigned int i = 0; i < diff; i++) {
      ret += std::to_string(0);
    }
    ret += value;
    return ret;
  }
};

std::vector<CompletionItem> doCompletion(SourceManager &srcMan, ModuleArchives &archives,
                                         const Source &src) {
  CompletionItemCollector collector;

  AnalyzerContextProvider provider(srcMan, archives);
  auto &ptr = provider.addNew(src);
  CodeCompleter codeCompleter(collector, makeObserver<FrontEnd::ModuleProvider>(provider),
                              ptr->getPool(), ptr->getScope(), "");
  auto ignoredOp = CodeCompOp::COMMAND | CodeCompOp::FILE | CodeCompOp::EXEC | CodeCompOp::HOOK;
  codeCompleter(src.getContent(), ignoredOp);
  return std::move(collector).finalize();
}

} // namespace ydsh::lsp