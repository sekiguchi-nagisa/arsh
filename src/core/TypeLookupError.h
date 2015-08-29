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

class ErrorMessage {
protected:
    const char *kind;

public:
    constexpr explicit ErrorMessage(const char *kind) : kind(kind) { }

    ErrorMessage(const ErrorMessage &e) = delete;
    ErrorMessage(ErrorMessage &&e) = delete;

    ~ErrorMessage() = default;

    ErrorMessage &operator=(const ErrorMessage &e) = delete;
    ErrorMessage &operator=(ErrorMessage &&e) = delete;

    const char *getKind() const {
        return this->kind;
    }
};

class ErrorMessage0 : public ErrorMessage {
private:
    const char *msgPart1;

public:
    constexpr ErrorMessage0(const char *kind, const char *msgPart1) :
            ErrorMessage(kind), msgPart1(msgPart1) { }

    ErrorMessage0(const ErrorMessage0 &e) = delete;
    ErrorMessage0(ErrorMessage0 &&e) = delete;

    ~ErrorMessage0() = default;

    ErrorMessage0 &operator=(const ErrorMessage0 &e) = delete;
    ErrorMessage0 &operator=(ErrorMessage0 &&e) = delete;

    const char *getMsgPart1() const {
        return this->msgPart1;
    }

    void operator()() const throw(TypeLookupError);
};

class ErrorMessage1 : public ErrorMessage {
private:
    const char *msgPart1;

public:
    constexpr ErrorMessage1(const char *kind, const char *msgPart1) :
            ErrorMessage(kind), msgPart1(msgPart1) { }

    ErrorMessage1(const ErrorMessage0 &e) = delete;
    ErrorMessage1(ErrorMessage0 &&e) = delete;

    ~ErrorMessage1() = default;

    ErrorMessage1 &operator=(const ErrorMessage1 &e) = delete;
    ErrorMessage1 &operator=(ErrorMessage1 &&e) = delete;

    const char *getMsgPart1() const {
        return this->msgPart1;
    }

    void operator()(const std::string &arg1) const throw(TypeLookupError);
};

class ErrorMessage3 : public ErrorMessage {
private:
    const char *msgPart1;
    const char *msgPart2;
    const char *msgPart3;

public:
    constexpr ErrorMessage3(const char *kind, const char *msgPart1,
                            const char *msgPart2, const char *msgPart3) :
            ErrorMessage(kind), msgPart1(msgPart1), msgPart2(msgPart2), msgPart3(msgPart3) { }

    ErrorMessage3(const ErrorMessage3 &e) = delete;
    ErrorMessage3(ErrorMessage3 &&e) = delete;

    ~ErrorMessage3() = default;

    ErrorMessage3 &operator=(const ErrorMessage3 &e) = delete;
    ErrorMessage3 &operator=(ErrorMessage3 &&e) = delete;

    const char *getMsgPart1() const {
        return this->msgPart1;
    }

    const char *getMsgPart2() const {
        return this->msgPart2;
    }

    const char *getMsgPart3() const {
        return this->msgPart3;
    }

    void operator()(const std::string &arg1, const std::string &arg2,
                    const std::string &arg3) const throw(TypeLookupError);
};

// define function objects for error reporting

#define GEN_VAR(ENUM, S1) constexpr ErrorMessage0 E_##ENUM(#ENUM, S1);
    EACH_TL_ERROR0(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1) constexpr ErrorMessage1 E_##ENUM(#ENUM, S1);
    EACH_TL_ERROR1(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2, S3) constexpr ErrorMessage3 E_##ENUM(#ENUM, S1, S2, S3);
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
