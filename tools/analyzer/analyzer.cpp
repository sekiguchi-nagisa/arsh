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

#include "analyzer.h"

namespace ydsh::lsp {

// ################################
// ##     ASTContextProvider     ##
// ################################

std::unique_ptr<FrontEnd::Context>
ASTContextProvider::newContext(Lexer &&lexer, FrontEndOption option,
                               ObserverPtr<CodeCompletionHandler> ccHandler) {
  auto ctx = this->find(lexer.getSourceName());
  return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lexer), ctx->getScope(),
                                             option, ccHandler);
}

const ModType &ASTContextProvider::newModTypeFromCurContext(
    const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx) {
  (void)ctx;
  fatal("unimplemented\n"); // FIXME: copy all FieldHandles in current TypePool to prev TypePool
}

FrontEnd::ModuleProvider::Ret ASTContextProvider::load(const char *scriptDir, const char *modPath,
                                                       FrontEndOption option) {
  modPath = "/"; // FIXME: dummy
  FilePtr filePtr;
  auto ret =
      ModuleLoaderBase::load(scriptDir, modPath, filePtr, ModLoadOption::IGNORE_NON_REG_FILE);
  if (is<ModLoadingError>(ret)) {
    return get<ModLoadingError>(ret);
  } else if (is<const char *>(ret)) {
    ByteBuffer buf;
    if (!readAll(filePtr, buf)) {
      return ModLoadingError(errno);
    }
    const char *fullpath = get<const char *>(ret);
    auto ctx = this->find(fullpath);
    assert(ctx);
    ctx->updateContent(std::string(buf.begin(), buf.end()));
    auto lex = Lexer::fromFullPath(fullpath, std::move(buf));
    return std::make_unique<FrontEnd::Context>(ctx->getPool(), std::move(lex), ctx->getScope(),
                                               option, nullptr);
  } else {
    assert(is<unsigned int>(ret)); // FIXME: load mod type
    fatal("unimplemented\n");
  }
}

ASTContextPtr ASTContextProvider::find(StringRef ref) const {
  auto iter = this->ctxMap.find(ref);
  if (iter != this->ctxMap.end()) {
    return iter->second;
  }
  return nullptr;
}

ASTContextPtr ASTContextProvider::addNew(const uri::URI &uri, std::string &&content, int version) {
  unsigned int newModId = this->ctxMap.size() + 1; // id 0 is already reserved
  auto ptr = ASTContextPtr::create(newModId, uri, std::move(content), version);
  this->ctxMap.emplace(ptr->getFullPath(), ptr);
  return ptr;
}

ModResult ASTContextProvider::addNewModEntry(CStrPtr &&ptr) {
  StringRef key = ptr.get();
  auto ctx = this->find(key);
  if (ctx) {                // already loaded
    return ctx->getModId(); // FIXME:
  } else {
    auto path = uri::URI::fromString(ptr.get());
    assert(path);
    unsigned int newModId = this->ctxMap.size() + 1; // id 0 is already reserved
    auto newctx = ASTContextPtr::create(newModId, path, "", 0);
    this->ctxMap.emplace(newctx->getFullPath(), newctx);
    if (this->ctxMap.size() == MAX_MOD_NUM) {
      fatal("module id reaches limit(%u)\n", MAX_MOD_NUM);
    }
    return newctx->getFullPath().c_str();
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

static FrontEnd createFrontend(ASTContextProvider &provider, const ASTContext &ctx) {
  const char *fullpath = ctx.getFullPath().c_str();
  const char *ptr = ctx.getContent().c_str();
  Lexer lexer = Lexer::fromFullPath(fullpath, ByteBuffer(ptr, ptr + strlen(ptr)));
  return FrontEnd(provider, std::move(lexer), FrontEndOption{}, nullptr);
}

static ASTContextPtr getCurrentCtx(const FrontEnd &frontEnd, const ASTContextProvider &provider) {
  auto &cur = frontEnd.getContext().back();
  StringRef key = cur->lexer.getSourceName();
  auto ctx = provider.find(key);
  assert(ctx);
  return ctx;
}

ASTContextPtr buildAST(ASTContextProvider &provider, DiagnosticEmitter &emitter,
                       ASTContextPtr ctx) {
  // prepare
  assert(ctx);
  FrontEnd frontEnd = createFrontend(provider, *ctx);
  frontEnd.setErrorListener(emitter);
  std::vector<ASTContextPtr> ctxs;
  ctxs.push_back(std::move(ctx));

  // run front end
  while (frontEnd) {
    auto ret = frontEnd();
    if (!ret) {
      return nullptr; // FIXME: error recovery
    }
    switch (ret.kind) {
    case FrontEndResult::ENTER_MODULE:
      ctxs.push_back(getCurrentCtx(frontEnd, provider));
      break;
    case FrontEndResult::EXIT_MODULE:
      ctxs.pop_back();
      break;
    case FrontEndResult::IN_MODULE:
      ctxs.back()->addNode(std::move(ret.node));
      break;
    default:
      break;
    }
  }
  return getCurrentCtx(frontEnd, provider);
}

} // namespace ydsh::lsp