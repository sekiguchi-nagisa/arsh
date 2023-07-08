/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef YDSH_HANDLE_INFO_H
#define YDSH_HANDLE_INFO_H

namespace ydsh {

// builtin type
#define EACH_HANDLE_INFO_TYPE(OP)                                                                  \
  OP(Void)                                                                                         \
  OP(Any)                                                                                          \
  OP(Value_)                                                                                       \
  OP(Int)                                                                                          \
  OP(Float)                                                                                        \
  OP(Bool)                                                                                         \
  OP(String)                                                                                       \
  OP(FD)                                                                                           \
  OP(Error)                                                                                        \
  OP(Job)                                                                                          \
  OP(StringIter)                                                                                   \
  OP(Regex)                                                                                        \
  OP(Signal)                                                                                       \
  OP(Signals)                                                                                      \
  OP(Module)                                                                                       \
  OP(Reader)                                                                                       \
  OP(Command)                                                                                      \
  OP(LineEditor)

// type template
#define EACH_HANDLE_INFO_TYPE_TEMP(OP)                                                             \
  OP(Array)                                                                                        \
  OP(Map)                                                                                          \
  OP(Tuple)                                                                                        \
  OP(Option)

// func type
#define EACH_HANDLE_INFO_FUNC_TYPE(OP) OP(Func)

// param types num
#define EACH_HANDLE_INFO_NUM(OP)                                                                   \
  OP(P_N0)                                                                                         \
  OP(P_N1)                                                                                         \
  OP(P_N2)                                                                                         \
  OP(P_N3)                                                                                         \
  OP(P_N4)                                                                                         \
  OP(P_N5)                                                                                         \
  OP(P_N6)                                                                                         \
  OP(P_N7)                                                                                         \
  OP(P_N8)

// parametric type
#define EACH_HANDLE_INFO_PTYPE(OP)                                                                 \
  OP(T0)                                                                                           \
  OP(T1)

#define EACH_HANDLE_INFO(OP)                                                                       \
  EACH_HANDLE_INFO_TYPE(OP)                                                                        \
  EACH_HANDLE_INFO_TYPE_TEMP(OP)                                                                   \
  EACH_HANDLE_INFO_FUNC_TYPE(OP)                                                                   \
  EACH_HANDLE_INFO_NUM(OP)                                                                         \
  EACH_HANDLE_INFO_PTYPE(OP)

/*
 * encoded type definition
 * ex. function hoge(a : Int, b = "re", c : Boolean, d = 2.3) : Int
 * --> INT_T P_N4 INT_T STRING_T BOOL_T FLOAT_T
 *     defaultValueFlag (00001010)
 * ex. constructor(a : Array<Int>, b : T1)
 * --> VOID_T P_N2 ARRAY_T P_N1 INT_T T1
 *     defaultValueFlag (00000000)
 */
enum class HandleInfo : char {
#define GEN_ENUM(ENUM) ENUM,
  EACH_HANDLE_INFO(GEN_ENUM)
#undef GEN_ENUM
};

/**
 * for method handle creation.
 */
struct NativeFuncInfo {
  const char *funcName;

  const char *params;

  /**
   * serialized function handle
   */
  const HandleInfo handleInfo[24];
};

const NativeFuncInfo *nativeFuncInfoTable();

struct native_type_info_t {
  unsigned short offset;

  unsigned short methodSize;

  unsigned int getActualMethodIndex(unsigned int index) const { return this->offset + index; }

  const NativeFuncInfo &getMethodInfo(unsigned int index) const {
    return nativeFuncInfoTable()[this->getActualMethodIndex(index)];
  }

  bool operator==(native_type_info_t info) const {
    return this->offset == info.offset && this->methodSize == info.methodSize;
  }

  bool operator!=(native_type_info_t info) const { return !(*this == info); }
};

} // namespace ydsh

#endif // YDSH_HANDLE_INFO_H
