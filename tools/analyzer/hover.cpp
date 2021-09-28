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
#include "source.h"
#include <misc/num_util.hpp>

namespace ydsh::lsp {

static const char *getVarDeclKind(DeclSymbol::Kind k) {
  switch (k) {
  case DeclSymbol::Kind::VAR:
    return "var";
  case DeclSymbol::Kind::LET:
    return "let";
  case DeclSymbol::Kind::EXPORT_ENV:
    return "export-env";
  case DeclSymbol::Kind::IMPORT_ENV:
    return "import-env";
  default:
    return "";
  }
}

std::string generateHoverContent(const SourceManager &srcMan, const DeclSymbol &decl) {
  std::string content = "```ydsh\n";
  std::string name = DeclSymbol::demangle(decl.getKind(), decl.getMangledName());
  switch (decl.getKind()) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::IMPORT_ENV: {
    content += getVarDeclKind(decl.getKind());
    content += " ";
    content += name;
    content += " : ";
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::FUNC: {
    content += "function ";
    content += name;
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::CMD: { // FIXME: builtin ...
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
  case DeclSymbol::Kind::MOD: {
    auto ref = decl.getInfo();
    auto ret = convertToNum<int>(ref.begin(), ref.end());
    assert(ret.second);
    assert(ret.first <= UINT16_MAX && ret.first >= 0);
    auto modId = static_cast<unsigned short>(ret.first);
    auto src = srcMan.findById(modId);
    assert(src);
    content += "source ";
    content += src->getPath();
    content += " as ";
    content += name;
    break;
  }
  }

  content += "\n```";
  return content;
}

} // namespace ydsh::lsp