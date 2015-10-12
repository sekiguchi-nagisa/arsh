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

#ifndef YDSH_TYPELOOKUPERROR_HPP
#define YDSH_TYPELOOKUPERROR_HPP

#include <string>
#include <ostream>

#include "MessageTemplate.hpp"

namespace ydsh {
namespace core {

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

template <unsigned int N>
class TLErrorMessage : public MessageTemplate {
public:
    constexpr TLErrorMessage(const char *kind, const char *value) :
            MessageTemplate(kind, value) { }

    template <typename ... T>
    void operator()(T && ... args) const throw(TypeLookupError) {
        static_assert(N == sizeof ... (T), "invalid parameter size");

        throw TypeLookupError(this->kind, this->format(std::forward<T&&>(args)...));
    }
};

// define function object for error reporting

#define DEFINE_TL_ERROR(NAME, MSG) constexpr TLErrorMessage<computeParamSize(MSG)> E_##NAME(#NAME, MSG)

DEFINE_TL_ERROR(TupleElement   , "Tuple type require at least 2 type element");
DEFINE_TL_ERROR(NotUseGeneric  , "not directly use generic base type: %");
DEFINE_TL_ERROR(UndefinedType  , "undefined type: %");
DEFINE_TL_ERROR(NotGenericBase , "unsupported type template: %");
DEFINE_TL_ERROR(NotPrimitive   , "not primitive type: %");
DEFINE_TL_ERROR(NotClass       , "not class type: %");
DEFINE_TL_ERROR(Nonheritable   , "nonheritable type: %");
DEFINE_TL_ERROR(DefinedType    , "already defined type: %");
DEFINE_TL_ERROR(InvalidElement , "invalid type element: %");
DEFINE_TL_ERROR(NoDBusInterface, "not found dbus interface: %");
DEFINE_TL_ERROR(UnmatchElement , "not match type element, % requires % type element, but is %");

#undef DEFINE_TL_ERROR

} // namespace core
} // namespace ydsh


#endif //YDSH_TYPELOOKUPERROR_HPP
