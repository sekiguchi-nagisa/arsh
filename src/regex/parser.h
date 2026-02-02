/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_REGEX_PARSER_H
#define ARSH_REGEX_PARSER_H

#include "misc/string_ref.hpp"

#include "node.h"

namespace arsh::regex {

class Parser {
public:
  struct Error {
    Token token;
    std::string message;

    Error(Token token, std::string &&message) : token(token), message(std::move(message)) {}
  };

private:
  StringRef ref;
  const char *iter{nullptr};
  Flag flag;
  unsigned int captureGroupCount{0};
  std::unordered_map<std::string, unsigned int> namedCaptureGroups;
  std::unique_ptr<Error> error{nullptr};

  CodePointSet idStartSet;
  CodePointSet idContinueSet;

public:
  Parser() = default;

  SyntaxTree operator()(const StringRef src, const Flag f) {
    this->ref = src;
    this->flag = f;
    this->captureGroupCount = 0;
    this->iter = this->begin();
    this->error.reset();
    auto node = this->parse();
    return {this->flag, this->captureGroupCount, std::move(node),
            std::move(this->namedCaptureGroups)};
  }

  bool hasError() const { return static_cast<bool>(this->error); }

  const auto &getError() const { return this->error; }

private:
  unsigned int curPos() const { return this->iter - this->begin(); }

  Token curToken(unsigned int len = 1) const { return {this->curPos(), len}; }

  Token getTokenFrom(const char *begin) const {
    Token token = {static_cast<unsigned int>(begin - this->begin()),
                   static_cast<unsigned int>(this->iter - begin)};
    return token;
  }

  void reportError(Token token, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

  const char *begin() const { return this->ref.begin(); }

  const char *end() const { return this->ref.end(); }

  bool isEnd() const { return this->iter == this->end(); }

  bool startsWith(StringRef prefix) const {
    return StringRef(this->iter, this->end() - this->iter).startsWith(prefix);
  }

  std::unique_ptr<Node> parse();

  std::unique_ptr<Node> parseAtomEscape();

  int parseUnicodeEscapeBMP(bool ignoreError);

  /**
   *
   * @param mayIgnoreError
   * if true, ignore parse error (always report surrogate code point event if true)
   * @return
   */
  int parseUnicodeEscape(bool mayIgnoreError);

  std::unique_ptr<PropertyNode> parseUnicodePropertyEscape();

  std::unique_ptr<BackRefNode> parseNamedBackRef();

  /**
   * parse capture group name (never report error)
   * @return
   * if error, return empty
   */
  std::string parseCaptureGroupName();
};

} // namespace arsh::regex

#endif // ARSH_REGEX_PARSER_H
