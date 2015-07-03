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
    E(Unresolved    , "having unresolved type") \
    E(InsideLoop    , "only available inside loop statement") \
    E(UnfoundReturn , "not found return statement") \
    E(Unreachable   , "found unreachable code") \
    E(InsideFunc    , "only available inside function") \
    E(NotNeedExpr   , "not need expression") \
    E(Assignable    , "require assignable node") \
    E(ReadOnly      , "read only value") \
    E(InsideFinally , "unavailable inside finally block") \
    E(UnneedNamedArg, "not need named argument") \
    E(NeedNamedArg  , "need named argument") \
    E(NoDefaultValue, "has no default value")

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
    E(UndefinedEnv     , "undefined environment variable: ") \
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
public:
    enum ErrorKind0 {
#define GEN_ENUM(ENUM, S1) ENUM,
        EACH_TC_ERROR0(GEN_ENUM)
#undef GEN_ENUM
    };

    enum ErrorKind1 {
#define GEN_ENUM(ENUM, S1) ENUM,
        EACH_TC_ERROR1(GEN_ENUM)
#undef GEN_ENUM
    };

    enum ErrorKind2 {
#define GEN_ENUM(ENUM, S1, S2) ENUM,
        EACH_TC_ERROR2(GEN_ENUM)
#undef GEN_ENUM
    };

    enum ErrorKind3 {
#define GEN_ENUM(ENUM, S1, S2, S3) ENUM,
        EACH_TC_ERROR3(GEN_ENUM)
#undef GEN_ENUM
    };

private:
    /**
     * line number of error node
     */
    unsigned int lineNum;

    const char *kind;

    std::string message;

public:
    TypeCheckError(unsigned int lineNum, TypeCheckError::ErrorKind0 k);
    TypeCheckError(unsigned int lineNum, TypeCheckError::ErrorKind1 k, const std::string &arg1);
    TypeCheckError(unsigned int lineNum, TypeCheckError::ErrorKind2 k, const std::string &arg1,
                   const std::string &arg2);
    TypeCheckError(unsigned int lineNum, TypeCheckError::ErrorKind3 k, const std::string &arg1,
                   const std::string &arg2, const std::string &arg3);

    TypeCheckError(unsigned int lineNum, const core::TypeLookupError &e);

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

std::ostream &operator<<(std::ostream &stream, TypeCheckError::ErrorKind0 kind);
std::ostream &operator<<(std::ostream &stream, TypeCheckError::ErrorKind1 kind);
std::ostream &operator<<(std::ostream &stream, TypeCheckError::ErrorKind2 kind);
std::ostream &operator<<(std::ostream &stream, TypeCheckError::ErrorKind3 kind);

struct ErrorRaiser0 {
    TypeCheckError::ErrorKind0 kind;

    void operator()(ast::Node *node) throw(TypeCheckError);
};

struct ErrorRaiser1 {
    TypeCheckError::ErrorKind1 kind;

    void operator()(ast::Node *node, const std::string &arg1) throw(TypeCheckError);
};

struct ErrorRaiser2 {
    TypeCheckError::ErrorKind2 kind;

    void operator()(ast::Node *node, const std::string &arg1,
                    const std::string &arg2) throw(TypeCheckError);
};

struct ErrorRaiser3 {
    TypeCheckError::ErrorKind3 kind;

    void operator()(ast::Node *node, const std::string &arg1,
                    const std::string &arg2, const std::string &arg3) throw(TypeCheckError);
};

// define function object for error reporting

#define GEN_VAR(ENUM, S1) extern ErrorRaiser0 E_##ENUM;
EACH_TC_ERROR0(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1) extern ErrorRaiser1 E_##ENUM;
EACH_TC_ERROR1(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2) extern ErrorRaiser2 E_##ENUM;
EACH_TC_ERROR2(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2, S3) extern ErrorRaiser3 E_##ENUM;
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
