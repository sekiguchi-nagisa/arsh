/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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

#ifndef YDSH_CORE_DIAGNOSIS_H
#define YDSH_CORE_DIAGNOSIS_H

#include <string>

namespace ydsh {
namespace core {

// TypeLookupError

class TypeLookupError {
private:
    const char *kind;
    std::string message;

public:
    TypeLookupError(const char *kind, std::string &&message) :
            kind(kind), message(std::move(message)) { }

    ~TypeLookupError() = default;

    const char *getKind() const {
        return this->kind;
    }

    const std::string &getMessage() const {
        return this->message;
    }

    /**
     * after call it, message will be empty.
     */
    std::string moveMessage() {
        return std::move(this->message);
    }

    bool operator==(const TypeLookupError &e) const {
        return this->message == e.getMessage();
    }
};

#define EACH_TL_ERROR(E) \
    E(UndefinedType  , "undefined type: %") \
    E(NotTemplate    , "illegal type template: %") \
    E(DefinedType    , "already defined type: %") \
    E(InvalidElement , "invalid type element: %") \
    E(NoDBusInterface, "not found D-Bus interface: %") \
    E(UnmatchElement , "not match type element, % requires % type element, but is %")

enum class TLError : unsigned int {
#define GEN_ENUM(K, M) K,
    EACH_TL_ERROR(GEN_ENUM)
#undef GEN_ENUM
};

const char *getTLErrorKind(TLError e);

constexpr const char *const TL_ERROR_MSG_ARRAY[] = {
#define GEN_MSG(K, M) #M,
        EACH_TL_ERROR(GEN_MSG)
#undef GEN_MSG
};

constexpr const char *getTLErrorMessage(TLError e) {
    return TL_ERROR_MSG_ARRAY[static_cast<unsigned int>(e)];
}


namespace __detail_tl_error {

TypeLookupError createErrorImpl(TLError e, const std::string **v);


inline TypeLookupError createErrorNext(TLError e, const std::string **v, unsigned int) {
    return createErrorImpl(e, v);
}

template <typename ... T>
inline TypeLookupError createErrorNext(TLError e, const std::string **v, unsigned int index,
                                       const std::string &arg, T && ... args) {
    v[index] = &arg;
    return createErrorNext(e, v, index + 1, std::forward<T>(args)...);
}


template <unsigned int N, typename ... T>
inline TypeLookupError createError(TLError e, T && ... args) {
    static_assert(N == sizeof ... (T), "invalid parameter size");
    const std::string *v[sizeof...(args)];
    return createErrorNext(e, v, 0, std::forward<T>(args)...);
};

constexpr unsigned int computeParamSize(const char *s, unsigned int index = 0) {
    return s[index] == '\0' ? 0 :
           (s[index] == '%' ? 1 : 0) + computeParamSize(s, index + 1);
}

} // namespace __detail_tl_error

} // namespace core
} // namespace ydsh

#define RAISE_TL_ERROR(e, ...) \
do { \
    using namespace ydsh::core::__detail_tl_error;\
    throw createError<computeParamSize(getTLErrorMessage(TLError::e))>(TLError::e, ## __VA_ARGS__);\
} while(false)


#include "../ast/node.h"

namespace ydsh {
namespace parser {

/**
 * for type error reporting
 */
class TypeCheckError {
private:
    Token token;

    const char *kind;

    std::string message;

public:
    TypeCheckError(Token token, const char *kind, std::string &&message) :
            token(token), kind(kind), message(std::move(message)) { }

    TypeCheckError(Token token, core::TypeLookupError &e) :
            token(token), kind(e.getKind()), message(e.moveMessage()) { }

    ~TypeCheckError() = default;

    unsigned int getStartPos() const {
        return this->token.pos;
    }

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

#define EACH_TC_ERROR(E) \
    E(InsideLoop       , "only available inside loop statement") \
    E(UnfoundReturn    , "not found return statement") \
    E(Unreachable      , "unreachable code") \
    E(InsideFunc       , "only available inside function") \
    E(NotNeedExpr      , "not need expression") \
    E(Assignable       , "require assignable expression") \
    E(ReadOnly         , "read only symbol") \
    E(InsideFinally    , "unavailable inside finally block") \
    E(OutsideToplevel  , "only available top level scope") \
    E(NotCallable      , "Func type object is not directly callable") \
    E(DisallowTypeof   , "not allow typeof operator") \
    E(DefinedSymbol    , "already defined symbol: %") \
    E(DefinedField     , "already defined field: %") \
    E(UndefinedSymbol  , "undefined symbol: %") \
    E(UndefinedField   , "undefined field: %") \
    E(UndefinedMethod  , "undefined method: %") \
    E(UndefinedInit    , "undefined constructor: %") \
    E(Unacceptable     , "unacceptable type: %") \
    E(DefinedCmd       , "already defined command: %") \
    E(Required         , "require %, but is %") \
    E(CastOp           , "unsupported cast op: % -> %") \
    E(UnmatchParam     , "not match parameter, require size is %, but is %")

enum class TCError : unsigned int {
#define GEN_ENUM(K, M) K,
    EACH_TC_ERROR(GEN_ENUM)
#undef GEN_ENUM
};

const char *getTCErrorKind(TCError e);

constexpr const char *const TC_ERROR_MSG_ARRAY[] = {
#define GEN_MSG(K, M) #M,
        EACH_TC_ERROR(GEN_MSG)
#undef GEN_MSG
};

constexpr const char *getTCErrorMessage(TCError e) {
    return TC_ERROR_MSG_ARRAY[static_cast<unsigned int>(e)];
}


namespace __detail_tc_error {

TypeCheckError createErrorImpl(TCError e, const ast::Node &node, const std::string **v);


inline TypeCheckError createTCErrorNext(TCError e, const ast::Node &node, const std::string **v, unsigned int) {
    return createErrorImpl(e, node, v);
}

template <typename ... T>
inline TypeCheckError createTCErrorNext(TCError e, const ast::Node &node, const std::string **v,
                                        unsigned int index, const std::string &arg, T && ... args) {
    v[index] = &arg;
    return createTCErrorNext(e, node, v, index + 1, std::forward<T>(args)...);
}


template <unsigned int N, typename ... T>
inline TypeCheckError createTCError(TCError e, const ast::Node &node, T && ... args) {
    static_assert(N == sizeof ... (T), "invalid parameter size");
    const std::string *v[sizeof...(args)];
    return createTCErrorNext(e, node, v, 0, std::forward<T>(args)...);
};

} // namespace __detail_tc_error

} // namespace parser
} // namespace ydsh

#define RAISE_TC_ERROR(e, node,  ...) \
do { \
    using namespace ydsh::parser::__detail_tc_error;\
    using namespace ydsh::core::__detail_tl_error;\
    constexpr auto s = computeParamSize(getTCErrorMessage(TCError::e));\
    throw createTCError<s>(TCError::e, node, ## __VA_ARGS__);\
} while(false)

#endif //YDSH_CORE_DIAGNOSIS_H
