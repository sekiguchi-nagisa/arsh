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

#include "hover.h"

namespace ydsh::lsp {

std::string generateHoverContent(const DeclSymbol &decl) {
  std::string content = "```ydsh\n";
  std::string name = DeclSymbol::demangle(decl.getKind(), decl.getMangledName());
  switch (decl.getKind()) {
  case DeclSymbol::Kind::VAR: { // FIXME: var, let, env...
    content += "var ";
    content += name;
    content += " : ";
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::FUNC: { // FIXME: dirtect func, func type obj
    content += "function ";
    content += name;
    content += "()";
    break;
  }
  case DeclSymbol::Kind::CMD: { // FIXME: udc, builtin ...
    content += name;
    content += "()";
    break;
  }
  case DeclSymbol::Kind::TYPE_ALIAS: {
    content += "typedef ";
    content += name;
    content += " = ";
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::MOD: { // FIXME: path
    content += name;
    break;
  }
  }

  content += "\n```";
  return content;
}

} // namespace ydsh::lsp