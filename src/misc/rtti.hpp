/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef MISC_LIB_RTTI_HPP
#define MISC_LIB_RTTI_HPP

#include <cassert>

#include "detect.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

/**
 * for LLVM-style RTTI
 * see. https://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html
 */

template <typename To, typename From, enable_when<std::is_base_of<From, To>::value> = nullptr>
inline bool isa(const From *obj) {
  return obj != nullptr && To::classof(obj);
}

template <typename To, typename From, enable_when<std::is_base_of<From, To>::value> = nullptr>
inline bool isa(const From &obj) {
  return To::classof(&obj);
}

template <typename To, typename From, enable_when<std::is_base_of<From, To>::value> = nullptr>
inline To *cast(From *obj) {
  assert(isa<To>(obj));
  return static_cast<To *>(obj);
}

template <typename To, typename From, enable_when<std::is_base_of<From, To>::value> = nullptr>
inline const To *cast(const From *obj) {
  assert(isa<To>(obj));
  return static_cast<const To *>(obj);
}

template <typename To, typename From, enable_when<std::is_base_of<From, To>::value> = nullptr>
inline To &cast(From &obj) {
  assert(isa<To>(obj));
  return static_cast<To &>(obj);
}

template <typename To, typename From, enable_when<std::is_base_of<From, To>::value> = nullptr>
inline const To &cast(const From &obj) {
  assert(isa<To>(obj));
  return static_cast<const To &>(obj);
}

template <typename To, typename From, enable_when<std::is_base_of<From, To>::value> = nullptr>
inline To *checked_cast(From *obj) {
  return isa<To>(obj) ? cast<To>(obj) : nullptr;
}

template <typename To, typename From, enable_when<std::is_base_of<From, To>::value> = nullptr>
inline const To *checked_cast(const From *obj) {
  return isa<To>(obj) ? cast<To>(obj) : nullptr;
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_RTTI_HPP
