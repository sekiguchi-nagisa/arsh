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
public:
  enum class Type : unsigned int {
    ERROR,
    WARN,
  };

private:
  Type type{Type::ERROR};

  Token token{};

  const char *kind{nullptr};

  CStrPtr message;

public:
  TypeCheckError() = default;

  TypeCheckError(Type type, Token token, const char *kind, CStrPtr &&message)
      : type(type), token(token), kind(kind), message(std::move(message)) {}

  TypeCheckError(Token token, const char *kind, CStrPtr &&message)
      : TypeCheckError(Type::ERROR, token, kind, std::move(message)) {}

  TypeCheckError(Token token, TypeLookupError &&e) noexcept
      : token(token), kind(e.getKind()), message(std::move(e).takeMessage()) {}

  TypeCheckError(TypeCheckError &&o) noexcept
      : type(o.type), token(o.token), kind(o.kind), message(std::move(o.message)) {}

  ~TypeCheckError() = default;

  Type getType() const { return this->type; }

  Token getToken() const { return this->token; }

  const char *getKind() const { return this->kind; }

  const char *getMessage() const { return this->message.get(); }
};

struct TCError {};

template <TypeCheckError::Type TYPE>
struct TCErrorDetail : TCError {
  static constexpr TypeCheckError::Type type = TYPE;
};

#define DEFINE_TCErrorImpl(T, E, fmt)                                                              \
  struct E : TCErrorDetail<T> {                                                                    \
    static constexpr const char *kind = #E;                                                        \
    static constexpr const char *value = fmt;                                                      \
  }

#define DEFINE_TCError(E, fmt) DEFINE_TCErrorImpl(TypeCheckError::Type::ERROR, E, fmt)
#define DEFINE_TCWarn(E, fmt) DEFINE_TCErrorImpl(TypeCheckError::Type::WARN, E, fmt)

DEFINE_TCError(IllegalStrEscape, "illegal escape sequence: `%s'");
DEFINE_TCError(OutOfRangeInt, "out of range Int literal, must be INT64");
DEFINE_TCError(OutOfRangeFloat, "out of range Float literal, must be FP64");
DEFINE_TCError(InsideLoop, "only available inside loop (for, while, do-while)");
DEFINE_TCError(UnfoundReturn, "`%s' needs return expression");
DEFINE_TCError(Unreachable, "unreachable code");
DEFINE_TCError(InsideFunc, "only available inside function or user-defined command");
DEFINE_TCError(NotNeedExpr, "not return `Void' type expression");
DEFINE_TCError(Assignable, "require assignable expression, "
                           "such as `<expr>[<index>]', `<obj>.<field>', `<$variable>'");
DEFINE_TCError(ReadOnlySymbol, "`%s' is defined as read only");
DEFINE_TCError(ReadOnlyField, "field `%s' for `%s' type is defined as read only");
DEFINE_TCError(InsideFinally, "no-return expressions (break, continue, return, throw, etc..) "
                              "are not allowed inside finally block and defer statement");
DEFINE_TCError(InsideChild, "some no-return expressions (break, continue, return) "
                            "are not allowed inside sub-shell");
DEFINE_TCError(OutsideToplevel, "%s is only available in top-level");
DEFINE_TCError(NotCallable, "`%s' type expression is not callable, must be function type");
DEFINE_TCError(UselessBlock, "useless block");
DEFINE_TCError(EmptyTry, "empty try block");
DEFINE_TCError(LocalLimit, "number of local variables reaches limit");
DEFINE_TCError(GlobalLimit, "number of global variables reaches limit");
DEFINE_TCError(PipeLimit, "pipeline length reaches limit");
DEFINE_TCError(ExpandLimit, "cannot expand too large path fragments");
DEFINE_TCError(NullInPath, "found null characters in module path");
DEFINE_TCError(NoGlobMatch, "glob pattern `%s' does not match any files");
DEFINE_TCError(ExpandRetLimit, "number of expansion results reaches limit");
DEFINE_TCError(ExpandCancel, "glob expansion canceled");
DEFINE_TCError(NoGlobDir, "glob pattern in source statement should match module path, "
                          "but always matches directory: `%s'");
DEFINE_TCError(NoRelativeGlob, "glob pattern in source statement must be absolute path: `%s'");
DEFINE_TCError(TildeFail, "cannot expand tilde, no such user: `%s'");
DEFINE_TCError(TildeNoDirStack, "in source statement, `~+', `~+N', `~N' style "
                                "tilde expansions are not performed: `%s'");
DEFINE_TCError(BraceUnopened, "unopened brace expansion, require `{' before `}'");
DEFINE_TCError(BraceUnclosed, "unclosed brace expansion, require `}' after `{'");
DEFINE_TCError(BraceOutOfRange, "out of range brace expansion number: `%s', must be INT64");
DEFINE_TCError(BraceOutOfRangeStep, "out of range brace expansion increment number: `%s', "
                                    "must be INT64_MIN +1 to INT64_MAX");
DEFINE_TCError(Constant, "must be constant expression");
DEFINE_TCError(NegativeIntMin, "negative value of INT64_MIN is not allowed");
DEFINE_TCError(DupPattern, "duplicated pattern");
DEFINE_TCError(NeedPattern, "case expression requires at least one pattern");
DEFINE_TCError(NeedDefault, "case expression requires default pattern `else => <expr>'");
DEFINE_TCError(DefinedSymbol, "already defined symbol: `%s'");
DEFINE_TCError(DefinedTypeAlias, "already defined type: `%s'");
DEFINE_TCError(DefinedMethod, "already defined method: `%s' for `%s' type");
DEFINE_TCError(NeedUdType, "method definition is only allowed for user-defined type");
DEFINE_TCError(SameModOfRecv, "method definition is only allowed at the same module "
                              "that `%s' type is defined");
DEFINE_TCError(SameNameField, "cannot define method: `%s', since `%s' type has same name field");
DEFINE_TCError(UndefinedSymbol, "cannot access undefined symbol: `%s'%s");
DEFINE_TCError(UndefinedType, "undefined type: `%s'%s");
DEFINE_TCError(UndefinedField, "cannot access undefined field: `%s' for `%s' type%s");
DEFINE_TCError(UndefinedMethod, "cannot call undefined method: `%s' for `%s' type%s");
DEFINE_TCError(UndefinedInit, "constructor is not defined in `%s' type");
DEFINE_TCError(UndefinedUnary, "undefined unary op: `%s' for `%s' type");
DEFINE_TCError(UndefinedBinary, "undefined binary op: `%s' for `%s' type");
DEFINE_TCError(NotIterable, "cannot iterate `%s' type");
DEFINE_TCError(PrivateField, "cannot access module private field: `%s'");
DEFINE_TCError(OptParamExpand, "`%s' type expression is not allowed "
                               "within string interpolations and parameter expansions");
DEFINE_TCError(Unacceptable, "`%s' type expression is not allowed");
DEFINE_TCError(UnacceptableType, "`%s' type is not allowed");
DEFINE_TCError(DefinedCmd, "already defined command: `%s'");
DEFINE_TCError(ConflictSymbol, "at global import, cannot import %s defined in `%s' module\n"
                               "since same name symbol is already defined in this module");
DEFINE_TCError(NotOpenMod, "cannot read module: `%s', caused by `%s'");
DEFINE_TCError(NotFoundMod, "module not found: `%s'");
DEFINE_TCError(CircularMod, "`%s' module recursively import itself");
DEFINE_TCError(ModLimit, "number of loaded modules reaches limit");
DEFINE_TCError(Required, "require `%s' type, but is `%s' type");
DEFINE_TCError(InvalidCatchType, "invalid catch type: `%s', must be `Error' or "
                                 "its derived types (except for `Nothing' type)");
DEFINE_TCError(NestedJob, "`&', `&|`, `&!' and `coproc' operators cannot "
                          "be applied to `Job' type expression");
DEFINE_TCError(CastOp, "unsupported cast op: `%s' type -> `%s' type");
DEFINE_TCError(NothingCast, "explicit cast from `Nothing' type expression is not allowed");
DEFINE_TCError(InvalidOptCast,
               "optional cast (`as?') only supports downcast, target type must be derived of `%s'");
DEFINE_TCError(UnmatchParam, "number of parameters does not match, require `%d', but is `%d'");
DEFINE_TCError(RegexSyntax, "regex syntax error: `%s'");
DEFINE_TCError(NoCommonSuper, "cannot resolve common super type from `%s'");
DEFINE_TCError(ConcatParam, "concatenation of `%s' type expression is not allowed");
DEFINE_TCError(FuncDepthLimit, "nested function depth reaches limit");
DEFINE_TCError(UpvarLimit, "number of upper variables in local function reaches limit");
DEFINE_TCError(UncaptureEnv, "local function cannot access temporary "
                             "defined environmental variables: `%s'");
DEFINE_TCError(UncaptureField, "local function cannot access upper variables "
                               "that will be assigned to fields: `%s'");
DEFINE_TCError(ErrorMod, "syntax or semantic errors occurred in `%s'");
DEFINE_TCError(NotInferParamNoFunc, "cannot infer parameter type, "
                                    "since current context does not require function type");
DEFINE_TCError(NotInferParamUnmatch,
               "cannot infer parameter type, since number of "
               "parameters does not match,\n`%s' type requires `%d' params, but is `%d'");
DEFINE_TCError(NoBackquote, "back-quote command substitution is not allowed. use `$( )' instead");
DEFINE_TCError(PosArgRange, "positional argument is out-of-range (up to INT32_MAX): `%s'");
DEFINE_TCError(RedirFdRange, "specified file descriptor number: `%s', but only allow 0,1,2");
DEFINE_TCError(NeedFd,
               "`>&', `<&' redirection only allow decimal numbers (0,1,2) or `FD' type expression");
DEFINE_TCError(IfLetOpt, "right hand-side of if-let condition must be Option type, but is `%s'");
DEFINE_TCError(UnwrapType,
               "unwrap op `!' must follow Option type expression, but actual is `%s' type");

DEFINE_TCWarn(MeaninglessCast, "meaningless cast op");
DEFINE_TCWarn(VarShadowing, "`%s' hides already defined name of outer scope");

TypeCheckError createTCErrorImpl(const Node &node, const char *kind, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

template <typename T, typename... Arg, typename = base_of_t<T, TCError>>
inline TypeCheckError createTCError(const Node &node, Arg &&...arg) {
  return createTCErrorImpl(node, T::kind, T::value, std::forward<Arg>(arg)...);
}

inline void addSuggestionSuffix(std::string &value, StringRef suggestion) {
  value += ", did you mean `";
  value += suggestion;
  value += "' ?";
}

} // namespace ydsh

#endif // YDSH_TCERROR_H
