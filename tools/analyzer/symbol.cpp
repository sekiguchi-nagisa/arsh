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

static std::string normalizeTypeName(StringRef typeName) {
  static std::regex re(R"(%mod\d+\.)", std::regex_constants::ECMAScript);
  return std::regex_replace(typeName.toString(), re, "");
}

std::string normalizeTypeName(const DSType &type) { return normalizeTypeName(type.getNameRef()); }

static std::vector<StringRef> splitParamNames(StringRef packedParamNames) {
  std::vector<StringRef> params;
  params.reserve(4);
  splitByDelim(packedParamNames, ';', [&params](StringRef p, bool) {
    if (!p.empty()) {
      params.push_back(p);
    }
    return true;
  });
  return params;
}

void formatVarSignature(const DSType &type, std::string &out) {
  out += ": ";
  out += normalizeTypeName(type);
}

void formatFuncSignature(const FunctionType &funcType, const FuncHandle &handle, std::string &out,
                         const std::function<void(StringRef)> &paramCallback) {
  auto params = splitParamNames(handle.getPackedParamNames());
  assert(params.size() == funcType.getParamSize());
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
  auto params = splitParamNames(handle.getPackedParamNames());
  assert(params.size() == handle.getParamSize());
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

class Decoder {
private:
  const HandleInfo *ptr;
  const std::vector<StringRef> &paramTypes;

public:
  Decoder(const HandleInfo *ptr, const std::vector<StringRef> &types)
      : ptr(ptr), paramTypes(types) {}

  unsigned int decodeNum() {
    return static_cast<unsigned int>(static_cast<int>(*(this->ptr++)) -
                                     static_cast<int>(HandleInfo::P_N0));
  }

  std::string decodeType();

  /**
   * consume constraint part
   */
  void decodeConstraint() {
    unsigned int size = this->decodeNum();
    for (unsigned int i = 0; i < size; i++) {
      this->decodeType();
      this->decodeType();
    }
  }
};

static bool isFuncType(StringRef ref) {
  if (!ref.startsWith("(")) {
    return false;
  }
  int level = 0;
  auto iter = ref.begin();
  const auto end = ref.end();
  for (; iter != end; ++iter) {
    char ch = *iter;
    if (ch == '(') {
      level++;
    } else if (ch == ')') {
      level--;
    }
    if (level == 0) {
      ++iter;
      break;
    }
  }
  for (; iter != end; ++iter) {
    char ch = *iter;
    if (ch == '-' && iter + 1 != end && *(iter + 1) == '>') {
      return true;
    }
  }
  return false;
}

static const char *toStringPrimitive(HandleInfo info) {
  switch (info) {
#define GEN_CASE(E)                                                                                \
  case HandleInfo::E:                                                                              \
    return #E;
    EACH_HANDLE_INFO_TYPE(GEN_CASE)
#undef GEN_CASE
  default:
    break;
  }
  return "";
}

std::string Decoder::decodeType() {
  const auto info = *(this->ptr++);
  switch (info) {
#define GEN_CASE(E) case HandleInfo::E:
    EACH_HANDLE_INFO_TYPE(GEN_CASE)
#undef GEN_CASE
    {
      StringRef v = toStringPrimitive(info);
      if (v == "Reader") {
        v = "Reader%%";
      } else if (v == "Value_") {
        v = "Value%%";
      } else if (v == "StringIter") {
        v = "StringIter%%";
      }
      return v.toString();
    }
  case HandleInfo::Array: {
    unsigned int size = this->decodeNum();
    (void)size;
    assert(size == 1);
    std::string out = "[";
    out += this->decodeType();
    out += "]";
    return out;
  }
  case HandleInfo::Map: {
    unsigned int size = this->decodeNum();
    (void)size;
    assert(size == 2);
    std::string out = "[";
    out += this->decodeType();
    out += " : ";
    out += this->decodeType();
    out += "]";
    return out;
  }
  case HandleInfo::Tuple: {
    unsigned int size = this->decodeNum();
    assert(size > 0);
    std::string out = "(";
    if (size == 1) {
      out += this->decodeType();
      out += ",";
    } else {
      for (unsigned int i = 0; i < size; i++) {
        if (i > 0) {
          out += ", ";
        }
        out += this->decodeType();
      }
    }
    out += ")";
    return out;
  }
  case HandleInfo::Option: {
    unsigned int size = this->decodeNum();
    (void)size;
    assert(size == 1);
    std::string out = this->decodeType();
    if (isFuncType(out)) {
      out.insert(0, "(") += ")";
    }
    out += "?";
    return out;
  }
  case HandleInfo::Func: {
    std::string ret = this->decodeType();
    unsigned int size = this->decodeNum();
    std::string out = "(";
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        out += ", ";
      }
      out += this->decodeType();
    }
    out += ") -> ";
    out += ret;
    return out;
  }
  case HandleInfo::P_N0:
  case HandleInfo::P_N1:
  case HandleInfo::P_N2:
  case HandleInfo::P_N3:
  case HandleInfo::P_N4:
  case HandleInfo::P_N5:
  case HandleInfo::P_N6:
  case HandleInfo::P_N7:
  case HandleInfo::P_N8:
    break; // normally unreachable
  case HandleInfo::T0:
    return this->paramTypes[0].toString();
  case HandleInfo::T1:
    return this->paramTypes[1].toString();
  }
  return ""; // normally unreachable due to suppress gcc warning
}

void formatNativeMethodSignature(const NativeFuncInfo *funcInfo, StringRef packedParamType,
                                 std::string &out) {
  auto params = splitParamNames(funcInfo->params);
  auto paramTypes = splitParamNames(packedParamType);
  Decoder decoder(funcInfo->handleInfo, paramTypes);

  decoder.decodeConstraint(); // ignore constraint

  auto returnTypeName = decoder.decodeType();
  unsigned int paramSize = decoder.decodeNum();
  assert(paramSize > 0);
  paramSize--; // ignore receiver
  assert(paramSize == params.size());
  auto recvTypeName = decoder.decodeType();

  out += "(";
  for (unsigned int i = 0; i < paramSize; i++) {
    if (i > 0) {
      out += ", ";
    }
    out += params[i];
    out += ": ";
    out += normalizeTypeName(decoder.decodeType());
  }
  out += ")";
  if (StringRef(funcInfo->funcName) != OP_INIT) { // method
    out += ": ";
    out += normalizeTypeName(returnTypeName);
    out += " for ";
    out += normalizeTypeName(recvTypeName);
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
  if (decl.getKind() == DeclSymbol::Kind::GENERIC_METHOD) {
    if (auto index = indexes.find(src.getSrcId())) {
      auto *r = index->getPackedParamTypesMap().lookupByPos(result.request.getPos());
      if (r) {
        packedParamTypes = *r;
      }
    }
  }

  std::string content = markup ? "```ydsh\n" : "";
  std::string name = decl.toDemangledName();
  switch (decl.getKind()) {
  case DeclSymbol::Kind::VAR:
  case DeclSymbol::Kind::LET:
  case DeclSymbol::Kind::EXPORT_ENV:
  case DeclSymbol::Kind::IMPORT_ENV:
  case DeclSymbol::Kind::THIS: {
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
    formatCommandLineUsage(decl.getInfo(), markup, content);
    break;
  }
  case DeclSymbol::Kind::GENERIC_METHOD: {
    content += "function ";
    content += name;
    auto ret = convertToDecimal<unsigned int>(decl.getInfo().begin(), decl.getInfo().end());
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
  case DeclSymbol::Kind::THIS:
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

} // namespace ydsh::lsp