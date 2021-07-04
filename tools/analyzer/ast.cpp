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
#include <frontend.h>
#include <misc/files.h>

#include "ast.h"

namespace ydsh::lsp {

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

ASTContext::ASTContext(unsigned int modID, const uri::URI &uri, std::string &&content, int version)
    : fullPath(uri.getPath()), content(std::move(content)), version(version) {
  auto &builtin = createBuiltin(this->pool, this->gvarCount);
  this->scope = IntrusivePtr<NameScope>::create(std::ref(this->gvarCount), modID);
  this->scope->importForeignHandles(builtin, true);

  // save discard point
  this->oldGvarCount = this->gvarCount;
  this->typeDiscardPoint = this->pool.getDiscardPoint();
  this->scopeDiscardPoint = this->scope->getDiscardPoint();
}

void ASTContext::updateContent(std::string &&c, int v) {
  this->content = std::move(c);
  this->version = v;
  this->gvarCount = this->oldGvarCount;
  this->pool.discard(this->typeDiscardPoint);
  this->scope->discard(this->scopeDiscardPoint);
  this->nodes.clear();
}

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change) {
  if (!change.range.hasValue()) {
    content = change.text;
    return true;
  } // FIXME: support incremental update
  return true;
}

} // namespace ydsh::lsp