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
  const auto range = getBuiltinCmdDescRange();
  for (auto &e : range) {
    if (StringRef(name) == e.name) {
      return &e;
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
  if (decl.is(DeclSymbol::Kind::GENERIC_METHOD) ||
      decl.is(DeclSymbol::Kind::GENERIC_METHOD_PARAM)) {
    if (auto index = indexes.find(src.getSrcId())) {
      if (auto *r = index->packedParamTypesMap.lookupByPos(result.request.getPos())) {
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
  case DeclSymbol::Kind::GENERIC_METHOD_PARAM: {
    const auto pos = decl.getInfo().find(':'); // follow `methodIndex:paramIndex` form
    auto ref = decl.getInfo().slice(0, pos);   // extract methodIndex str
    auto ret = convertToNum10<unsigned int>(ref.begin(), ref.end());
    assert(ret);
    const unsigned int methodIndex = ret.value;
    ref = decl.getInfo().substr(pos + 1); // extract paramIndex str
    ret = convertToNum10<unsigned int>(ref.begin(), ref.end());
    assert(ret);
    const unsigned int paramIndex = ret.value;
    unsigned int paramCount = 0;
    std::string dummy;
    std::string param;
    formatNativeMethodSignature(methodIndex, packedParamTypes, dummy, [&](const StringRef p) {
      if (paramIndex == paramCount++) {
        param = p.toString();
      }
    });
    assert(!param.empty());
    content += DeclSymbol::getVarDeclPrefix(decl.getKind());
    content += " ";
    content += param;
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
    formatNativeMethodSignature(ret.value, packedParamTypes, content);
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

SymbolKind toSymbolKind(DeclSymbol::Kind kind, DeclSymbol::Attr attr) {
  SymbolKind symbolKind = SymbolKind::File;
  switch (kind) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::EXPORT_ENV:
    symbolKind = hasFlag(attr, DeclSymbol::Attr::MEMBER) ? SymbolKind::Field : SymbolKind::Variable;
    break;
  case DeclSymbol::Kind::PREFIX_ENV:
  case DeclSymbol::Kind::THIS:
  case DeclSymbol::Kind::PARAM:
  case DeclSymbol::Kind::GENERIC_METHOD_PARAM:
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
    symbolKind = SymbolKind::Constructor;
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

static bool isIgnoredDocSymbol(const DeclSymbol &decl) {
  return isBuiltinMod(decl.getModId()) || decl.has(DeclSymbol::Attr::BUILTIN) ||
         decl.is(DeclSymbol::Kind::THIS) || decl.is(DeclSymbol::Kind::HERE_START);
}

static std::string toDocSymbolDetail(const DeclSymbol &decl) {
  switch (decl.getKind()) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::PREFIX_ENV:
  case DeclSymbol::Kind::THIS:
  case DeclSymbol::Kind::CONST:
  case DeclSymbol::Kind::MOD_CONST:
  case DeclSymbol::Kind::PARAM:
  case DeclSymbol::Kind::GENERIC_METHOD_PARAM:
  case DeclSymbol::Kind::FUNC:
  case DeclSymbol::Kind::METHOD:
  case DeclSymbol::Kind::GENERIC_METHOD:
  case DeclSymbol::Kind::TYPE_ALIAS:
    return decl.getInfo().toString();
  case DeclSymbol::Kind::CONSTRUCTOR: {
    auto info = decl.getInfo();
    auto pos = info.find(" {");
    if (pos != StringRef::npos) {
      info = info.slice(0, pos);
    }
    return info.toString();
  }
  case DeclSymbol::Kind::CMD: {
    auto info = decl.getInfo();
    auto pos = info.find("---");
    if (pos != StringRef::npos) {
      info = info.slice(0, pos);
    }
    std::string value = "(): ";
    value += info;
    return value;
  }
  case DeclSymbol::Kind::BUILTIN_CMD:
  case DeclSymbol::Kind::BUILTIN_TYPE:
  case DeclSymbol::Kind::HERE_START:
  case DeclSymbol::Kind::ERROR_TYPE_DEF:
    break;
  case DeclSymbol::Kind::MOD: {
    auto ret = decl.getInfoAsModId();
    assert(ret.second);
    return toModTypeName(ret.first);
  }
  }
  return "";
}

static DocumentSymbol toDocSymbol(const Source &src, const DeclSymbol &decl) {
  auto selectionRange = src.toRange(decl.getToken());
  auto range = src.toRange(decl.getBody());
  auto name = decl.toDemangledName();
  assert(selectionRange.hasValue());
  assert(range.hasValue());
  return {
      .name = std::move(name),
      .detail = toDocSymbolDetail(decl),
      .kind = toSymbolKind(decl.getKind(), decl.getAttr()),
      .range = range.unwrap(),
      .selectionRange = selectionRange.unwrap(),
      .children = {},
  };
}

std::vector<DocumentSymbol> generateDocumentSymbols(const SymbolIndexes &indexes,
                                                    const Source &src) {
  std::vector<DocumentSymbol> ret;
  if (auto index = indexes.find(src.getSrcId())) {
    Token funcBody;
    for (auto &decl : index->decls) {
      if (isIgnoredDocSymbol(decl)) {
        continue;
      }
      auto doc = toDocSymbol(src, decl);
      if (funcBody.size && decl.getToken().endPos() < funcBody.endPos()) { // within func
        ret.back().children.unwrap().push_back(std::move(doc));
        continue;
      }
      if (doc.kind == SymbolKind::Function || doc.kind == SymbolKind::Method ||
          doc.kind == SymbolKind::Constructor) {
        funcBody = decl.getBody();
        doc.children = std::vector<DocumentSymbol>{};
      } else {
        funcBody = {0, 0};
      }
      ret.push_back(std::move(doc));
    }
  }
  return ret;
}

} // namespace arsh::lsp