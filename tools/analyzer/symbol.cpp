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

#include <regex>

#include <cmd_desc.h>
#include <constant.h>
#include <format_signature.h>
#include <type.h>

#include "source.h"
#include "symbol.h"

namespace arsh::lsp {

static const BuiltinCmdDesc *findCmdDesc(const char *name) {
  unsigned int size = getBuiltinCmdSize();
  auto *cmdList = getBuiltinCmdDescList();
  for (unsigned int i = 0; i < size; i++) {
    StringRef cmdName = name;
    if (cmdName == cmdList[i].name) {
      return cmdList + i;
    }
  }
  return nullptr;
}

static void formatCommandLineUsage(StringRef info, bool markup, std::string &content) {
  StringRef usage;
  auto pos = info.find("---");
  if (pos != StringRef::npos) {
    usage = info.substr(pos + 3);
    info = info.slice(0, pos);
  }
  content += info;
  if (!usage.empty()) {
    if (markup) {
      content += "\n```";
    }
    content += "\n\n";
    content += "**command line**\n";
    if (markup) {
      content += "```md\n";
    }
    content += usage;
  }
}

std::string generateHoverContent(const SourceManager &srcMan, const SymbolIndexes &indexes,
                                 const Source &src, const FindDeclResult &result, bool markup) {
  auto &decl = result.decl;
  StringRef packedParamTypes;
  if (decl.is(DeclSymbol::Kind::GENERIC_METHOD)) {
    if (auto index = indexes.find(src.getSrcId())) {
      auto *r = index->getPackedParamTypesMap().lookupByPos(result.request.getPos());
      if (r) {
        packedParamTypes = *r;
      }
    }
  }

  std::string content = markup ? "```arsh\n" : "";
  std::string name = decl.toDemangledName();
  switch (decl.getKind()) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::PREFIX_ENV:
  case DeclSymbol::Kind::THIS:
  case DeclSymbol::Kind::PARAM: {
    content += DeclSymbol::getVarDeclPrefix(decl.getKind());
    content += " ";
    content += name;
    content += ": ";
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::CONST: {
    content += "const ";
    content += name;
    content += " = ";
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::MOD_CONST: {
    content += "const ";
    content += name;
    content += " = '";
    if (name == CVAR_SCRIPT_NAME) {
      content += src.getPath();
    } else if (name == CVAR_SCRIPT_DIR) {
      const char *path = src.getPath().c_str();
      const char *ptr = strrchr(path, '/');
      assert(ptr);
      if (ptr == path) {
        content += '/';
      } else {
        content.append(path, ptr - path);
      }
    }
    content += "'";
    break;
  }
  case DeclSymbol::Kind::FUNC:
  case DeclSymbol::Kind::METHOD: {
    content += "function ";
    content += name;
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::CONSTRUCTOR: {
    content += "type ";
    content += name;
    formatCommandLineUsage(decl.getInfo(), markup, content);
    break;
  }
  case DeclSymbol::Kind::GENERIC_METHOD: {
    content += "function ";
    content += name;
    auto ret = convertToNum10<unsigned int>(decl.getInfo().begin(), decl.getInfo().end());
    assert(ret);
    formatNativeMethodSignature(&nativeFuncInfoTable()[ret.value], packedParamTypes, content);
    break;
  }
  case DeclSymbol::Kind::BUILTIN_CMD: {
    auto *cmd = findCmdDesc(name.c_str());
    assert(cmd);
    content = markup ? "```md\n" : "";
    content += name;
    content += ": ";
    content += name;
    content += " ";
    content += cmd->usage;
    content += "\n";
    content += cmd->detail;
    break;
  }
  case DeclSymbol::Kind::CMD: {
    content += name;
    content += "(): ";
    formatCommandLineUsage(decl.getInfo(), markup, content);
    break;
  }
  case DeclSymbol::Kind::BUILTIN_TYPE: {
    return "";
  }
  case DeclSymbol::Kind::TYPE_ALIAS: {
    content += "type ";
    content += name;
    content += " = ";
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::ERROR_TYPE_DEF: {
    content += "type ";
    content += name;
    content += ": ";
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::MOD: {
    auto ret = decl.getInfoAsModId();
    assert(ret.second);
    auto targetSrc = srcMan.findById(ret.first);
    assert(targetSrc);
    content += "source ";
    content += targetSrc->getPath();
    content += " as ";
    content += name;
    break;
  }
  case DeclSymbol::Kind::HERE_START: {
    content = markup ? "```md\n" : "";
    content += decl.getInfo();
    break;
  }
  }

  if (markup) {
    content += "\n```";
  }
  return content;
}

SymbolKind toSymbolKind(DeclSymbol::Kind kind) {
  SymbolKind symbolKind = SymbolKind::File;
  switch (kind) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::PREFIX_ENV:
  case DeclSymbol::Kind::THIS:
  case DeclSymbol::Kind::PARAM:
    symbolKind = SymbolKind::Variable;
    break;
  case DeclSymbol::Kind::CONST:
  case DeclSymbol::Kind::MOD_CONST:
    symbolKind = SymbolKind::Constant;
    break;
  case DeclSymbol::Kind::FUNC:
    symbolKind = SymbolKind::Function;
    break;
  case DeclSymbol::Kind::CONSTRUCTOR:
    symbolKind = SymbolKind::Constructor; // FIXME:
    break;
  case DeclSymbol::Kind::METHOD:
  case DeclSymbol::Kind::GENERIC_METHOD:
    symbolKind = SymbolKind::Method;
    break;
  case DeclSymbol::Kind::BUILTIN_CMD:
  case DeclSymbol::Kind::CMD:
    symbolKind = SymbolKind::Function;
    break;
  case DeclSymbol::Kind::BUILTIN_TYPE:
  case DeclSymbol::Kind::TYPE_ALIAS:
  case DeclSymbol::Kind::ERROR_TYPE_DEF:
    symbolKind = SymbolKind::Class;
    break;
  case DeclSymbol::Kind::MOD:
    symbolKind = SymbolKind::Variable;
    break;
  case DeclSymbol::Kind::HERE_START:
    symbolKind = SymbolKind::String;
    break;
  }
  return symbolKind;
}

std::string toString(ConstEntry entry) {
  std::string value;
  switch (entry.data.k) {
  case ConstEntry::INT:
    value += std::to_string(static_cast<unsigned int>(entry.data.v));
    break;
  case ConstEntry::BOOL:
    value += entry.data.v ? "true" : "false";
    break;
  case ConstEntry::SIG:
    value += "signal(";
    value += std::to_string(static_cast<unsigned int>(entry.data.v));
    value += ")";
    break;
  case ConstEntry::NONE:
    value += "new Nothing?()";
    break;
  }
  return value;
}

} // namespace arsh::lsp