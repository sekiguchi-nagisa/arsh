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

#include "misc/inlined_stack.hpp"
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

  struct CheckerFrame {
    std::unique_ptr<Node> *node;
    unsigned int index;

    CheckerFrame() = default;

    explicit CheckerFrame(std::unique_ptr<Node> &node) : node(&node), index(0) {}
  };

private:
  static constexpr unsigned int STACK_DEPTH_LIMIT = 256;

  StringRef ref;
  const char *iter{nullptr};
  Flag flag;
  bool overflow{false};
  unsigned int captureGroupCount{0};
  std::unordered_map<std::string, unsigned int> namedCaptureGroups;
  std::unique_ptr<Error> error{nullptr};

  InlinedStack<CheckerFrame, 4> checkerFrames;

  CodePointSet idStartSet;
  CodePointSet idContinueSet;

public:
  Parser() = default;

  SyntaxTree operator()(const StringRef src, const Flag f) {
    this->ref = src;
    this->flag = f;
    this->overflow = false;
    this->captureGroupCount = 0;
    this->iter = this->begin();
    this->error.reset();
    while (this->checkerFrames.size()) {
      this->checkerFrames.pop();
    }

    // actual parse function
    auto node = this->parse();
    if (node) {
      if (!this->check(node)) {
        node = nullptr;
      }
    }
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

  void reportOverflow() {
    this->reportError(this->getTokenFrom(this->begin()), "deeply nested regular expression");
  }

  const char *begin() const { return this->ref.begin(); }

  const char *end() const { return this->ref.end(); }

  bool isEnd() const { return this->iter == this->end(); }

  bool startsWith(StringRef prefix) const {
    return StringRef(this->iter, this->end() - this->iter).startsWith(prefix);
  }

  int nextValidCodePoint();

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

  std::unique_ptr<Node> parseNamedBackRef();

  /**
   * parse capture group name
   * @param prefixStart for error message
   * @param ignoreError
   * @return
   * if error, return empty
   */
  std::string parseCaptureGroupName(const char *prefixStart, bool ignoreError);

  // extra syntax check
  bool check(std::unique_ptr<Node> &node);

  /**
   * check back reference indicate valid capture group name / index
   * in BMP mode, if invalid name, replace it with char nodes
   * @param node
   * @return
   */
  bool checkBackRef(std::unique_ptr<Node> &node);

  enum class BackRefResolveStatus : unsigned char {
    OK,
    ERROR,
    REPLACE,
  };

  BackRefResolveStatus resolveCaptureGroup(BackRefNode &refNode);
};

} // namespace arsh::regex

#endif // ARSH_REGEX_PARSER_H
