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

#ifndef YDSH_TCERROR_H
#define YDSH_TCERROR_H

#include <stdexcept>

#include "node.h"
#include "tlerror.h"

namespace ydsh {

/**
 * for type error reporting
 */
class TypeCheckError {
private:
  Token token{};

  const char *kind{nullptr};

  CStrPtr message;

public:
  TypeCheckError() = default;

  TypeCheckError(Token token, const char *kind, CStrPtr &&message)
      : token(token), kind(kind), message(std::move(message)) {}

  TypeCheckError(Token token, TypeLookupError &&e) noexcept
      : token(token), kind(e.getKind()), message(std::move(e).takeMessage()) {}

  TypeCheckError(const TypeCheckError &o) noexcept
      : token(o.token), kind(o.kind), message(strdup(o.message.get())) {}

  ~TypeCheckError() = default;

  Token getToken() const { return this->token; }

  const char *getKind() const { return this->kind; }

  const char *getMessage() const { return this->message.get(); }
};

struct TCError {};

#define DEFINE_TCError(E, fmt)                                                                     \
  struct E : TCError {                                                                             \
    static constexpr const char *kind = #E;                                                        \
    static constexpr const char *value = fmt;                                                      \
  }

DEFINE_TCError(InsideLoop, "only available inside loop statement");
DEFINE_TCError(UnfoundReturn, "not found return statement");
DEFINE_TCError(Unreachable, "unreachable code");
DEFINE_TCError(InsideFunc, "only available inside function");
DEFINE_TCError(NotNeedExpr, "not need expression");
DEFINE_TCError(Assignable, "require assignable expression");
DEFINE_TCError(ReadOnlySymbol, "read only symbol: `%s'");
DEFINE_TCError(ReadOnlyField, "read only field: `%s'");
DEFINE_TCError(InsideFinally, "no-return expressions (break, continue, return, throw, etc..) "
                              "are not allowed inside finally block and defer statement");
DEFINE_TCError(InsideChild, "some no-return expression (break, continue, return) "
                            "are not allowed inside child process");
DEFINE_TCError(OutsideToplevel, "%s is only available in global scope");
DEFINE_TCError(NotCallable, "Func type object is not directly callable");
DEFINE_TCError(UselessBlock, "useless block");
DEFINE_TCError(EmptyTry, "empty try block");
DEFINE_TCError(LocalLimit, "number of local variables reaches limit");
DEFINE_TCError(GlobalLimit, "number of global variables reaches limit");
DEFINE_TCError(PipeLimit, "pipeline length reaches limit");
DEFINE_TCError(ExpandLimit, "cannot expand too large path fragments");
DEFINE_TCError(NullInPath, "found null characters in source path");
DEFINE_TCError(NoGlobMatch, "no matches for glob pattern: `%s'");
DEFINE_TCError(ExpandRetLimit, "number of expansion results reaches limit");
DEFINE_TCError(NoGlobDir, "glob pattern always matches directory: `%s'");
DEFINE_TCError(Constant, "must be constant expression");
DEFINE_TCError(DupPattern, "duplicated pattern");
DEFINE_TCError(NeedPattern, "require at least one pattern");
DEFINE_TCError(NeedDefault, "the case expression needs default pattern");
DEFINE_TCError(DefinedSymbol, "already defined symbol: `%s'");
DEFINE_TCError(DefinedTypeAlias, "already defined type: `%s'");
DEFINE_TCError(DefinedMethod, "already defined method: `%s' for `%s' type");
DEFINE_TCError(UndefinedSymbol, "undefined symbol: `%s'");
DEFINE_TCError(UndefinedField, "undefined field: `%s' for `%s' type");
DEFINE_TCError(UndefinedMethod, "undefined method: `%s' for `%s' type");
DEFINE_TCError(UndefinedInit, "constructor is not defined in `%s' type");
DEFINE_TCError(UndefinedUnary, "undefined unary op: `%s' for `%s' type");
DEFINE_TCError(UndefinedBinary, "undefined binary op: `%s' for `%s' type");
DEFINE_TCError(NotIterable, "cannot iterate `%s' type");
DEFINE_TCError(PrivateField, "cannot access module private field: `%s'");
DEFINE_TCError(Unacceptable, "unacceptable type: `%s'");
DEFINE_TCError(DefinedCmd, "already defined command: `%s'");
DEFINE_TCError(ConflictSymbol, "at global import, detect symbol conflict: `%s' in `%s'");
DEFINE_TCError(NotOpenMod, "cannot read module: `%s', by `%s'");
DEFINE_TCError(NotFoundMod, "module not found: `%s'");
DEFINE_TCError(CircularMod, "circular module import: `%s'");
DEFINE_TCError(ModLimit, "number of loaded modules reaches limit");
DEFINE_TCError(Required, "require `%s' type, but is `%s' type");
DEFINE_TCError(CastOp, "unsupported cast op: `%s' type -> `%s' type");
DEFINE_TCError(NothingCast, "explicit cast from `Nothing' type is not allowed");
DEFINE_TCError(UnmatchParam, "not match parameter, require size is %d, but is %d");
DEFINE_TCError(RegexSyntax, "regex syntax error: `%s'");
DEFINE_TCError(NoCommonSuper, "cannot resolve common super type from `%s'");
DEFINE_TCError(ConcatParam, "concatenation of `%s' type value is "
                            "not allowed in parameter expansion");
DEFINE_TCError(FuncDepthLimit, "nested function depth reaches limit");
DEFINE_TCError(UpvarLimit, "number of upper variables in local function reaches limit");
DEFINE_TCError(UncaptureEnv, "local function cannot access temporary "
                             "defined environmental variables: `%s'");
DEFINE_TCError(UncaptureField, "local function cannot access upper variables "
                               "that will be assigned to fields: `%s'");
DEFINE_TCError(ErrorMod, "syntax or semantic errors occurred in `%s'");
DEFINE_TCError(NotInferParamNoFunc, "cannot infer parameter type, "
                                    "because current context does not require function type");
DEFINE_TCError(NotInferParamUnmatch, "cannot infer parameter type, "
                                     "because number of parameter is not match");

#undef DEFINE_TCError

TypeCheckError createTCErrorImpl(const Node &node, const char *kind, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

template <typename T, typename... Arg, typename = base_of_t<T, TCError>>
inline TypeCheckError createTCError(const Node &node, Arg &&...arg) {
  return createTCErrorImpl(node, T::kind, T::value, std::forward<Arg>(arg)...);
}

} // namespace ydsh

#endif // YDSH_TCERROR_H
