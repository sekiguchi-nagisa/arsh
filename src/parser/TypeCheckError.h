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

#ifndef PARSER_TYPECHECKERROR_H_
#define PARSER_TYPECHECKERROR_H_

#include <string>
#include <vector>

#include "../core/TypeLookupError.h"

#define EACH_TC_ERROR0(E) \
    E(Unresolved     , "having unresolved type") \
    E(InsideLoop     , "only available inside loop statement") \
    E(UnfoundReturn  , "not found return statement") \
    E(Unreachable    , "found unreachable code") \
    E(InsideFunc     , "only available inside function") \
    E(NotNeedExpr    , "not need expression") \
    E(Assignable     , "require assignable node") \
    E(ReadOnly       , "read only value") \
    E(InsideFinally  , "unavailable inside finally block") \
    E(UnneedNamedArg , "not need named argument") \
    E(NeedNamedArg   , "need named argument") \
    E(NoDefaultValue , "has no default value") \
    E(OutsideToplevel, "only available toplevel scope") \
    E(NotCallable    , "Func type object is not callable")

#define EACH_TC_ERROR1(E) \
    E(DefinedSymbol    , "already defined symbol: ") \
    E(DefinedField     , "already defiend field: ") \
    E(UndefinedSymbol  , "undefined symbol: ") \
    E(UndefinedField   , "undefined field: ") \
    E(UndefinedMethod  , "undefined method: ") \
    E(UndefinedInit    , "undefined constructor: ") \
    E(Unacceptable     , "unacceptable type: ") \
    E(NoIterator       , "not support iterator: ") \
    E(UnfoundNamedParam, "undefined parameter name: ") \
    E(DupNamedArg      , "found duplicated named argument: ") \
    E(DefinedCmd       , "already defined command: ") \
    E(Unimplemented    , "unimplemented type checker api: ")

#define EACH_TC_ERROR2(E) \
    E(Required    , "require ", " but is ") \
    E(CastOp      , "unsupported cast op: ", " -> ") \
    E(UnaryOp     , "undefined operator: ", " ") \
    E(UnmatchParam, "not match parameter, require size is ", ", but is ")

#define EACH_TC_ERROR3(E) \
    E(BinaryOp, "undefined operator: ", " ", " ")

namespace ydsh {
namespace ast {

class Node;

}
}


namespace ydsh {
namespace parser {

/**
 * for type error reporting
 */
class TypeCheckError {
private:
    /**
     * line number of error node
     */
    unsigned int lineNum;

    const char *kind;

    std::string message;

public:
    TypeCheckError(unsigned int lineNum, const char *kind, std::string &&message) :
            lineNum(lineNum), kind(kind), message(std::move(message)) { }

    TypeCheckError(unsigned int lineNum, core::TypeLookupError &e) :
            lineNum(lineNum), kind(e.getKind()), message(e.moveMessage()) { }

    ~TypeCheckError() = default;

    unsigned int getLineNum() const {
        return this->lineNum;
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

    void operator()(ast::Node *node) const throw(TypeCheckError);
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

    void operator()(ast::Node *node, const std::string &arg1) const throw(TypeCheckError);
};

class ErrorMessage2 : public ErrorMessage {
private:
    const char *msgPart1;
    const char *msgPart2;

public:
    constexpr ErrorMessage2(const char *kind, const char *msgPart1,
                            const char *msgPart2) :
            ErrorMessage(kind), msgPart1(msgPart1), msgPart2(msgPart2) { }

    ErrorMessage2(const ErrorMessage2 &e) = delete;
    ErrorMessage2(ErrorMessage2 &&e) = delete;

    ~ErrorMessage2() = default;

    ErrorMessage2 &operator=(const ErrorMessage2 &e) = delete;
    ErrorMessage2 &operator=(ErrorMessage2 &&e) = delete;

    const char *getMsgPart1() const {
        return this->msgPart1;
    }

    const char *getMsgPart2() const {
        return this->msgPart2;
    }

    void operator()(ast::Node *node, const std::string &arg1,
                    const std::string &arg2) const throw(TypeCheckError);
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

    void operator()(ast::Node *node, const std::string &arg1,
                    const std::string &arg2, const std::string &arg3) const throw(TypeCheckError);
};

// define function object for error reporting

#define GEN_VAR(ENUM, S1) constexpr ErrorMessage0 E_##ENUM(#ENUM, S1);
EACH_TC_ERROR0(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1) constexpr ErrorMessage1 E_##ENUM(#ENUM, S1);
EACH_TC_ERROR1(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2) constexpr ErrorMessage2 E_##ENUM(#ENUM, S1, S2);
EACH_TC_ERROR2(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2, S3) constexpr ErrorMessage3 E_##ENUM(#ENUM, S1, S2, S3);
EACH_TC_ERROR3(GEN_VAR)
#undef GEN_VAR

} // namespace parser
} // namespace ydsh

// undef macro
#ifndef IN_SRC_FILE
#undef EACH_TC_ERROR0
#undef EACH_TC_ERROR1
#undef EACH_TC_ERROR2
#undef EACH_TC_ERROR3
#endif


#endif /* PARSER_TYPECHECKERROR_H_ */
