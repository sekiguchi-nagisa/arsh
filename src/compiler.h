/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef YDSH_COMPILER_H
#define YDSH_COMPILER_H

#include "cgerror.h"
#include "codegen.h"
#include "frontend.h"
#include "misc/flag_util.hpp"
#include <ydsh/ydsh.h>

namespace ydsh {

#define EACH_TERM_COLOR(C)                                                                         \
  C(Reset, 0)                                                                                      \
  C(Bold, 1)                                                                                       \
  /*C(Black,   30)*/                                                                               \
  /*C(Red,     31)*/                                                                               \
  C(Green, 32)                                                                                     \
  C(Yellow, 33)                                                                                    \
  C(Blue, 34)                                                                                      \
  C(Magenta, 35)                                                                                   \
  C(Cyan, 36) /*                                                                                   \
  C(white,   37)*/

enum class TermColor : unsigned int { // ansi color code
#define GEN_ENUM(E, N) E,
  EACH_TERM_COLOR(GEN_ENUM)
#undef GEN_ENUM
};

struct ErrorConsumer {
  virtual ~ErrorConsumer() = default;

  virtual bool colorSupported() const = 0;

  virtual void consume(std::string &&message) = 0;

  virtual void consume(DSError &&error) = 0;
};

class DefaultErrorConsumer : public ErrorConsumer {
private:
  DSError *dsError;
  FILE *fp;
  bool tty;

public:
  /**
   *
   * @param error
   * @param fp
   * may be null
   * @param close
   */
  DefaultErrorConsumer(DSError *error, FILE *fp);

  ~DefaultErrorConsumer() override = default;

  bool colorSupported() const override;

  void consume(std::string &&message) override;

  void consume(DSError &&error) override;
};

class ErrorReporter : public FrontEnd::ErrorListener {
private:
  ErrorConsumer &consumer;

public:
  explicit ErrorReporter(ErrorConsumer &consumer) : consumer(consumer) {}

  ~ErrorReporter() override = default;

  bool handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                        const ParseError &parseError) override;
  bool handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                       const TypeCheckError &checkError) override;
  bool handleCodeGenError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                          const CodeGenError &codeGenError);

private:
  bool handleError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx, DSErrorKind type,
                   const char *errorKind, Token errorToken, const char *message);

  void printError(const Lexer &lexer, const char *kind, Token token, TermColor c,
                  const char *message);

  const char *color(TermColor c) const;

  void printErrorLine(std::string &out, const Lexer &lexer, Token token) const;

  static void appendAs(std::string &out, const char *fmt, ...)
      __attribute__((format(printf, 2, 3)));
};

enum class CompileOption : unsigned short {
  LOAD_TO_ROOT = 1u << 1u,
  PARSE_ONLY = 1u << 2u,
  CHECK_ONLY = 1u << 3u,
  PRINT_TOPLEVEL = 1u << 4u,
};

template <>
struct allow_enum_bitop<CompileOption> : std::true_type {};

inline FrontEndOption toOption(CompileOption option) {
  FrontEndOption op{};
  if (hasFlag(option, CompileOption::PARSE_ONLY)) {
    setFlag(op, FrontEndOption::PARSE_ONLY);
  }
  if (hasFlag(option, CompileOption::PRINT_TOPLEVEL)) {
    setFlag(op, FrontEndOption::TOPLEVEL);
  }
  return op;
}

struct CompileDumpTarget {
  FILE *fps[3];
};

class Compiler {
private:
  CompileOption compileOption;
  DefaultModuleProvider &provider;
  FrontEnd frontEnd;
  ErrorReporter errorReporter;
  NodeDumper uastDumper;
  NodeDumper astDumper;
  ByteCodeGenerator codegen;
  ByteCodeDumper codeDumper;

public:
  Compiler(DefaultModuleProvider &moduleProvider, std::unique_ptr<FrontEnd::Context> &&ctx,
           CompileOption compileOption, const CompileDumpTarget *dumpTarget,
           ErrorConsumer &consumer);

  bool frontEndOnly() const {
    return hasFlag(this->compileOption, CompileOption::PARSE_ONLY) ||
           hasFlag(this->compileOption, CompileOption::CHECK_ONLY);
  }

  unsigned int lineNum() const { return this->frontEnd.getRootLineNum(); }

  int operator()(ObjPtr<FuncObject> &func);
};

} // namespace ydsh

#endif // YDSH_COMPILER_H
