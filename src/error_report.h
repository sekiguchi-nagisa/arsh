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

#ifndef YDSH_ERROR_REPORT_H
#define YDSH_ERROR_REPORT_H

#include "cgerror.h"
#include "frontend.h"
#include <ydsh/ydsh.h>

namespace ydsh {

struct ErrorListener {
  virtual ~ErrorListener() = default;

  virtual bool handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                const ParseError &parseError) = 0;

  virtual bool handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                               const TypeCheckError &checkError) = 0;

  virtual bool handleCodeGenError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                  const CodeGenError &codeGenError) = 0;
};

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

class ErrorReporter : public ErrorListener {
private:
  DSError *dsError;
  FILE *fp;
  bool close;
  bool tty;

public:
  ErrorReporter(DSError *dsError, FILE *fp, bool close);

  ~ErrorReporter() override;

  bool handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                        const ParseError &parseError) override;
  bool handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                       const TypeCheckError &checkError) override;
  bool handleCodeGenError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                          const CodeGenError &codeGenError) override;

private:
  bool handleError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx, DSErrorKind type,
                   const char *errorKind, Token errorToken, const char *message);

  void printError(const Lexer &lexer, const char *kind, Token token, TermColor c,
                  const char *message);

  const char *color(TermColor c) const;

  void printErrorLine(const Lexer &lexer, Token token) const;
};

} // namespace ydsh

#endif // YDSH_ERROR_REPORT_H
