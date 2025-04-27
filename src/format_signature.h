/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#ifndef ARSH_FORMAT_SIGNATURE_H
#define ARSH_FORMAT_SIGNATURE_H

#include <functional>
#include <string>

#include "misc/string_ref.hpp"

namespace arsh {

class Type;
class FunctionType;
class FuncHandle;
class MethodHandle;
struct NativeFuncInfo;

void normalizeTypeName(const Type &type, std::string &out);

inline std::string normalizeTypeName(const Type &type) {
  std::string out;
  normalizeTypeName(type, out);
  return out;
}

void formatVarSignature(const Type &type, std::string &out);

void formatFuncSignature(const FunctionType &funcType, const FuncHandle &handle, std::string &out,
                         const std::function<void(StringRef)> &paramCallback = nullptr);

void formatFuncSignature(const Type &retType, unsigned int paramSize, const Type *const *paramTypes,
                         std::string &out,
                         const std::function<void(StringRef)> &paramCallback = nullptr);

void formatFieldSignature(const Type &recvType, const Type &type, std::string &out);

/**
 *
 * @param recvType may be null if indicate constructor
 * @param handle
 * @param out
 * @param paramCallback
 */
void formatMethodSignature(const Type *recvType, const MethodHandle &handle, std::string &out,
                           const std::function<void(StringRef)> &paramCallback);

inline void formatMethodSignature(const Type &recvType, const MethodHandle &handle,
                                  std::string &out,
                                  const std::function<void(StringRef)> &paramCallback = nullptr) {
  formatMethodSignature(&recvType, handle, out, paramCallback);
}

/**
 * for builtin method
 * @param nativeMethodIndex
 * @param packedParamType
 * must follow `T0:T1` notation
 * @param out
 * @param paramCallback
 */
void formatNativeMethodSignature(unsigned int nativeMethodIndex, StringRef packedParamType,
                                 std::string &out,
                                 const std::function<void(StringRef)> &paramCallback = nullptr);

} // namespace arsh

#endif // ARSH_FORMAT_SIGNATURE_H
