/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef YDSH_TYPE_UTIL_H
#define YDSH_TYPE_UTIL_H

#include "../ast/TypeToken.h"

namespace ydsh {
namespace ast {

std::unique_ptr<TypeToken> addRestElements(std::unique_ptr<ReifiedTypeToken> &&reified) {
    return std::move(reified);
}

template <typename... T>
std::unique_ptr<TypeToken> addRestElements(std::unique_ptr<ReifiedTypeToken> &&reified,
                                           std::unique_ptr<TypeToken>&& type, T&&... rest) {
    reified->addElementTypeToken(type.release());
    return addRestElements(std::move(reified), std::forward<T>(rest)...);
}


template <typename... T>
std::unique_ptr<TypeToken> reified(const char *name, std::unique_ptr<TypeToken> &&first, T&&... rest) {
    std::unique_ptr<ReifiedTypeToken> reified(
            new ReifiedTypeToken(new ClassTypeToken(0, std::string(name))));
    reified->addElementTypeToken(first.release());
    return addRestElements(std::move(reified), std::forward<T>(rest)...);
}


std::unique_ptr<TypeToken> addParamType(std::unique_ptr<FuncTypeToken> &&func) {
    return std::move(func);
}

template <typename... T>
std::unique_ptr<TypeToken> addParamType(std::unique_ptr<FuncTypeToken> &&func,
                                        std::unique_ptr<TypeToken>&& type, T&&... rest) {
    func->addParamTypeToken(type.release());
    return addParamType(std::move(func), std::forward<T>(rest)...);
}

template <typename... T>
std::unique_ptr<TypeToken> func(std::unique_ptr<TypeToken> &&returnType, T&&... paramTypes) {
    std::unique_ptr<FuncTypeToken> func(new FuncTypeToken(returnType.release()));
    return addParamType(std::move(func), std::forward<T>(paramTypes)...);
}

inline std::unique_ptr<TypeToken> type(const char *name, unsigned int lineNum = 0) {
    return std::unique_ptr<TypeToken>(new ClassTypeToken(lineNum, std::string(name)));
}

inline std::unique_ptr<TypeToken> array(std::unique_ptr<TypeToken> &&type) {
    return reified("Array", std::move(type));
}

inline std::unique_ptr<TypeToken> map(std::unique_ptr<TypeToken> &&keyType, std::unique_ptr<TypeToken> &&valueType) {
    return reified("Map", std::move(keyType), std::move(valueType));
}

template <typename... T>
std::unique_ptr<TypeToken> tuple(std::unique_ptr<TypeToken> &&first, T&&... rest) {
    return reified("Tuple", std::move(first), std::forward<T>(rest)...);
}

} // namespace ast
} // namespace ydsh




#endif //YDSH_TYPE_UTIL_H
