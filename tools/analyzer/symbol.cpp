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
#include <misc/format.hpp>
#include <type.h>

#include "source.h"
#include "symbol.h"

namespace ydsh::lsp {

std::string normalizeTypeName(const DSType &type) {
  static std::regex re(R"(%mod\d+\.)", std::regex_constants::ECMAScript);
  return std::regex_replace(type.getName(), re, "");
}

static std::vector<StringRef> splitParamNames(unsigned int paramSize, StringRef packedParamNames) {
  std::vector<StringRef> params;
  params.reserve(paramSize);
  splitByDelim(packedParamNames, ';', [&params](StringRef p, bool) {
    if (!p.empty()) {
      params.push_back(p);
    }
    return true;
  });
  assert(paramSize == params.size());
  return params;
}

void formatVarSignature(const DSType &type, std::string &out) {
  out += ": ";
  out += normalizeTypeName(type);
}

void formatFuncSignature(const FunctionType &funcType, const FuncHandle &handle, std::string &out,
                         const std::function<void(StringRef)> &paramCallback) {
  auto params = splitParamNames(funcType.getParamSize(), handle.getPackedParamNames());
  out += "(";
  for (unsigned int i = 0; i < funcType.getParamSize(); i++) {
    if (i > 0) {
      out += ", ";
    }
    const size_t offset = out.size();
    out += params[i];
    out += ": ";
    out += normalizeTypeName(funcType.getParamTypeAt(i));
    if (paramCallback) {
      paramCallback(StringRef(out.c_str() + offset));
    }
  }
  out += "): ";
  out += normalizeTypeName(funcType.getReturnType());
}

void formatFuncSignature(const DSType &retType, unsigned int paramSize,
                         const DSType *const *paramTypes, std::string &out,
                         const std::function<void(StringRef)> &paramCallback) {
  out += "(";
  for (unsigned int i = 0; i < paramSize; i++) {
    if (i > 0) {
      out += ", ";
    }
    const size_t offset = out.size();
    out += "p";
    out += std::to_string(i);
    out += ": ";
    out += normalizeTypeName(*paramTypes[i]);
    if (paramCallback) {
      paramCallback(StringRef(out.c_str() + offset));
    }
  }
  out += "): ";
  out += normalizeTypeName(retType);
}

void formatFieldSignature(const DSType &recvType, const DSType &type, std::string &out) {
  out += ": ";
  out += normalizeTypeName(type);
  out += " for ";
  out += normalizeTypeName(recvType);
}

void formatMethodSignature(const DSType &recvType, const MethodHandle &handle, std::string &out,
                           bool constructor, const std::function<void(StringRef)> &paramCallback) {
  auto params = splitParamNames(handle.getParamSize(), handle.getPackedParamNames());
  out += "(";
  for (unsigned int i = 0; i < handle.getParamSize(); i++) {
    if (i > 0) {
      out += ", ";
    }
    const size_t offset = out.size();
    out += params[i];
    out += ": ";
    out += normalizeTypeName(handle.getParamTypeAt(i));
    if (paramCallback) {
      paramCallback(StringRef(out.c_str() + offset));
    }
  }
  out += ")";
  if (!constructor) {
    out += ": ";
    out += normalizeTypeName(handle.getReturnType());
    out += " for ";
    out += normalizeTypeName(recvType);
  }
}

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

std::string generateHoverContent(const SourceManager &srcMan, const Source &src,
                                 const DeclSymbol &decl, bool markup) {
  std::string content = markup ? "```ydsh\n" : "";
  std::string name = DeclSymbol::demangle(decl.getKind(), decl.getAttr(), decl.getMangledName());
  switch (decl.getKind()) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::IMPORT_ENV: {
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
    content += "typedef ";
    content += name;
    content += decl.getInfo();
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
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::TYPE_ALIAS: {
    content += "typedef ";
    content += name;
    content += " = ";
    content += decl.getInfo();
    break;
  }
  case DeclSymbol::Kind::ERROR_TYPE_DEF: {
    content += "typedef ";
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
    symbolKind = SymbolKind::Method;
    break;
  case DeclSymbol::Kind::BUILTIN_CMD:
  case DeclSymbol::Kind::CMD:
    symbolKind = SymbolKind::Function;
    break;
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

} // namespace ydsh::lsp