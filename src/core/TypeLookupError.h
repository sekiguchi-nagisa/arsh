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

#ifndef CORE_TYPELOOKUPERROR_H_
#define CORE_TYPELOOKUPERROR_H_

#include <string>
#include <ostream>

#define EACH_TL_ERROR0(E) \
    E(TupleElement, "Tuple type require at least 2 type element")

#define EACH_TL_ERROR1(E) \
    E(NotUseGeneric  , "not directly use generic base type: ") \
    E(UndefinedType  , "undefined type: ") \
    E(NotGenericBase , "unsupported type template: ") \
    E(NotPrimitive   , "not primitive type: ") \
    E(NotClass       , "not class type: ") \
    E(Nonheritable   , "nonheritable type: ") \
    E(DefinedType    , "already defined type: ") \
    E(InvalidElement , "invalid type element: ") \
    E(NoDBusInterface, "not found dbus interface: ")

#define EACH_TL_ERROR3(E) \
    E(UnmatchElement, "not match type element, ", " requires ", " type element, but is ")


namespace ydsh {
namespace core {

class TypeLookupError {
public:
    enum ErrorKind0 {
#define GEN_ENUM(ENUM, STR) ENUM,
        EACH_TL_ERROR0(GEN_ENUM)
#undef GEN_ENUM
    };

    enum ErrorKind1 {
#define GEN_ENUM(ENUM, STR) ENUM,
        EACH_TL_ERROR1(GEN_ENUM)
#undef GEN_ENUM
    };

    enum ErrorKind3 {
#define GEN_ENUM(ENUM, STR1, STR2, STR3) ENUM,
        EACH_TL_ERROR3(GEN_ENUM)
#undef GEN_ENUM
    };

private:
    const char *kind;
    std::string message;

public:
    explicit TypeLookupError(TypeLookupError::ErrorKind0 k);
    TypeLookupError(TypeLookupError::ErrorKind1 k, const std::string &arg1);
    TypeLookupError(TypeLookupError::ErrorKind3 k, const std::string &arg1,
                    const std::string &arg2, const std::string &arg3);

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

std::ostream &operator<<(std::ostream &stream, TypeLookupError::ErrorKind0 kind);
std::ostream &operator<<(std::ostream &stream, TypeLookupError::ErrorKind1 kind);
std::ostream &operator<<(std::ostream &stream, TypeLookupError::ErrorKind3 kind);

struct ErrorRaiser0 {
    TypeLookupError::ErrorKind0 kind;

    void operator()() throw(TypeLookupError) {
        throw TypeLookupError(this->kind);
    }
};

struct ErrorRaiser1 {
    TypeLookupError::ErrorKind1 kind;

    void operator()(const std::string &arg1) throw(TypeLookupError) {
        throw TypeLookupError(this->kind, arg1);
    }
};

struct ErrorRaiser3 {
    TypeLookupError::ErrorKind3 kind;

    void operator()(const std::string &arg1,
                    const std::string &arg2, const std::string &arg3) throw(TypeLookupError) {
        throw TypeLookupError(this->kind, arg1, arg2, arg3);
    }
};

// define function objects for error reporting

#define GEN_VAR(ENUM, S1) extern ErrorRaiser0 E_##ENUM;
    EACH_TL_ERROR0(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1) extern ErrorRaiser1 E_##ENUM;
    EACH_TL_ERROR1(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2, S3) extern ErrorRaiser3 E_##ENUM;
    EACH_TL_ERROR3(GEN_VAR);
#undef GEN_VAR

} // namespace core
} // namespace ydsh

// undef macro
#ifndef IN_SRC_FILE
#undef EACH_TL_ERROR0
#undef EACH_TL_ERROR1
#undef EACH_TL_ERROR3
#endif


#endif /* CORE_TYPELOOKUPERROR_H_ */
