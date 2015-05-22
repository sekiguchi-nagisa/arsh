//
// Created by sekiguchi nagisa on 15/05/21.
//

#ifndef YDSH_TYPE_UTIL_H
#define YDSH_TYPE_UTIL_H

#include "../ast/TypeToken.h"

namespace ydsh {
namespace ast {

void addElement(std::unique_ptr<ReifiedTypeToken> &reified) {
}

template <typename F, typename... R>
void addElement(std::unique_ptr<ReifiedTypeToken> &reified, F&& type, R&&... reset) {
    reified->addElementTypeToken(type.release());
    addElement(reified, std::forward<R>(reset)...);
}

template <typename... T>
std::unique_ptr<TypeToken> reified(const char *name, T&&... elementTypes) {
    std::unique_ptr<ReifiedTypeToken> reified(
            new ReifiedTypeToken(new ClassTypeToken(0, std::string(name))));
    addElement(reified, std::forward<T>(elementTypes)...);
    return std::move(reified);
}


void addParamType(std::unique_ptr<FuncTypeToken> &func) {
}

template <typename F, typename... R>
void addParamType(std::unique_ptr<FuncTypeToken> &func, F&& type, R&&... reset) {
    func->addParamTypeToken(type.release());
    addParamType(func, std::forward<R>(reset)...);
}

template <typename... T>
std::unique_ptr<TypeToken> func(std::unique_ptr<TypeToken> &&returnType, T&&... paramTypes) {
    std::unique_ptr<FuncTypeToken> func(new FuncTypeToken(returnType.release()));
    addParamType(func, std::forward<T>(paramTypes)...);
    return std::move(func);
}

inline std::unique_ptr<TypeToken> type(const char *name, unsigned int lineNum = 0) {
    return std::unique_ptr<TypeToken>(new ClassTypeToken(lineNum, std::string(name)));
}

} // namespace ast
} // namespace ydsh




#endif //YDSH_TYPE_UTIL_H
