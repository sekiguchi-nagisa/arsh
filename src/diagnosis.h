/*
 * Copyright (C) 2016-2017 Nagisa Sekiguchi
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

#ifndef YDSH_DIAGNOSIS_H
#define YDSH_DIAGNOSIS_H

#include <string>

#include "node.h"

namespace ydsh {

class TypeLookupError {
private:
    const char *kind;
    std::string message;

public:
    TypeLookupError(const char *kind, const char *message) noexcept :
            kind(kind), message(message) { }

    ~TypeLookupError() = default;

    const char *getKind() const {
        return this->kind;
    }

    const std::string &getMessage() const {
        return this->message;
    }

    bool operator==(const TypeLookupError &e) const {
        return this->message == e.getMessage();
    }

    friend std::string extract(TypeLookupError &&e) {
        return std::move(e.message);
    }
};

enum class TLError : unsigned int {
    E_UndefinedType,
    E_NotTemplate,
    E_DefinedType,
    E_InvalidElement,
    E_NoDBusInterface,
    E_UnmatchElement,
};

#define UndefinedType   "undefined type: `%s'"
#define NotTemplate     "illegal type template: %s"
#define DefinedType     "already defined type: `%s'"
#define InvalidElement  "invalid type element: `%s'"
#define NoDBusInterface "not found D-Bus interface: %s"
#define UnmatchElement  "not match type element, `%s' requires %d type element, but is %d"


TypeLookupError createTLError(TLError e, const char *kind, const char *fmt, ...) __attribute__ ((format(printf, 3, 4)));

#define RAISE_TL_ERROR(e, ...) \
    throw createTLError(TLError::E_ ## e, #e, e, ## __VA_ARGS__)


/**
 * for type error reporting
 */
class TypeCheckError {
private:
    Token token;

    const char *kind;

    std::string message;

public:
    TypeCheckError(Token token, const char *kind, const char *message) noexcept :
            token(token), kind(kind), message(message) { }

    TypeCheckError(Token token, TypeLookupError &e) noexcept :
            token(token), kind(e.getKind()), message(extract(std::move(e))) { }

    ~TypeCheckError() = default;

    Token getToken() const {
        return this->token;
    }

    const char *getKind() const {
        return this->kind;
    }

    const std::string &getMessage() const {
        return this->message;
    }

    bool operator==(const TypeCheckError &e) const {
        return this->message == e.getMessage();
    }
};

enum class TCError : unsigned int {
    E_InsideLoop,
    E_UnfoundReturn,
    E_Unreachable,
    E_InsideFunc,
    E_NotNeedExpr,
    E_Assignable,
    E_ReadOnly,
    E_InsideFinally,
    E_InsideChild,
    E_OutsideToplevel,
    E_NotCallable,
    E_DisallowTypeof,
    E_UselessBlock,
    E_EmptyTry,
    E_UselessTry,
    E_LocalLimit,
    E_DefinedSymbol,
    E_DefinedField,
    E_UndefinedSymbol,
    E_UndefinedField,
    E_UndefinedMethod,
    E_UndefinedInit,
    E_Unacceptable,
    E_DefinedCmd,
    E_ConflictSymbol,
    E_UnresolvedMod,
    E_CircularMod,
    E_NotMod,
    E_Required,
    E_CastOp,
    E_UnmatchParam,
};

#define InsideLoop        "only available inside loop statement"
#define UnfoundReturn     "not found return statement"
#define Unreachable       "unreachable code"
#define InsideFunc        "only available inside function"
#define NotNeedExpr       "not need expression"
#define Assignable        "require assignable expression"
#define ReadOnly          "read only symbol"
#define InsideFinally     "unavailable inside finally block"
#define InsideChild       "unavailable inside child process"
#define OutsideToplevel   "only available top level scope"
#define NotCallable       "Func type object is not directly callable"
#define DisallowTypeof    "not allow typeof operator"
#define UselessBlock      "useless block"
#define EmptyTry          "empty try block"
#define UselessTry        "useless try block"
#define LocalLimit        "too many local variable"
#define DefinedSymbol     "already defined symbol: %s"
#define DefinedField      "already defined field: %s"
#define UndefinedSymbol   "undefined symbol: %s"
#define UndefinedField    "undefined field: %s"
#define UndefinedMethod   "undefined method: %s"
#define UndefinedInit     "undefined constructor: %s"
#define Unacceptable      "unacceptable type: `%s'"
#define DefinedCmd        "already defined command: %s"
#define ConflictSymbol    "at global import, detect symbol conflict: `%s'"
#define UnresolvedMod     "unresolved module: %s"
#define CircularMod       "circular module import: %s"
#define NotMod            "unavailable module: %s, by `%s'"
#define Required          "require `%s' type, but is `%s' type"
#define CastOp            "unsupported cast op: `%s' type -> `%s' type"
#define UnmatchParam      "not match parameter, require size is %d, but is %d"


TypeCheckError createTCError(TCError e, const Node &node,
                             const char *kind, const char *fmt, ...) __attribute__ ((format(printf, 4, 5)));

#define RAISE_TC_ERROR(e, node,  ...) \
    throw createTCError(TCError::E_ ## e, node, #e, e, ## __VA_ARGS__)

} // namespace ydsh

#endif //YDSH_DIAGNOSIS_H
