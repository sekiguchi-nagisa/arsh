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

#ifndef PARSER_TYPECHECKERROR_HPP_
#define PARSER_TYPECHECKERROR_HPP_

#include <string>
#include <vector>

#include "../core/TypeLookupError.hpp"
#include "../ast/Node.h"

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

template <unsigned int N>
class TCErrorMessage : public core::MessageTemplate {
public:
    constexpr TCErrorMessage(const char *kind, const char *value) :
            MessageTemplate(kind, value) { }

    template <typename ... T>
    void operator()(ast::Node *node, T && ... args) const throw(TypeCheckError) {
        static_assert(N == sizeof ... (T), "invalid parameter size");

        throw TypeCheckError(node->getLineNum(), this->kind, this->format(std::forward<T &&>(args)...));
    }
};

// define function object for error reporting

#define DEFINE_TC_ERROR(NAME, MSG) constexpr TCErrorMessage<core::computeParamSize(MSG)> E_##NAME(#NAME, MSG)

DEFINE_TC_ERROR(Unresolved       , "having unresolved type");
DEFINE_TC_ERROR(InsideLoop       , "only available inside loop statement");
DEFINE_TC_ERROR(UnfoundReturn    , "not found return statement");
DEFINE_TC_ERROR(Unreachable      , "found unreachable code");
DEFINE_TC_ERROR(InsideFunc       , "only available inside function");
DEFINE_TC_ERROR(NotNeedExpr      , "not need expression");
DEFINE_TC_ERROR(Assignable       , "require assignable node");
DEFINE_TC_ERROR(ReadOnly         , "read only value");
DEFINE_TC_ERROR(InsideFinally    , "unavailable inside finally block");
DEFINE_TC_ERROR(UnneedNamedArg   , "not need named argument");
DEFINE_TC_ERROR(NeedNamedArg     , "need named argument");
DEFINE_TC_ERROR(NoDefaultValue   , "has no default value");
DEFINE_TC_ERROR(OutsideToplevel  , "only available toplevel scope");
DEFINE_TC_ERROR(NotCallable      , "Func type object is not callable");
DEFINE_TC_ERROR(DefinedSymbol    , "already defined symbol: %");
DEFINE_TC_ERROR(DefinedField     , "already defiend field: %");
DEFINE_TC_ERROR(UndefinedSymbol  , "undefined symbol: %");
DEFINE_TC_ERROR(UndefinedField   , "undefined field: %");
DEFINE_TC_ERROR(UndefinedMethod  , "undefined method: %");
DEFINE_TC_ERROR(UndefinedInit    , "undefined constructor: %");
DEFINE_TC_ERROR(Unacceptable     , "unacceptable type: %");
DEFINE_TC_ERROR(NoIterator       , "not support iterator: %");
DEFINE_TC_ERROR(UnfoundNamedParam, "undefined parameter name: %");
DEFINE_TC_ERROR(DupNamedArg      , "found duplicated named argument: %");
DEFINE_TC_ERROR(DefinedCmd       , "already defined command: %");
DEFINE_TC_ERROR(Unimplemented    , "unimplemented type checker api: %");
DEFINE_TC_ERROR(Required         , "require %, but is %");
DEFINE_TC_ERROR(CastOp           , "unsupported cast op: % -> %");
DEFINE_TC_ERROR(UnaryOp          , "undefined operator: % %");
DEFINE_TC_ERROR(UnmatchParam     , "not match parameter, require size is %, but is %");
DEFINE_TC_ERROR(BinaryOp         , "undefined operator: % % %");

#undef DEFINE_TC_ERROR

} // namespace parser
} // namespace ydsh


#endif /* PARSER_TYPECHECKERROR_HPP_ */
