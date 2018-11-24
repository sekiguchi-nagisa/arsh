/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
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
    TypeLookupError(const char *kind, const char *message) :
            kind(kind), message(message) { }

    ~TypeLookupError() = default;

    const char *getKind() const {
        return this->kind;
    }

    const std::string &getMessage() const {
        return this->message;
    }

    friend std::string extract(TypeLookupError &&e) {
        return std::move(e.message);
    }
};

struct TLError {};

template <typename T, typename B>
using base_of_t = typename std::enable_if<std::is_base_of<B, T>::value, T>::type;


#define DEFINE_TLError(E, fmt) \
struct E : TLError { \
    static constexpr const char *kind = #E; \
    static constexpr const char *value = fmt; }

DEFINE_TLError(UndefinedType,   "undefined type: `%s'");
DEFINE_TLError(NotTemplate,     "illegal type template: %s");
DEFINE_TLError(DefinedType,     "already defined type: `%s'");
DEFINE_TLError(InvalidElement,  "invalid type element: `%s'");
DEFINE_TLError(UnmatchElement,  "not match type element, `%s' requires %d type element, but is %d");

#undef DEFINE_TLError

TypeLookupError createTLErrorImpl(const char *kind, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));

template <typename T, typename ... Arg, typename = base_of_t<T, TLError>>
inline TypeLookupError createTLError(Arg && ...arg) {
    return createTLErrorImpl(T::kind, T::value, std::forward<Arg>(arg)...);
}

#define RAISE_TL_ERROR(e, ...) \
    throw createTLError<e>(__VA_ARGS__)


/**
 * for type error reporting
 */
class TypeCheckError {
private:
    Token token;

    const char *kind;

    std::string message;

public:
    TypeCheckError(Token token, const char *kind, const char *message) :
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
};

struct TCError {};

#define DEFINE_TCError(E, fmt) \
struct E : TCError { \
    static constexpr const char *kind = #E; \
    static constexpr const char *value = fmt; }

DEFINE_TCError(InsideLoop        ,"only available inside loop statement"              );
DEFINE_TCError(UnfoundReturn     ,"not found return statement"                        );
DEFINE_TCError(Unreachable       ,"unreachable code"                                  );
DEFINE_TCError(InsideFunc        ,"only available inside function"                    );
DEFINE_TCError(NotNeedExpr       ,"not need expression"                               );
DEFINE_TCError(Assignable        ,"require assignable expression"                     );
DEFINE_TCError(ReadOnly          ,"read only symbol"                                  );
DEFINE_TCError(InsideFinally     ,"unavailable inside finally block"                  );
DEFINE_TCError(InsideChild       ,"unavailable inside child process"                  );
DEFINE_TCError(OutsideToplevel   ,"only available top level scope"                    );
DEFINE_TCError(NotCallable       ,"Func type object is not directly callable"         );
DEFINE_TCError(UselessBlock      ,"useless block"                                     );
DEFINE_TCError(EmptyTry          ,"empty try block"                                   );
DEFINE_TCError(MeaninglessTry    ,"meaningless try block"                             );
DEFINE_TCError(LocalLimit        ,"too many local variables"                          );
DEFINE_TCError(DefinedSymbol     ,"already defined symbol: `%s'"                      );
DEFINE_TCError(DefinedField      ,"already defined field: `%s'"                       );
DEFINE_TCError(UndefinedSymbol   ,"undefined symbol: `%s'"                            );
DEFINE_TCError(UndefinedField    ,"undefined field: `%s'"                             );
DEFINE_TCError(UndefinedMethod   ,"undefined method: `%s'"                            );
DEFINE_TCError(UndefinedInit     ,"undefined constructor: `%s'"                       );
DEFINE_TCError(Unacceptable      ,"unacceptable type: `%s'"                           );
DEFINE_TCError(DefinedCmd        ,"already defined command: `%s'"                     );
DEFINE_TCError(ConflictSymbol    ,"at global import, detect symbol conflict: `%s'"    );
DEFINE_TCError(UnresolvedMod     ,"cannot open module: %s, by `%s'"                   );
DEFINE_TCError(CircularMod       ,"circular module import: `%s'"                      );
DEFINE_TCError(Required          ,"require `%s' type, but is `%s' type"               );
DEFINE_TCError(CastOp            ,"unsupported cast op: `%s' type -> `%s' type"       );
DEFINE_TCError(UnmatchParam      ,"not match parameter, require size is %d, but is %d");

#undef DEFINE_TCError


TypeCheckError createTCErrorImpl(const Node &node, const char *kind,
                                 const char *fmt, ...) __attribute__ ((format(printf, 3, 4)));

template <typename T, typename ... Arg, typename = base_of_t<T, TCError>>
inline TypeCheckError createTCError(const Node &node, Arg && ...arg) {
    return createTCErrorImpl(node, T::kind, T::value, std::forward<Arg>(arg)...);
}

#define RAISE_TC_ERROR(e, node,  ...) \
    throw createTCError<e>(node, ## __VA_ARGS__)

} // namespace ydsh

#endif //YDSH_DIAGNOSIS_H
