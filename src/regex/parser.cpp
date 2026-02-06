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

#include "parser.h"

#include <cstdarg>

#include "misc/format.hpp"
#include "misc/num_util.hpp"
#include "misc/rtti.hpp"
#include "misc/unicode.hpp"

namespace arsh::regex {

// #####################
// ##     AltNode     ##
// #####################

void AltNode::appendToLast(std::unique_ptr<Node> &&node) {
  Token token = node->getToken();
  if (auto &last = this->patterns.back(); !last) {
    last = std::move(node);
  } else if (isa<SeqNode>(*last)) {
    cast<SeqNode>(*last).append(std::move(node));
  } else {
    last = std::make_unique<SeqNode>(std::move(last), std::move(node));
  }
  this->updateToken(token);
}

// ####################
// ##     Parser     ##
// ####################

void Parser::reportError(Token token, const char *fmt, ...) { // NOLINT
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    abort();
  }
  va_end(arg);
  this->error = std::make_unique<Error>(token, str);
  free(str);
}

int Parser::nextValidCodePoint() {
  int codePoint = -1;
  if (unsigned int len = UnicodeUtil::utf8ToCodePoint(this->iter, this->end(), codePoint)) {
    if (!UnicodeUtil::isValidCodePoint(codePoint)) {
      this->reportError(this->curToken(len), "invalid UTF-8 (surrogate): `U+%04X'", codePoint);
      return -1;
    }
    this->iter += len;
  } else {
    this->reportError(this->curToken(), "invalid UTF-8 byte: `%02X'", *this->iter);
  }
  return codePoint;
}

static void append(std::unique_ptr<Node> &node, std::unique_ptr<Node> &&tmp) {
  if (!node) {
    node = std::move(tmp);
  } else if (isa<AltNode>(*node)) {
    cast<AltNode>(*node).appendToLast(std::move(tmp));
  } else if (isa<SeqNode>(*node)) {
    cast<SeqNode>(*node).append(std::move(tmp));
  } else {
    node = std::make_unique<SeqNode>(std::move(node), std::move(tmp));
  }
}

static bool isSyntaxChar(const char ch) {
  switch (ch) {
  case '^':
  case '$':
  case '\\':
  case '.':
  case '*':
  case '+':
  case '?':
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '|':
    return true;
  default:
    return false;
  }
}

std::unique_ptr<Node> Parser::parse() {
  std::unique_ptr<Node> node;

  while (this->iter != this->end()) {
    std::unique_ptr<Node> atomNode;
    switch (*this->iter) {
    case '^':
      append(node, std::make_unique<BoundaryNode>(this->curToken(), BoundaryNode::Type::START));
      this->iter++;
      continue;
    case '$':
      append(node, std::make_unique<BoundaryNode>(this->curToken(), BoundaryNode::Type::END));
      this->iter++;
      continue;
    case '\\': {
      this->iter++;
      if (auto r = this->parseAtomEscape()) {
        if (isa<BoundaryNode>(*r)) {
          append(node, std::move(r));
          continue;
        }
        atomNode = std::move(r);
        goto REPEAT;
      }
      return nullptr;
    }
    case '.':
      atomNode = std::make_unique<AnyNode>(this->curToken());
      this->iter++;
      goto REPEAT;
    case '*':
    case '+':
    case '?': {
      this->reportError(this->curToken(), "`%c' quantifier does not follow atom", *this->iter);
      return nullptr;
    }
    case '(':
      goto REPEAT; // TODO
    case ')':
      this->reportError(this->curToken(), "unmatched `)'");
      return nullptr;
    case '[':
      goto REPEAT; // TODO
    case ']':
      if (this->flag.isEitherUnicodeMode()) {
        this->reportError(this->curToken(), "unmatched `]'");
        return nullptr;
      }
      atomNode = std::make_unique<CharNode>(this->curToken(), *this->iter);
      this->iter++;
      goto REPEAT;
    case '{':
    case '}':
      if (this->flag.isEitherUnicodeMode()) {
        this->reportError(this->curToken(), "lone quantifier bracket `%c'", *this->iter);
        return nullptr;
      }
      atomNode = std::make_unique<CharNode>(this->curToken(), *this->iter);
      this->iter++;
      goto REPEAT;
    case '|':
      if (!node) {
        node = std::make_unique<EmptyNode>(this->curPos());
      }
      if (isa<AltNode>(*node)) {
        if (auto &altNode = cast<AltNode>(*node); !altNode.getPatterns().back()) {
          altNode.appendToLast(std::make_unique<EmptyNode>(this->curPos())); // repace null
        }
      } else {
        node = std::make_unique<AltNode>(std::move(node));
      }
      cast<AltNode>(*node).appendNull(); // for next alternative
      this->iter++;
      continue;
    default: {
      const auto old = this->iter;
      if (int codePoint = this->nextValidCodePoint(); codePoint > -1) {
        atomNode = std::make_unique<CharNode>(this->getTokenFrom(old), codePoint);
        goto REPEAT;
      }
      return nullptr;
    }
    }

  REPEAT:
    atomNode = this->tryToParseQuantifier(std::move(atomNode), this->flag.is(Mode::LEGACY));
    if (!atomNode) {
      return nullptr;
    }
    append(node, std::move(atomNode));
  }

  if (!node) {
    node = std::make_unique<EmptyNode>(this->curPos());
  } else if (isa<AltNode>(*node) && !cast<AltNode>(*node).getPatterns().back()) {
    cast<AltNode>(*node).appendToLast(std::make_unique<EmptyNode>(this->curPos()));
  }
  return node;
}

std::unique_ptr<Node> Parser::parseAtomEscape() {
  const auto start = this->iter - 1;
  if (this->isEnd()) {
    this->reportError(this->getTokenFrom(start), "\\ at end of pattern");
    return nullptr;
  }
  int codePoint = -1;
  switch (*this->iter) {
  case 'f':
    codePoint = '\f';
    this->iter++;
    goto CHAR;
  case 'n':
    codePoint = '\n';
    this->iter++;
    goto CHAR;
  case 'r':
    codePoint = '\r';
    this->iter++;
    goto CHAR;
  case 't':
    codePoint = '\t';
    this->iter++;
    goto CHAR;
  case 'v':
    codePoint = '\v';
    this->iter++;
    goto CHAR;
  case 'c': {
    this->iter++;
    if (!this->isEnd() && isLetter(*this->iter)) {
      codePoint = static_cast<int>(*this->iter) % 32;
      this->iter++;
      goto CHAR;
    }
    if (this->flag.is(Mode::LEGACY)) {
      this->iter--; // only consume '\\'
      codePoint = '\\';
      goto CHAR;
    }
    std::string str;
    if (unsigned int len = UnicodeUtil::utf8ValidateChar(this->iter, this->end())) {
      str += StringRef(this->iter, len);
      this->iter += len;
    }
    this->reportError(this->getTokenFrom(start), "invalid unicode escape: `\\c%s'", str.c_str());
    return nullptr;
  }
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9': {
    const auto old = this->iter++;
    while (!this->isEnd() && isDigit(*this->iter)) {
      this->iter++;
    }
    StringRef tmp(old, static_cast<unsigned int>(this->iter - old));
    if (tmp == "0") {
      codePoint = '\0';
      goto CHAR;
    }
    return std::make_unique<BackRefNode>(this->getTokenFrom(start), tmp.toString(), false);
  }
  case 'b':
    this->iter++;
    return std::make_unique<BoundaryNode>(this->getTokenFrom(start), BoundaryNode::Type::WORD);
  case 'B':
    this->iter++;
    return std::make_unique<BoundaryNode>(this->getTokenFrom(start), BoundaryNode::Type::NOT_WORD);
  case 'd':
    this->iter++;
    return std::make_unique<PropertyNode>(this->getTokenFrom(start), PropertyNode::Type::DIGIT);
  case 'D':
    this->iter++;
    return std::make_unique<PropertyNode>(this->getTokenFrom(start), PropertyNode::Type::NOT_DIGIT);
  case 's':
    this->iter++;
    return std::make_unique<PropertyNode>(this->getTokenFrom(start), PropertyNode::Type::SPACE);
  case 'S':
    this->iter++;
    return std::make_unique<PropertyNode>(this->getTokenFrom(start), PropertyNode::Type::NOT_SPACE);
  case 'w':
    this->iter++;
    return std::make_unique<PropertyNode>(this->getTokenFrom(start), PropertyNode::Type::WORD);
  case 'W':
    this->iter++;
    return std::make_unique<PropertyNode>(this->getTokenFrom(start), PropertyNode::Type::NOT_WORD);
  case 'p':
  case 'P':
    if (this->flag.is(Mode::LEGACY)) {
      codePoint = static_cast<unsigned char>(*this->iter++);
      goto CHAR;
    }
    this->iter--;
    return this->parseUnicodePropertyEscape();
  case 'x': {
    this->iter++;
    const auto old = this->iter;
    codePoint = 0;
    for (unsigned int i = 0; i < 2 && !this->isEnd(); i++) {
      if (char ch = *this->iter++; isHex(ch)) {
        codePoint *= 16;
        codePoint += static_cast<int>(hexToNum(ch));
      } else {
        goto INVALID_HEX;
      }
    }
    if (this->iter - old == 2) {
      goto CHAR;
    }
  INVALID_HEX:
    if (this->flag.is(Mode::LEGACY)) {
      this->iter = old;
      codePoint = 'x';
      goto CHAR;
    }
    std::string str = StringRef(old, this->iter - old).toString();
    this->reportError(this->getTokenFrom(start), "invalid unicode escape: `\\x%s'", str.c_str());
    return nullptr;
  }
  case 'u': {
    this->iter--;
    codePoint = this->parseUnicodeEscape(this->flag.is(Mode::LEGACY));
    if (UnicodeUtil::isValidCodePoint(codePoint)) {
      goto CHAR;
    }
    if (this->flag.is(Mode::LEGACY) && !this->hasError()) {
      this->iter = start;
      this->iter += 2;
      codePoint = 'u';
      goto CHAR;
    }
    return nullptr;
  }
  case 'k':
    this->iter--;
    return this->parseNamedBackRef();
  default:
    if (this->flag.isEitherUnicodeMode() && (isSyntaxChar(*this->iter) || *this->iter == '/')) {
      codePoint = static_cast<unsigned char>(*this->iter++);
      goto CHAR;
    }
    codePoint = this->nextValidCodePoint();
    if (codePoint > -1) {
      if (this->flag.is(Mode::LEGACY)) {
        goto CHAR;
      }
      char data[5];
      unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, data);
      data[len] = '\0';
      this->reportError(this->getTokenFrom(start), "invalid escape: `\\%s'", data);
      return nullptr;
    }
    return nullptr;
  }

CHAR:
  assert(UnicodeUtil::isValidCodePoint(codePoint));
  return std::make_unique<CharNode>(this->getTokenFrom(start), codePoint);
}

int Parser::parseUnicodeEscapeBMP(const bool ignoreError) {
  const auto old = this->iter;
  int codePoint = 0;
  if (!this->startsWith("\\u")) {
    goto INVALID;
  }
  this->iter += 2;
  for (unsigned int i = 0; i < 4 && !this->isEnd(); i++) {
    if (char ch = *this->iter++; isHex(ch)) {
      codePoint *= 16;
      codePoint += static_cast<int>(hexToNum(ch));
    } else {
      goto INVALID;
    }
  }
  if (this->iter - old == 6) {
    return codePoint;
  }

INVALID:
  if (this->startsWith("\\")) {
    this->iter++;
  }
  if (!ignoreError) {
    this->reportError(this->getTokenFrom(old), "invalid unicode escape: `%s'",
                      StringRef(old, this->iter - old).toString().c_str());
  }
  return -1;
}

int Parser::parseUnicodeEscape(const bool mayIgnoreError) {
  int codePoint = 0;
  const auto old = this->iter;
  if (this->flag.isEitherUnicodeMode() && this->startsWith("\\u{")) {
    this->iter += 3;
    bool arithOverflow = false;
    while (!this->isEnd() && isHex(*this->iter)) {
      char ch = *this->iter++;
      if (!arithOverflow) {
        arithOverflow = smul_overflow(codePoint, 16, codePoint) ||
                        sadd_overflow(codePoint, static_cast<int>(hexToNum(ch)), codePoint);
      }
    }
    if (this->isEnd() || *this->iter != '}' || this->iter - old == 3 ||
        !UnicodeUtil::isCodePoint(codePoint)) {
      if (!this->isEnd()) {
        this->iter++;
      }
      if (!mayIgnoreError) {
        this->reportError(this->getTokenFrom(old), "invalid unicode escape: `%s'",
                          StringRef(old, this->iter - old).toString().c_str());
      }
      return -1;
    }
    this->iter++;
  } else {
    codePoint = this->parseUnicodeEscapeBMP(mayIgnoreError);
    if (codePoint < 0) {
      return -1;
    }
    if (UnicodeUtil::isHighSurrogate(codePoint)) {
      const auto old2 = this->iter;
      int low = this->parseUnicodeEscapeBMP(true); // always ignore error
      if (auto c = UnicodeUtil::utf16ToCodePoint(codePoint, low);
          UnicodeUtil::isValidCodePoint(c)) {
        codePoint = c;
      } else {
        this->iter = old2;
      }
    }
  }
  if (!UnicodeUtil::isValidCodePoint(codePoint)) {
    this->reportError(this->getTokenFrom(old),
                      "unicode escape generate invalid UTF-8 (surrogate): `U+%04X'", codePoint);
    return -1;
  }
  return codePoint;
}

std::unique_ptr<PropertyNode> Parser::parseUnicodePropertyEscape() {
  const auto old = this->iter;
  assert(!this->isEnd() && *this->iter == '\\');
  this->iter++;
  assert(!this->isEnd() && (*this->iter == 'p' || *this->iter == 'P'));
  const bool invert = *this->iter == 'P';
  this->iter++;
  if (!this->isEnd() && *this->iter == '{') {
    this->iter++;
    const auto leftStart = this->iter;
    while (!this->isEnd() && (*this->iter != '}' && *this->iter != '=')) {
      this->iter++;
    }
    const StringRef left(leftStart, this->iter - leftStart);
    if (this->isEnd()) {
      goto INVALID;
    }
    if (*this->iter == '}') {
      this->iter++;
      if (this->flag.is(Mode::UNICODE_SET)) {
        if (auto ret = ucp::parseEmojiProperty(left); ret.hasValue()) {
          return std::make_unique<PropertyNode>(this->getTokenFrom(old), ret.unwrap(), invert);
        }
      }
      std::string err;
      if (auto ret = ucp::parseProperty(left, &err); ret.hasValue()) {
        return std::make_unique<PropertyNode>(this->getTokenFrom(old), ret.unwrap(), invert);
      }
      this->reportError(this->getTokenFrom(old), "%s", err.c_str());
      return nullptr;
    }
    if (*this->iter == '=') {
      this->iter++;
      const auto rightStart = this->iter;
      while (!this->isEnd() && *this->iter != '}') {
        this->iter++;
      }
      if (this->isEnd() || *this->iter != '}' || left.empty()) {
        goto INVALID;
      }
      const StringRef right(rightStart, this->iter - rightStart);
      this->iter++;
      std::string err;
      if (auto ret = ucp::parseProperty(left, right, &err); ret.hasValue()) {
        return std::make_unique<PropertyNode>(this->getTokenFrom(old), ret.unwrap(), invert);
      }
      this->reportError(this->getTokenFrom(old), "%s", err.c_str());
      return nullptr;
    }
  }
INVALID:
  this->reportError(this->getTokenFrom(old), "invalid unicode property escape: `%s'",
                    StringRef(old, this->iter - old).toString().c_str());
  return nullptr;
}

std::unique_ptr<Node> Parser::parseNamedBackRef() {
  const auto old = this->iter;
  assert(this->startsWith("\\k"));
  this->iter += 2;
  auto name = this->parseCaptureGroupName(old, this->flag.is(Mode::LEGACY));
  if (name.empty()) {
    if (this->flag.is(Mode::LEGACY)) {
      this->iter = old + 2;
      return std::make_unique<CharNode>(this->getTokenFrom(old), 'k');
    }
    return nullptr;
  }
  return std::make_unique<BackRefNode>(this->getTokenFrom(old), std::move(name), true);
}

static bool isJSIdStartAscii(int codePoint) {
  return (codePoint >= 'a' && codePoint <= 'z') || (codePoint >= 'A' && codePoint <= 'Z') ||
         codePoint == '_' || codePoint == '$';
}

static bool isJSIdStart(CodePointSet &cache, const int codePoint) {
  if (codePoint <= 127) { // fast-path
    return isJSIdStartAscii(codePoint);
  }
  if (!cache) { // uninitialized
    cache = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ID_Start));
    assert(cache);
  }
  return cache.ref().contains(codePoint);
}

static bool isJSIdContinue(CodePointSet &cache, const int codePoint) {
  if (codePoint <= 127) { // fast-path
    return isJSIdStartAscii(codePoint) || (codePoint >= '0' && codePoint <= '9');
  }
  if (!cache) { // uninitialized
    cache = ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ID_Continue));
    assert(cache);
  }
  return cache.ref().contains(codePoint);
}

static void appendCodePoint(std::string &out, int codePoint) {
  assert(UnicodeUtil::isValidCodePoint(codePoint));
  char b[4];
  unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, b);
  out.append(b, len);
}

#define TRY_CP(E)                                                                                  \
  ({                                                                                               \
    if (this->isEnd()) {                                                                           \
      goto END;                                                                                    \
    }                                                                                              \
    int cp__ = (E);                                                                                \
    if (cp__ < 0) {                                                                                \
      return "";                                                                                   \
    }                                                                                              \
    cp__;                                                                                          \
  })

std::string Parser::parseCaptureGroupName(const char *prefixStart, const bool ignoreError) {
  const auto old = this->iter;
  if (!this->startsWith("<")) {
    if (!ignoreError) {
      this->reportError(this->getTokenFrom(prefixStart), "%s is not followed by <",
                        this->getStrRefFrom(prefixStart).toString().c_str());
    }
    return "";
  }
  this->iter++;

  std::string name = "<";

  // ID_Start
  int codePoint = TRY_CP(this->nextValidCodePoint());
  if (codePoint == '>') {
    name += '>';
    goto INVALID;
  }
  if (codePoint == '\\') {
    this->iter--;
    codePoint = TRY_CP(this->parseUnicodeEscape(ignoreError));
  }
  appendCodePoint(name, codePoint);
  if (!isJSIdStart(this->idStartSet, codePoint)) {
    goto INVALID;
  }

  // // ID_Continue
  while (true) {
    codePoint = TRY_CP(this->nextValidCodePoint());
    if (codePoint == '>') {
      name += '>';
      break;
    }
    if (codePoint == '\\') {
      this->iter--;
      codePoint = TRY_CP(this->parseUnicodeEscape(ignoreError));
    }
    appendCodePoint(name, codePoint);
    if (!isJSIdContinue(this->idContinueSet, codePoint)) {
      goto INVALID;
    }
  }
  return name;

END:
  if (name != "<") {
    if (!ignoreError) {
      this->reportError(this->getTokenFrom(old), "unclosed capture group name: `%s'", name.c_str());
    }
    return "";
  }

INVALID:
  if (!ignoreError) {
    this->reportError(this->getTokenFrom(old),
                      "capture group name must contain valid identifier: `%s'", name.c_str());
  }
  return "";
}

std::unique_ptr<Node> Parser::tryToParseQuantifier(std::unique_ptr<Node> &&node,
                                                   const bool ignoreError) {
  const auto old = this->iter;
  bool greedy = true;
  switch (*this->iter) {
  case '?':
    this->iter++;
    if (this->startsWith("?")) {
      this->iter++;
      greedy = false;
    }
    return RepeatNode::option(std::move(node), greedy, this->getTokenFrom(old));
  case '*':
    this->iter++;
    if (this->startsWith("?")) {
      this->iter++;
      greedy = false;
    }
    return RepeatNode::zeroOrMore(std::move(node), greedy, this->getTokenFrom(old));
  case '+':
    this->iter++;
    if (this->startsWith("?")) {
      this->iter++;
      greedy = false;
    }
    return RepeatNode::oneOrMore(std::move(node), greedy, this->getTokenFrom(old));
  case '{': {
    this->iter++;
    unsigned short min = 0;
    if (auto ret = this->parseQuantifierDigits(old, ignoreError, ','); ret.hasValue()) {
      min = ret.unwrap();
    } else if (ignoreError) {
      this->iter = old;
      return std::move(node);
    } else {
      return nullptr;
    }
    unsigned int max = min;
    if (this->isEnd()) {
      if (ignoreError) {
        this->iter = old;
        return std::move(node);
      }
      this->reportError(this->getTokenFrom(old), "unclosed quantifier: `%s'",
                        this->getStrRefFrom(old).toString().c_str());
      return nullptr;
    }
    if (*this->iter == ',') {
      this->iter++;
      if (this->startsWith("}")) {
        max = RepeatNode::UNLIMIT;
      } else {
        if (auto ret = this->parseQuantifierDigits(old, ignoreError, '}'); ret.hasValue()) {
          max = ret.unwrap();
        } else if (ignoreError) {
          this->iter = old;
          return std::move(node);
        } else {
          return nullptr;
        }
      }
    }
    if (!this->startsWith("}")) {
      if (!this->isEnd()) {
        this->iter++;
      }
      if (ignoreError) {
        this->iter = old;
        return std::move(node);
      }
      this->reportError(this->getTokenFrom(old), "unclosed quantifier: `%s'",
                        this->getStrRefFrom(old).toString().c_str());
      return nullptr;
    }
    this->iter++;
    if (this->startsWith("?")) {
      this->iter++;
      greedy = false;
    }
    if (min > max) {
      this->reportError(this->getTokenFrom(old), "numbers out of order in {} quantifier");
      return nullptr;
    }
    return std::make_unique<RepeatNode>(std::move(node), min, max, greedy, this->getTokenFrom(old));
  }
  default:
    return std::move(node);
  }
}

Optional<unsigned short> Parser::parseQuantifierDigits(const char *prefixStart,
                                                       const bool ignoreError, const char end) {
  const auto old = this->iter;
  std::string digits;
  while (!this->isEnd() && *this->iter != end && *this->iter != '}') {
    digits += *this->iter++;
  }
  if (digits.empty()) {
    if (!this->isEnd()) {
      this->iter++;
    }
    if (!ignoreError) {
      this->reportError(this->getTokenFrom(prefixStart), "invalid quantifier: `%s'",
                        this->getStrRefFrom(prefixStart).toString().c_str());
    }
    return {};
  }

  auto ret = convertToNum10<uint64_t>(digits.c_str(), digits.c_str() + digits.size());
  if (!ret) {
    if (!ignoreError) {
      this->reportError(this->getTokenFrom(old), "must be positive decimal number: `%s'",
                        digits.c_str());
    }
    return {};
  }
  if (ret.value > RepeatNode::QUANTIFIER_MAX) {
    if (!ignoreError) {
      this->reportError(this->getTokenFrom(old), "too large quantifier number: `%s'",
                        digits.c_str());
    }
    return {};
  }
  return static_cast<unsigned short>(ret.value);
}

bool Parser::check(std::unique_ptr<Node> &node) {
  bool r = this->checkBackRef(node); // TODO: more extra syntax check
  if (this->overflow) {
    this->reportOverflow();
    return false;
  }
  return r;
}

#define GOTO_NEXT(FS, F)                                                                           \
  do {                                                                                             \
    (FS).back().index++;                                                                           \
    (FS).push(F);                                                                                  \
    if ((FS).size() == STACK_DEPTH_LIMIT) {                                                        \
      this->overflow = true;                                                                       \
      return false;                                                                                \
    }                                                                                              \
    goto NEXT;                                                                                     \
  } while (false)

static std::vector<std::unique_ptr<Node>> split(const BackRefNode &refNode) {
  std::vector<std::unique_ptr<Node>> nodes;
  if (refNode.isNamed()) {
    // \k
    Token token = {refNode.getToken().pos, 2};
    nodes.push_back(std::make_unique<CharNode>(token, 'k'));

    // <identifier>
    const char *iter = refNode.getName().c_str();
    const char *end = refNode.getName().c_str() + refNode.getName().size();
    while (iter != end) {
      int codePoint = -1;
      const unsigned int len = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint);
      assert(len);
      iter += len;
      token = {token.endPos(), len};
      nodes.push_back(std::make_unique<CharNode>(token, codePoint));
    }
  } else { // TODO
  }
  return nodes;
}

bool Parser::checkBackRef(std::unique_ptr<Node> &node) {
  for (this->checkerFrames.push(CheckerFrame(node)); this->checkerFrames.size();
       this->checkerFrames.pop()) {
  NEXT: {
    auto &frame = this->checkerFrames.back();
    auto &curNode = **frame.node;
    if (isa<BackRefNode>(curNode) && frame.index == 0) {
      auto &refNode = cast<BackRefNode>(curNode);
      if (auto s = this->resolveCaptureGroup(refNode); s == BackRefResolveStatus::ERROR) {
        return false;
      } else if (s == BackRefResolveStatus::REPLACE) {
        auto nodes = split(refNode);
        auto seqNode = std::make_unique<SeqNode>(std::move(nodes[0]), std::move(nodes[1]));
        for (unsigned int i = 2; i < nodes.size(); i++) {
          seqNode->append(std::move(nodes[i]));
        }
        frame.node->reset(seqNode.release());
      }
      continue;
    }
    if (isa<NestedNode>(curNode) && frame.index == 0) {
      auto &nestedNode = cast<NestedNode>(curNode);
      if (auto *refNode = checked_cast<BackRefNode>(nestedNode.getPattern().get())) {
        if (auto s = this->resolveCaptureGroup(*refNode); s == BackRefResolveStatus::ERROR) {
          return false;
        } else if (s == BackRefResolveStatus::REPLACE) {
          auto nodes = split(*refNode);
          auto seqNode = std::make_unique<SeqNode>(std::move(nodes[0]), std::move(nodes[1]));
          for (unsigned int i = 2; i < nodes.size(); i++) {
            seqNode->append(std::move(nodes[i]));
          }
          nestedNode.refPattern() = std::move(seqNode);
        }
        continue;
      }
      GOTO_NEXT(this->checkerFrames, CheckerFrame(nestedNode.refPattern()));
    }
    if (isa<ListNode>(curNode) && frame.index < cast<ListNode>(curNode).refPatterns().size()) {
      auto &listNode = cast<ListNode>(curNode);
      auto &cNode = listNode.refPatterns()[frame.index];
      if (auto *refNode = checked_cast<BackRefNode>(cNode.get())) {
        if (auto s = this->resolveCaptureGroup(*refNode); s == BackRefResolveStatus::ERROR) {
          return false;
        } else if (s == BackRefResolveStatus::REPLACE) {
          auto nodes = split(*refNode);
          listNode.refPatterns().erase(listNode.refPatterns().begin() + frame.index);
          auto i = listNode.refPatterns().begin() + frame.index;
          for (auto &e : nodes) {
            i = listNode.refPatterns().insert(i, std::move(e));
          }
        }
        frame.index++;
        goto NEXT;
      }
      GOTO_NEXT(this->checkerFrames, CheckerFrame(cNode));
    }
  }
  }
  return true;
}

Parser::BackRefResolveStatus Parser::resolveCaptureGroup(BackRefNode &refNode) {
  if (refNode.isNamed()) {
    auto &name = refNode.getName();
    if (auto i = this->namedCaptureGroups.find(name); i != this->namedCaptureGroups.end()) {
      refNode.setGroupIndex(i->second);
      return BackRefResolveStatus::OK;
    }
    if (this->flag.is(Mode::LEGACY)) {
      return BackRefResolveStatus::REPLACE;
    }
    this->reportError(refNode.getToken(), "undefined capture group name: `%s'", name.c_str());
    return BackRefResolveStatus::ERROR;
  }
  auto &value = refNode.getName(); // TODO: resolve index
  this->reportError(refNode.getToken(), "capture group index is out-of-range: `%s'", value.c_str());
  return BackRefResolveStatus::ERROR;
}

} // namespace arsh::regex