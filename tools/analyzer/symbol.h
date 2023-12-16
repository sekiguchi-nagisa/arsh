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

#ifndef ARSH_TOOLS_ANALYZER_SYMBOL_H
#define ARSH_TOOLS_ANALYZER_SYMBOL_H

#include "index.h"
#include "lsp.h"

namespace arsh {
class DSType;
class FunctionType;
class FuncHandle;
class MethodHandle;
} // namespace arsh

namespace arsh::lsp {

class SourceManager;
class Source;

std::string normalizeTypeName(const DSType &type);

void formatVarSignature(const DSType &type, std::string &out);

void formatFuncSignature(const FunctionType &funcType, const FuncHandle &handle, std::string &out,
                         const std::function<void(StringRef)> &paramCallback = nullptr);

void formatFuncSignature(const DSType &retType, unsigned int paramSize,
                         const DSType *const *paramTypes, std::string &out,
                         const std::function<void(StringRef)> &paramCallback = nullptr);

void formatFieldSignature(const DSType &recvType, const DSType &type, std::string &out);

void formatMethodSignature(const DSType &recvType, const MethodHandle &handle, std::string &out,
                           bool constructor = false,
                           const std::function<void(StringRef)> &paramCallback = nullptr);

/**
 * for builtin method
 * @param funcInfo
 * @param packedParamType
 * @param out
 */
void formatNativeMethodSignature(const NativeFuncInfo *funcInfo, StringRef packedParamType,
                                 std::string &out);

std::string generateHoverContent(const SourceManager &srcMan, const SymbolIndexes &indexes,
                                 const Source &src, const FindDeclResult &result,
                                 bool markup = true);

SymbolKind toSymbolKind(DeclSymbol::Kind kind);

std::string toString(ConstEntry entry);

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_SYMBOL_H
