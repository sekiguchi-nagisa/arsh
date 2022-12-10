/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef YDSH_HIGHLIGHTER_BASE_H
#define YDSH_HIGHLIGHTER_BASE_H

#include <array>

#include "parser.h"

namespace ydsh {

#define EACH_HIGHLIGHT_TOKEN_CLASS(OP)                                                             \
  OP(NONE, "none")                                                                                 \
  OP(COMMENT, "comment")                                                                           \
  OP(KEYWORD, "keyword")                                                                           \
  OP(OPERATOR, "operator")                                                                         \
  OP(NUMBER, "number")                                                                             \
  OP(REGEX, "regex")                                                                               \
  OP(STRING, "string")                                                                             \
  OP(COMMAND, "command")                                                                           \
  OP(COMMAND_ARG, "command_arg")                                                                   \
  OP(REDIRECT, "redirect")                                                                         \
  OP(VARIABLE, "variable")                                                                         \
  OP(TYPE, "type")                                                                                 \
  OP(MEMBER, "member")                                                                             \
  OP(FOREGROUND_, "foreground") /* pseudo token class for foreground (text) color */               \
  OP(BACKGROUND_, "background") /* pseudo token class for background color */                      \
  OP(LINENO_, "lineno")         /* pseudo token class for line number */

enum class HighlightTokenClass : unsigned int {
#define GEN_ENUM(E, S) E,
  EACH_HIGHLIGHT_TOKEN_CLASS(GEN_ENUM)
#undef GEN_ENUM
};

HighlightTokenClass toTokenClass(TokenKind kind);

using HighlightTokenEntries =
    std::array<std::pair<HighlightTokenClass, const char *>,
               static_cast<unsigned int>(HighlightTokenClass::LINENO_) + 1>;

const HighlightTokenEntries &getHighlightTokenEntries();

class TokenEmitter : public TriviaStore, public TokenTracker {
protected:
  /**
   * must be end with newline
   */
  StringRef source;

public:
  explicit TokenEmitter(StringRef source) : source(source) {}

  ~TokenEmitter() override = default;

  [[nodiscard]] StringRef getSource() const { return this->source; }

  void operator()(TokenKind kind, Token token) override;

  /**
   * for comment
   * @param token
   * must be represent comment
   */
  void operator()(Token token) override;

  /**
   * common entry point of colorize source content
   * @return
   */
  std::unique_ptr<ParseError> tokenizeAndEmit();

private:
  /**
   * actual token emit function
   * @param tokenClass
   * @param token
   */
  virtual void emit(HighlightTokenClass tokenClass, Token token) = 0;
};

template <typename T>
struct ANSIFormatOp {
  void writeWithEscapeSeq(StringRef ref, const std::string &escapeSeq) {
    // split by newline
    for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
      auto r = ref.find('\n', pos);
      auto line = ref.slice(pos, r);
      pos = r != StringRef::npos ? r + 1 : r;

      if (!line.empty()) {
        static_cast<T *>(this)->write(escapeSeq);
        static_cast<T *>(this)->write(line);
        if (!escapeSeq.empty()) {
          static_cast<T *>(this)->write("\033[0m");
        }
      }
      if (r != StringRef::npos) {
        static_cast<T *>(this)->write("\n");
      }
    }
  }
};

} // namespace ydsh

#endif // YDSH_HIGHLIGHTER_BASE_H
