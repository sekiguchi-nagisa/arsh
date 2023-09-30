/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include <cstdarg>

#include "analyzer.h"
#include "extra_checker.h"

namespace ydsh::lsp {

// ##########################
// ##     ExtraChecker     ##
// ##########################

bool ExtraChecker::enterModule(const SourcePtr &src, const std::shared_ptr<TypePool> &) {
  this->modIds.push_back(src->getSrcId());
  return true;
}

bool ExtraChecker::exitModule(const std::unique_ptr<Node> &node) {
  assert(!this->modIds.empty());
  this->modIds.pop_back();
  this->visit(node);
  return true;
}

void ExtraChecker::warnImpl(Token token, const char *kind, const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    fatal_perror("failed");
  }
  va_end(arg);

  TypeCheckError error(TypeCheckError::Type::WARN, token, kind, CStrPtr(str));
  this->emitter.handleTypeError(this->modIds.back(), error);
}

void ExtraChecker::checkVarDecl(VarDeclNode &node, bool maybeUnused) {
  if (auto &handle = node.getHandle(); handle && !maybeUnused) {
    if (!handle->has(HandleAttr::GLOBAL) && !handle->has(HandleAttr::UNCAPTURED) &&
        !handle->is(HandleKind::ENV) && handle.useCount() == 1) {
      this->warn<UnusedLocal>(node.getNameInfo().getToken(), node.getNameInfo().getName().c_str());
    }
  }
  NodePass::visitVarDeclNode(node);
}

void ExtraChecker::visitVarDeclNode(VarDeclNode &node) { this->checkVarDecl(node, false); }

void ExtraChecker::visitTypeDefNode(TypeDefNode &node) {
  if (node.getDefKind() == TypeDefNode::ALIAS) {
    if (auto &handle = node.getHandle();
        handle && !handle->has(HandleAttr::GLOBAL) && handle.useCount() == 1) {
      this->warn<UnusedTypeAlias>(node.getNameInfo().getToken(), node.getName().c_str());
    }
  }
  NodePass::visitTypeDefNode(node);
}

void ExtraChecker::visitFunctionNode(FunctionNode &node) {
  for (auto &e : node.getParamNodes()) {
    this->checkVarDecl(*e, true);
  }
  this->visit(node.getRecvTypeNode());
  this->visit(node.getReturnTypeNode());
  this->visit(node.getBlockNode());
}

} // namespace ydsh::lsp