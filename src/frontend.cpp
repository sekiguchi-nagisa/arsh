/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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
#include "core.h"
#include <cerrno>

namespace ydsh {

// ######################
// ##     FrontEnd     ##
// ######################

static auto wrapModLoadingError(const Node &node, const char *path, ModLoadingError e) {
  if (e.isCircularLoad()) {
    return createTCError<CircularMod>(node, path);
  } else if (e.isFileNotFound()) {
    return createTCError<NotFoundMod>(node, path);
  } else {
    return createTCError<NotOpenMod>(node, path, strerror(e.getErrNo()));
  }
}

bool FrontEnd::ErrorListener::handleModLoadingError(const std::vector<std::unique_ptr<Context>> &ctx,
                                                   const Node &pathNode, const char *modPath,
                                                   ModLoadingError e) {
  auto error = wrapModLoadingError(pathNode, modPath, e);
  return this->handleTypeError(ctx, error);
}

FrontEnd::FrontEnd(ModuleLoader &loader, Lexer &&lexer, TypePool &pool,
                   IntrusivePtr<NameScope> scope, FrontEndOption option,
                   ObserverPtr<CodeCompletionHandler> ccHandler)
    : modLoader(loader), option(option),
      checker(pool, hasFlag(option, FrontEndOption::TOPLEVEL), nullptr) {
  this->contexts.push_back(
      std::make_unique<Context>(std::move(lexer), std::move(scope), ccHandler));
  this->curScope()->clearLocalSize();
  this->checker.setLexer(this->getCurrentLexer());
  this->checker.setCodeCompletionHandler(ccHandler);
}

std::unique_ptr<Node> FrontEnd::tryToParse() {
  std::unique_ptr<Node> node;
  if (this->parser()) {
    node = this->parser()();
    if (this->parser().hasError()) {
      this->listener &&this->listener->handleParseError(this->contexts, this->parser().getError());
    } else if (this->uastDumper) {
      this->uastDumper(*node);
    }
  }
  return node;
}

bool FrontEnd::tryToCheckType(std::unique_ptr<Node> &node) {
  if (hasFlag(this->option, FrontEndOption::PARSE_ONLY)) {
    return true;
  }

  try {
    node = this->checker(this->prevType, std::move(node), this->curScope());
    this->prevType = &node->getType();

    if (this->astDumper) {
      this->astDumper(*node);
    }
    return true;
  } catch (const TypeCheckError &e) {
    this->listener &&this->listener->handleTypeError(this->contexts, e);
    return false;
  }
}

FrontEnd::Ret FrontEnd::operator()() {
  do {
    // load module
    Ret ret = this->loadModule();
    if (!ret || ret.status == ENTER_MODULE) {
      return ret;
    }

    // parse
    if (!ret.node) {
      ret.node = this->tryToParse();
      if (this->parser().hasError()) {
        return {nullptr, FAILED};
      }
    }

    if (!ret.node) { // when parse reach EOS
      ret.node = this->exitModule();
    }

    // check type
    if (!this->tryToCheckType(ret.node)) {
      return {nullptr, FAILED};
    }

    if (isa<SourceListNode>(*ret.node)) {
      auto &src = cast<SourceListNode>(*ret.node);
      if (!src.getPathList().empty()) {
        this->getCurSrcListNode().reset(cast<SourceListNode>(ret.node.release()));
        continue;
      }
    }

    if (isa<SourceNode>(*ret.node) && cast<SourceNode>(*ret.node).isFirstAppear()) {
      ret.status = EXIT_MODULE;
    }
    return ret;
  } while (true);
}

void FrontEnd::setupASTDump() {
  if (this->uastDumper) {
    this->uastDumper->initialize(this->getCurrentLexer().getSourceName(),
                                 "### dump untyped AST ###");
  }
  if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY)&& this->astDumper) {
    this->astDumper->initialize(this->getCurrentLexer().getSourceName(), "### dump typed AST ###");
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

FrontEnd::Ret FrontEnd::loadModule() {
  if (!this->hasUnconsumedPath()) {
    this->getCurSrcListNode().reset();
    return {nullptr, IN_MODULE};
  }

  auto &node = *this->getCurSrcListNode();
  unsigned int pathIndex = node.getCurIndex();
  const char *modPath = node.getPathList()[pathIndex].c_str();
  node.setCurIndex(pathIndex + 1);
  FilePtr filePtr;
  auto ret = this->modLoader.load(
      node.getPathNode().getType().is(TYPE::String) ? this->getCurScriptDir() : nullptr, modPath,
      filePtr, ModLoadOption::IGNORE_NON_REG_FILE);
  if (is<ModLoadingError>(ret)) {
    auto e = get<ModLoadingError>(ret);
    if (e.isFileNotFound() && node.isOptional()) {
      return {std::make_unique<EmptyNode>(), IN_MODULE};
    }
    if (this->listener) {
      this->listener->handleModLoadingError(this->contexts, node.getPathNode(), modPath, e);
    }
    return {nullptr, FAILED};
  } else if (is<const char *>(ret)) {
    ByteBuffer buf;
    if (!readAll(filePtr, buf)) {
      if (this->listener) {
        this->listener->handleModLoadingError(this->contexts, node.getPathNode(), modPath,
                                              ModLoadingError(errno));
      }
      return {nullptr, FAILED};
    }
    this->enterModule(get<const char *>(ret), std::move(buf));
    return {nullptr, ENTER_MODULE};
  } else if (is<unsigned int>(ret)) {
    auto &type = this->getTypePool().get(get<unsigned int>(ret));
    assert(type.isModType());
    auto &modType = static_cast<const ModType &>(type);
    if (this->curScope()->modId == modType.getModID()) { // normally unreachable
      if (this->listener) {
        this->listener->handleModLoadingError(this->contexts, node.getPathNode(), modPath,
                                              ModLoadingError(0));
      }
      return {nullptr, FAILED};
    }
    return {node.create(modType, false), IN_MODULE};
  }
  return {nullptr, FAILED};
}

static Lexer createLexer(const char *fullPath, ByteBuffer &&buf) {
  char *path = strdup(fullPath);
  const char *ptr = strrchr(path, '/');
  path[ptr == path ? 1 : ptr - path] = '\0';
  return Lexer(fullPath, std::move(buf), CStrPtr(path));
}

void FrontEnd::enterModule(const char *fullPath, ByteBuffer &&buf) {
  {
    auto lex = createLexer(fullPath, std::move(buf));
    auto scope = this->modLoader.createGlobalScopeFromFullpath(
        fullPath, this->getTypePool().getBuiltinModType());
    this->contexts.push_back(std::make_unique<Context>(std::move(lex), std::move(scope), nullptr));
  }
  this->checker.setLexer(this->getCurrentLexer());

  if (this->uastDumper) {
    this->uastDumper->enterModule(fullPath);
  }
  if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY) && this->astDumper) {
    this->astDumper->enterModule(fullPath);
  }
}

std::unique_ptr<SourceNode> FrontEnd::exitModule() {
  assert(!this->contexts.empty());
  auto &ctx = *this->contexts.back();
  auto &modType =
      this->modLoader.createModType(this->getTypePool(), *ctx.scope, ctx.lexer.getSourceName());
  const unsigned int varNum = ctx.scope->getMaxLocalVarIndex();
  this->contexts.pop_back();
  this->checker.setLexer(this->getCurrentLexer());

  auto node = this->getCurSrcListNode()->create(modType, true);
  if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY)) {
    node->setMaxVarNum(varNum);
    if (prevType != nullptr && this->prevType->isNothingType()) {
      this->prevType = &this->getTypePool().get(TYPE::Void);
      node->setNothing(true);
    }
  }

  if (this->uastDumper) {
    this->uastDumper->leaveModule();
  }
  if (!hasFlag(this->option, FrontEndOption::PARSE_ONLY) && this->astDumper) {
    this->astDumper->leaveModule();
  }
  return node;
}

} // namespace ydsh
