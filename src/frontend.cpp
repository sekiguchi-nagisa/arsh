/*
 * Copyright (C) 2018-2021 Nagisa Sekiguchi
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

#include "frontend.h"
#include <cerrno>

namespace arsh {

// ######################
// ##     FrontEnd     ##
// ######################

static auto wrapModLoadingError(const Node &node, const char *path, ModLoadingError e) {
  if (e.isCircularLoad()) {
    return createTCError<CircularMod>(node, path);
  } else if (e.isFileNotFound()) {
    return createTCError<NotFoundMod>(node, path);
  } else if (e.isModLimit()) {
    return createTCError<ModLimit>(node);
  } else if (e.isVarLimit()) {
    return createTCError<GlobalLimit>(node);
  } else {
    return createTCError<NotOpenMod>(node, path, strerror(e.getErrNo()));
  }
}

FrontEnd::FrontEnd(ModuleProvider &provider, std::unique_ptr<Context> &&ctx, FrontEndOption option,
                   ObserverPtr<CodeCompletionContext> compCtx)
    : checker(provider.getSysConfig(), provider.getCancelToken(), ctx->pool,
              hasFlag(option, FrontEndOption::TOPLEVEL), *ctx->lexer),
      provider(provider), option(option) {
  this->contexts.push_back(std::move(ctx));
  this->curScope()->clearLocalSize();
  this->checker.setCodeCompletionHandler(compCtx);
  if (hasFlag(this->option, FrontEndOption::REPORT_WARN)) {
    this->checker.setAllowWarning(true);
  }
}

bool FrontEnd::tryToParse() {
  auto &ctx = this->contexts.back();
  if (ctx->nodes.empty()) {
    ParserOption parserOption{};
    if (hasFlag(this->option, FrontEndOption::SINGLE_EXPR)) {
      setFlag(parserOption, ParserOption::SINGLE_EXPR);
    }
    if (hasFlag(this->option, FrontEndOption::COLLECT_SIGNATURE)) {
      setFlag(parserOption, ParserOption::COLLECT_SIGNATURE);
    }
    if (hasFlag(this->option, FrontEndOption::IMPLICIT_BLOCK)) {
      setFlag(parserOption, ParserOption::IMPLICIT_BLOCK);
    }
    ObserverPtr<CodeCompletionContext> handler;
    if (this->contexts.size() == 1) { // code completion is disabled in sourced scripts
      handler = this->checker.getCodeCompletionHandler();
    }
    Parser parser(*ctx->lexer, parserOption, handler);
    ctx->nodes = parser();
    assert(!ctx->nodes.empty());
    if (parser.hasError()) {
      this->curScope()->updateModAttr(ModAttr::HAS_ERRORS);
      this->listener &&this->listener->handleParseError(this->contexts, parser.getError());
      if (!hasFlag(this->option, FrontEndOption::ERROR_RECOVERY)) {
        return false;
      }
    }
  }
  return true;
}

std::unique_ptr<Node> FrontEnd::takeNode() {
  auto &ctx = this->contexts.back();
  std::unique_ptr<Node> node;
  if (auto &nodes = ctx->nodes; ctx->nodeIndex < nodes.size()) {
    node = std::move(nodes[ctx->nodeIndex]);
    ctx->nodeIndex++;
    if (this->uastDumper) {
      this->uastDumper(*node);
    }
  }
  return node;
}

bool FrontEnd::tryToCheckType(std::unique_ptr<Node> &node) {
  if (hasFlag(this->option, FrontEndOption::PARSE_ONLY)) {
    return true;
  }
  this->checker.setTypePool(this->contexts.back()->pool);
  this->checker.setLexer(*this->contexts.back()->lexer);
  node = this->checker(this->prevIsNothing, std::move(node), this->curScope());
  this->prevIsNothing = node->getType().isNothingType();

  auto &errors = this->checker.getErrors();
  unsigned int actualErrorCount = 0;
  for (size_t i = 0; i < errors.size(); i++) {
    this->listener &&this->listener->handleTypeError(this->contexts, errors[i], i == 0);
    if (errors[i].getType() == TypeCheckError::Type::ERROR) {
      actualErrorCount++;
    }
  }
  if (actualErrorCount) {
    this->curScope()->updateModAttr(ModAttr::HAS_ERRORS);
    if (hasFlag(this->option, FrontEndOption::ERROR_RECOVERY) &&
        !this->checker.hasReachedCompNode()) {
      return true;
    }
    return false;
  } else if (this->astDumper) {
    this->astDumper(*node);
  }
  return true;
}

FrontEndResult FrontEnd::operator()() {
  do {
    // load module
    auto ret = this->enterModule();
    if (!ret || ret.kind == FrontEndResult::Kind::ENTER_MODULE) {
      return ret;
    }

    // parse
    if (!ret.node) {
      if (!this->tryToParse()) {
        return FrontEndResult::failed();
      }
      ret.node = this->takeNode();
    }

    if (!ret.node) { // when parse reach EOS
      ret.node = this->exitModule();
    }

    // check type
    if (!this->tryToCheckType(ret.node)) {
      return FrontEndResult::failed();
    }

    if (isa<SourceListNode>(*ret.node)) {
      auto &src = cast<SourceListNode>(*ret.node);
      if (!src.getPathList().empty()) {
        this->getCurSrcListNode().reset(cast<SourceListNode>(ret.node.release()));
        continue;
      }
    }

    if (isa<SourceNode>(*ret.node) && cast<SourceNode>(*ret.node).isFirstAppear()) {
      ret.kind = FrontEndResult::Kind::EXIT_MODULE;
    }
    return ret;
  } while (true);
}

void FrontEnd::setupASTDump() {
  if (this->uastDumper) {
    this->uastDumper->initialize(this->getCurrentLexer()->getSourceName(),
                                 "### dump untyped AST ###");
  }
  if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY) && this->astDumper) {
    this->astDumper->initialize(this->getCurrentLexer()->getSourceName(), "### dump typed AST ###");
  }
}

void FrontEnd::teardownASTDump() {
  if (this->uastDumper) {
    this->uastDumper->finalize(*this->curScope());
  }
  if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY) && this->astDumper) {
    this->astDumper->finalize(*this->curScope());
  }
}

FrontEndResult FrontEnd::enterModule() {
  if (!this->hasUnconsumedPath()) {
    this->getCurSrcListNode().reset();
    return FrontEndResult::inModule(nullptr);
  }

  auto &node = *this->getCurSrcListNode();
  unsigned int pathIndex = node.getCurIndex();
  const char *modPath = node.getPathList()[pathIndex]->c_str();
  node.setCurIndex(pathIndex + 1);

  auto ret = this->provider.load(this->getCurScriptDir(), modPath);
  if (is<ModLoadingError>(ret)) {
    auto e = get<ModLoadingError>(ret);
    if (e.isFileNotFound() && node.isOptional()) {
      return FrontEndResult::inModule(std::make_unique<EmptyNode>());
    }
    auto error = wrapModLoadingError(node.getPathNode(), modPath, e);
    this->listener &&this->listener->handleTypeError(this->contexts, error, true);
    if (hasFlag(this->option, FrontEndOption::ERROR_RECOVERY)) {
      return FrontEndResult::inModule(std::make_unique<ErrorNode>(error.getToken()));
    }
    return FrontEndResult::failed();
  } else if (is<std::unique_ptr<Context>>(ret)) {
    auto &v = get<std::unique_ptr<Context>>(ret);
    const char *fullPath = v->lexer->getSourceName().c_str();
    this->contexts.push_back(std::move(v));
    if (this->uastDumper) {
      this->uastDumper->enterModule(fullPath);
    }
    if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY) && this->astDumper) {
      this->astDumper->enterModule(fullPath);
    }
    return FrontEndResult::enterModule();
  } else {
    assert(is<const ModType *>(ret));
    auto &modType = *get<const ModType *>(ret);
    if (this->curScope()->modId == modType.getModId()) { // when load module from completion context
      auto error = wrapModLoadingError(node.getPathNode(), modPath,
                                       ModLoadingError(ModLoadingError::CIRCULAR_LOAD));
      this->listener &&this->listener->handleTypeError(this->contexts, error, true);
      if (hasFlag(this->option, FrontEndOption::ERROR_RECOVERY)) {
        return FrontEndResult::inModule(std::make_unique<ErrorNode>(error.getToken()));
      }
      return FrontEndResult::failed();
    }
    return FrontEndResult::inModule(node.create(modType, false));
  }
}

std::unique_ptr<SourceNode> FrontEnd::exitModule() {
  assert(!this->contexts.empty());
  const unsigned int varNum = this->curScope()->getMaxLocalVarIndex();
  if (this->prevIsNothing) {
    this->curScope()->updateModAttr(ModAttr::UNREACHABLE);
    this->prevIsNothing = false;
  }
  auto &modType = this->provider.newModTypeFromCurContext(this->contexts);
  this->contexts.pop_back();

  auto node = this->getCurSrcListNode()->create(modType, true);
  if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY)) {
    node->setMaxVarNum(varNum);
  }

  if (this->uastDumper) {
    this->uastDumper->leaveModule();
  }
  if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY) && this->astDumper) {
    this->astDumper->leaveModule();
  }
  return node;
}

// ###################################
// ##     DefaultModuleProvider     ##
// ###################################

std::unique_ptr<FrontEnd::Context> DefaultModuleProvider::newContext(LexerPtr lexer) {
  return std::make_unique<FrontEnd::Context>(this->pool, std::move(lexer), this->scope);
}

const ModType &DefaultModuleProvider::newModTypeFromCurContext(
    const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx) {
  return this->newModType(*ctx.back()->scope);
}

FrontEnd::ModuleProvider::Ret DefaultModuleProvider::load(const char *scriptDir,
                                                          const char *modPath) {
  return this->load(scriptDir, modPath, ModLoadOption::IGNORE_NON_REG_FILE);
}

FrontEnd::ModuleProvider::Ret
DefaultModuleProvider::load(const char *scriptDir, const char *modPath, ModLoadOption loadOption) {
  FilePtr filePtr;
  auto ret = this->loader.load(scriptDir, modPath, filePtr, loadOption);
  if (is<ModLoadingError>(ret)) {
    return get<ModLoadingError>(ret);
  } else if (is<const char *>(ret)) {
    if (this->loader.getGvarCount() == SYS_LIMIT_GLOBAL_NUM) {
      return ModLoadingError(ModLoadingError::VAR_LIMIT);
    }

    ByteBuffer buf;
    if (!readAll(filePtr, buf, SYS_LIMIT_INPUT_SIZE)) {
      return ModLoadingError(errno);
    }
    const char *fullPath = get<const char *>(ret);
    auto lex = Lexer::fromFullPath(fullPath, std::move(buf));
    auto newScope = this->loader.createGlobalScopeFromFullPath(this->pool, fullPath,
                                                               this->pool.getBuiltinModType());
    return std::make_unique<FrontEnd::Context>(this->pool, std::move(lex), std::move(newScope));
  } else {
    assert(is<unsigned int>(ret));
    auto &type = this->pool.get(get<unsigned int>(ret));
    assert(type.isModType());
    return cast<ModType>(&type);
  }
}

const SysConfig &DefaultModuleProvider::getSysConfig() const { return this->loader.sysConfig; }

std::reference_wrapper<const CancelToken> DefaultModuleProvider::getCancelToken() const {
  return std::cref(*this->cancelToken);
}

} // namespace arsh
