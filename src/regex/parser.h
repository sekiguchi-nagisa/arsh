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

  enum class FrameType : unsigned char {
    NONE,
    GROUP,
    LOOK_AROUND,
  };

  struct Frame {
    FrameType type{FrameType::NONE};
    union {
      LookAroundNode::Type lookaroundType;
      GroupNode::Type groupType;
    };
    Modifier setModifiers{Modifier::NONE};
    Modifier unsetModifiers{Modifier::NONE};
    Token start;
    std::unique_ptr<Node> node;

    Frame() = default;

    explicit Frame(std::unique_ptr<Node> &&node) : node(std::move(node)) {
      this->groupType = GroupNode::Type::NON_CAPTURE;
    }

    Frame(Token start, LookAroundNode::Type lookaroundType)
        : type(FrameType::LOOK_AROUND), start(start) {
      this->lookaroundType = lookaroundType;
    }

    Frame(Token start, GroupNode::Type groupType) : type(FrameType::GROUP), start(start) {
      this->groupType = groupType;
    }

    Frame(Token start, Modifier set, Modifier unset) : type(FrameType::GROUP), start(start) {
      this->groupType = GroupNode::Type::MODIFIER;
      this->setModifiers = set;
      this->unsetModifiers = unset;
    }
  };

private:
  static constexpr unsigned int STACK_DEPTH_LIMIT = 256;

  StringRef ref;
  const char *iter{nullptr};
  Flag flag;
  bool overflow{false};
  unsigned int captureGroupCount{0};
  std::unordered_map<std::string, FlexBuffer<unsigned int>> namedCaptureGroups;
  std::vector<BackRefNode *> namedRefNodes; // for lazy named backref check
  std::unique_ptr<Error> error{nullptr};
  std::vector<Frame> frames;

  CodePointSet idStartSet;
  CodePointSet idContinueSet;

public:
  Parser() = default;

  SyntaxTree operator()(StringRef src, Flag f);

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

  StringRef getStrRefFrom(const char *begin) const { return StringRef(begin, this->iter - begin); }

  void reportError(Token token, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

  void reportOverflow(Token token) { this->reportError(token, "deeply nested regular expression"); }

  void reportUndefinedNamedRef(Token token, const char *name) {
    this->reportError(token, "undefined capture group name: `%s'", name);
  }

  void reportUnclosedGroup(Token token) { this->reportError(token, "unclosed group"); }

  const char *begin() const { return this->ref.begin(); }

  const char *end() const { return this->ref.end(); }

  bool isEnd() const { return this->iter == this->end(); }

  bool startsWith(StringRef prefix) const {
    return StringRef(this->iter, this->end() - this->iter).startsWith(prefix);
  }

  auto &curNode() { return this->frames.back().node; }

  int nextValidCodePoint();

  void append(std::unique_ptr<Node> &&node);

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

  bool hasNameCaptureGroup() const {
    return !this->namedCaptureGroups.empty(); // TODO: lazy fill
  }

  /**
   * parse capture group name
   * @param prefixStart for error message
   * @param ignoreError
   * @return
   * if error, return empty
   */
  std::string parseCaptureGroupName(const char *prefixStart, bool ignoreError);

  std::unique_ptr<Node> tryToParseQuantifier(std::unique_ptr<Node> &&node, bool ignoreError);

  Optional<unsigned short> parseQuantifierDigits(const char *prefixStart, bool ignoreError,
                                                 char end);

  Optional<Modifier> parseModifiers(char end);

  bool enterGroup();

  std::unique_ptr<Node> exitGroup();
};

} // namespace arsh::regex

#endif // ARSH_REGEX_PARSER_H
