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

#include <core.h>
#include <misc/files.h>

#include "analyzer.h"

namespace ydsh::lsp {

static bool isRevertedIndex(std::unordered_set<unsigned short> &revertingModIdSet,
                            const ModuleIndexPtr &index) {
  if (!index) {
    return false;
  }
  const auto modId = index->getModId();
  auto iter = revertingModIdSet.find(modId);
  if (iter != revertingModIdSet.end()) {
    return true;
  }
  for (auto &e : index->getImportedIndexes()) {
    if (isRevertedIndex(revertingModIdSet, e.second)) {
      revertingModIdSet.emplace(modId);
      return true;
    }
  }
  return false;
}

void IndexMap::revert(std::unordered_set<unsigned short> &&revertingModIdSet) {
  for (auto iter = this->map.begin(); iter != this->map.end();) {
    if (isRevertedIndex(revertingModIdSet, iter->second)) {
      iter = this->map.erase(iter);
    } else {
      ++iter;
    }
  }
}

static bool isImported(const ModuleIndexPtr &index, unsigned short id) {
  if (!index) {
    return false;
  }
  for (auto &e : index->getImportedIndexes()) {
    if (e.second) {
      if (e.second->getModId() == id) {
        return true;
      }
      if (isImported(e.second, id)) {
        return true;
      }
    }
  }
  return false;
}

bool IndexMap::revertIfUnused(unsigned short id) {
  for (auto &e : this->map) {
    if (isImported(e.second, id)) {
      return false;
    }
  }
  for (auto iter = this->map.begin(); iter != this->map.end();) {
    if (iter->second && iter->second->getModId() == id) {
      this->map.erase(iter);
      return true;
    } else {
      ++iter;
    }
  }
  return false;
}

// ########################
// ##     ASTContext     ##
// ########################

static void consumeAllInput(FrontEnd &frontEnd) {
  while (frontEnd) {
    if (!frontEnd()) {
      break;
    }
  }
}

static const ModType &createBuiltin(TypePool &pool, unsigned int &gvarCount) {
  auto builtin = IntrusivePtr<NameScope>::create(gvarCount);
  bindBuiltinVariables(nullptr, pool, *builtin);

  ModuleLoader loader; // dummy
  const char *embed = getEmbeddedScript();
  Lexer lexer("(builtin)", ByteBuffer(embed, embed + strlen(embed)), getCWD());
  DefaultModuleProvider provider(loader, pool, builtin);
  FrontEnd frontEnd(provider, std::move(lexer));
  consumeAllInput(frontEnd);
  gvarCount++; // reserve module object entry
  return builtin->toModType(pool);
}

ASTContext::ASTContext(const Source &src)
    : pool(std::make_unique<TypePool>()), version(src.getVersion()) {
  auto &builtin = createBuiltin(this->getPool(), this->gvarCount);
  this->scope = IntrusivePtr<NameScope>::create(std::ref(this->gvarCount), src.getSrcId());
  this->scope->importForeignHandles(builtin, true);
  this->typeDiscardPoint = this->getPool().getDiscardPoint();
}

ModuleIndexPtr ASTContext::buildAndAddIndex(const SourceManager &srcMan, IndexMap &indexMap) && {
  auto &modType = this->getScope()->toModType(this->getPool());
  auto archive = ModuleArchive::create(this->getPool(), modType, this->getTypeIdOffset());
  std::vector<std::pair<bool, ModuleIndexPtr>> imported;
  unsigned int size = modType.getChildSize();
  for (unsigned int i = 0; i < size; i++) {
    auto e = modType.getChildAt(i);
    auto &type = static_cast<const ModType &>(this->getPool().get(e.typeId()));
    if (type.getModID() == 0) { // skip builtin module
      continue;
    }
    auto src = srcMan.findById(type.getModID());
    assert(src);
    auto index = indexMap.find(*src);
    assert(index);
    imported.emplace_back(e.isGlobal(), std::move(index));
  }
  unsigned short id = this->getModId();
  auto index = ModuleIndex::create(id, this->getVersion(), std::move(this->pool),
                                   std::move(this->nodes), std::move(archive), std::move(imported));
  auto src = srcMan.findById(id);
  assert(src);
  indexMap.add(*src, index);
  return index;
}

// ################################
// ##     ASTContextProvider     ##
// ################################

std::unique_ptr<FrontEnd::Context>
ASTContextProvider::newContext(Lexer &&lexer, FrontEndOption option,
                               ObserverPtr<CodeCompletionHandler> ccHandler) {
  auto &ctx = this->current();
  return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lexer), ctx->getScope(),
                                             option, ccHandler);
}

const ModType &ASTContextProvider::newModTypeFromCurContext(
    const std::vector<std::unique_ptr<FrontEnd::Context>> &) {
  auto index = std::move(*this->current()).buildAndAddIndex(this->srcMan, this->indexMap);
  this->ctxs.pop_back();
  auto *modType = loadFromModuleIndex(this->current()->getPool(), *index);
  assert(modType);
  return *modType;
}

static Lexer createLexer(const Source &src) {
  const char *fullpath = src.getPath();
  const char *ptr = src.getContent().c_str();
  return Lexer::fromFullPath(fullpath, ByteBuffer(ptr, ptr + strlen(ptr)));
}

FrontEnd::ModuleProvider::Ret ASTContextProvider::load(const char *scriptDir, const char *modPath,
                                                       FrontEndOption option) {
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
    if (auto index = this->indexMap.find(*src); index) {
      return loadFromModuleIndex(this->current()->getPool(), *index);
    } else { // re-parse
      auto &ctx = this->addNew(*src);
      auto lex = createLexer(*src);
      return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lex), ctx->getScope(),
                                                 option, nullptr);
    }
  }
}

const ASTContextPtr &ASTContextProvider::addNew(const Source &src) {
  auto ptr = std::make_unique<ASTContext>(src);
  this->ctxs.push_back(std::move(ptr));
  this->indexMap.add(src, nullptr);
  return this->current();
}

ModResult ASTContextProvider::addNewModEntry(CStrPtr &&ptr) {
  StringRef path = ptr.get();
  auto src = this->srcMan.find(path);
  if (src) { // already loaded
    if (auto index = this->indexMap.find(*src); !index) {
      return ModLoadingError(0); // nest import
    }
    return src->getSrcId();
  } else {
    src = this->srcMan.update(path, 0, ""); // dummy
    if (!src) {
      fatal("module id reaches limit(%u)\n", MAX_MOD_NUM);
    }
    return src->getPath();
  }
}

// ###############################
// ##     DiagnosticEmitter     ##
// ###############################

bool DiagnosticEmitter::handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                         const ParseError &parseError) {
  (void)ctx;
  (void)parseError;
  return false;
}
bool DiagnosticEmitter::handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                        const TypeCheckError &checkError) {
  (void)ctx;
  (void)checkError;
  return false;
}

ModuleIndexPtr buildIndex(SourceManager &srcMan, IndexMap &indexMap, AnalyzerAction &action,
                          const Source &src) {
  // prepare
  ASTContextProvider provider(srcMan, indexMap);
  provider.addNew(src);
  FrontEnd frontEnd(provider, createLexer(src), FrontEndOption{}, nullptr);
  if (action.emitter) {
    frontEnd.setErrorListener(*action.emitter);
  }
  if (action.dumper) {
    frontEnd.setASTDumper(*action.dumper);
  }

  // run front end
  frontEnd.setupASTDump();
  while (frontEnd) {
    auto ret = frontEnd();
    if (!ret) {
      break;
    }
    if (ret.kind == FrontEndResult::IN_MODULE) {
      provider.current()->addNode(std::move(ret.node));
    }
  }
  frontEnd.teardownASTDump();
  return std::move(*provider.current()).buildAndAddIndex(srcMan, indexMap);
}

} // namespace ydsh::lsp