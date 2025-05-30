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

#include "format_signature.h"
#include "misc/format.hpp"
#include "misc/num_util.hpp"
#include "type.h"

namespace arsh {

static void normalizeTypeName(StringRef typeName, std::string &out) {
  unsigned int count = 0;
  // ex. (%mod23.Interval, %mod23, %mod45.Interval) => (Interval, %mod32, Interval)
  splitByDelim(typeName, "%mod", [&out, &count](StringRef sub, bool) {
    if (count++ > 0) {
      unsigned int offset = 0;
      for (; offset < sub.size(); offset++) {
        if (isDecimal(sub[offset])) {
          continue;
        }
        break;
      }
      if (offset < sub.size() && sub[offset] == '.') {
        sub = sub.substr(++offset);
      }
    }
    out += sub;
    return true;
  });
}

void normalizeTypeName(const Type &type, std::string &out) {
  if (type.typeKind() == TypeKind::Builtin) { // fast path
    out += type.getNameRef();
  } else {
    normalizeTypeName(type.getNameRef(), out);
  }
}

static std::vector<StringRef> splitParamNames(StringRef packedParamNames) {
  std::vector<StringRef> params;
  params.reserve(4);
  iteratePackedParamNames(packedParamNames, [&params](StringRef p, bool) {
    if (!p.empty()) {
      params.push_back(p);
    }
    return true;
  });
  return params;
}

void formatVarSignature(const Type &type, std::string &out) {
  out += ": ";
  normalizeTypeName(type, out);
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
    normalizeTypeName(funcType.getParamTypeAt(i), out);
    if (paramCallback) {
      paramCallback(StringRef(out.c_str() + offset));
    }
  }
  out += "): ";
  normalizeTypeName(funcType.getReturnType(), out);
}

void formatFuncSignature(const Type &retType, unsigned int paramSize, const Type *const *paramTypes,
                         std::string &out, const std::function<void(StringRef)> &paramCallback) {
  out += "(";
  for (unsigned int i = 0; i < paramSize; i++) {
    if (i > 0) {
      out += ", ";
    }
    const size_t offset = out.size();
    out += "p";
    out += std::to_string(i);
    out += ": ";
    normalizeTypeName(*paramTypes[i], out);
    if (paramCallback) {
      paramCallback(StringRef(out.c_str() + offset));
    }
  }
  out += "): ";
  normalizeTypeName(retType, out);
}

void formatFieldSignature(const Type &recvType, const Type &type, std::string &out) {
  out += ": ";
  normalizeTypeName(type, out);
  out += " for ";
  normalizeTypeName(recvType, out);
}

void formatMethodSignature(const Type *recvType, const MethodHandle &handle, std::string &out,
                           const std::function<void(StringRef)> &paramCallback) {
  if (recvType && handle.isEqOrOrdMethod()) {
    formatNativeMethodSignature(handle.getIndex(), recvType->getNameRef(), out, paramCallback);
    return;
  }
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
    normalizeTypeName(handle.getParamTypeAt(i), out);
    if (paramCallback) {
      paramCallback(StringRef(out.c_str() + offset));
    }
  }
  out += ")";
  if (recvType) {
    out += ": ";
    normalizeTypeName(handle.getReturnType(), out);
    out += " for ";
    normalizeTypeName(*recvType, out);
  }
}

class Decoder {
private:
  const HandleInfo *ptr;
  const std::vector<StringRef> paramTypes;

public:
  Decoder(const HandleInfo *ptr, std::vector<StringRef> &&types)
      : ptr(ptr), paramTypes(std::move(types)) {}

  unsigned int decodeNum() {
    return static_cast<unsigned int>(static_cast<int>(*(this->ptr++)) -
                                     static_cast<int>(HandleInfo::P_N0));
  }

  std::string decodeType(bool replaceIface);

  /**
   * consume constraint part
   */
  void decodeConstraint() {
    unsigned int size = this->decodeNum();
    for (unsigned int i = 0; i < size; i++) {
      this->decodeType(false);
      this->decodeType(false);
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

std::string Decoder::decodeType(bool replaceIface) {
  switch (const auto info = *(this->ptr++); info) {
#define GEN_CASE(E)                                                                                \
  case HandleInfo::E:                                                                              \
    return #E;
    EACH_HANDLE_INFO_TYPE_PUBLIC(GEN_CASE)
#undef GEN_CASE

#define GEN_CASE(E)                                                                                \
  case HandleInfo::E:                                                                              \
    return "%" #E;
    EACH_HANDLE_INFO_TYPE_HIDDEN(GEN_CASE)
#undef GEN_CASE

#define GEN_CASE(E)                                                                                \
  case HandleInfo::E:                                                                              \
    if (replaceIface) {                                                                            \
      return this->paramTypes[0].toString();                                                       \
    }                                                                                              \
    return "%" #E;
    EACH_HANDLE_INFO_TYPE_HIDDEN_IFACE(GEN_CASE)
#undef GEN_CASE

  case HandleInfo::Array: {
    unsigned int size = this->decodeNum();
    (void)size;
    assert(size == 1);
    std::string out = "[";
    out += this->decodeType(replaceIface);
    out += "]";
    return out;
  }
  case HandleInfo::Map: {
    unsigned int size = this->decodeNum();
    (void)size;
    assert(size == 2);
    std::string out = "[";
    out += this->decodeType(replaceIface);
    out += " : ";
    out += this->decodeType(replaceIface);
    out += "]";
    return out;
  }
  case HandleInfo::Tuple: {
    unsigned int size = this->decodeNum();
    assert(size > 0);
    std::string out = "(";
    if (size == 1) {
      out += this->decodeType(replaceIface);
      out += ",";
    } else {
      for (unsigned int i = 0; i < size; i++) {
        if (i > 0) {
          out += ", ";
        }
        out += this->decodeType(replaceIface);
      }
    }
    out += ")";
    return out;
  }
  case HandleInfo::Option: {
    unsigned int size = this->decodeNum();
    (void)size;
    assert(size == 1);
    std::string out = this->decodeType(replaceIface);
    if (isFuncType(out)) {
      out.insert(0, "(") += ")";
    }
    out += "?";
    return out;
  }
  case HandleInfo::Func: {
    std::string ret = this->decodeType(replaceIface);
    unsigned int size = this->decodeNum();
    std::string out = "(";
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        out += ", ";
      }
      out += this->decodeType(replaceIface);
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

void formatNativeMethodSignature(unsigned int nativeMethodIndex, StringRef packedParamType,
                                 std::string &out,
                                 const std::function<void(StringRef)> &paramCallback) {
  const bool eqOrOrd = !packedParamType.empty() && isEqOrOrdTypeMethod(nativeMethodIndex);
  const auto *funcInfo = &nativeFuncInfoTable()[nativeMethodIndex];
  const auto params = splitParamNames(funcInfo->params);
  Decoder decoder(funcInfo->handleInfo, splitParamNames(packedParamType));

  decoder.decodeConstraint(); // ignore constraint

  auto returnTypeName = decoder.decodeType(eqOrOrd);
  unsigned int paramSize = decoder.decodeNum();
  assert(paramSize > 0);
  paramSize--; // ignore receiver
  assert(paramSize == params.size());
  auto recvTypeName = decoder.decodeType(eqOrOrd);

  out += "(";
  for (unsigned int i = 0; i < paramSize; i++) {
    if (i > 0) {
      out += ", ";
    }
    const size_t offset = out.size();
    out += params[i];
    out += ": ";
    normalizeTypeName(decoder.decodeType(eqOrOrd), out);
    if (paramCallback) {
      paramCallback(StringRef(out.c_str() + offset));
    }
  }
  out += ")";
  if (StringRef(funcInfo->funcName) != OP_INIT) { // method
    out += ": ";
    normalizeTypeName(returnTypeName, out);
    out += " for ";
    normalizeTypeName(recvTypeName, out);
  }
}

} // namespace arsh
