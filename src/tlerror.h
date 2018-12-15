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

#ifndef YDSH_TLERROR_H
#define YDSH_TLERROR_H

#include <string>

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

} // namespace ydsh

#endif //YDSH_TLERROR_H
