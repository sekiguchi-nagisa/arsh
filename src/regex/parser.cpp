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
        append(node, std::move(r));
        continue;
      }
      return nullptr;
    }
    case '.':
      append(node, std::make_unique<AnyNode>(this->curToken()));
      this->iter++;
      continue;
    case '*':
    case '+':
    case '?': {
      this->reportError(this->curToken(), "`%c' quantifier does not follow atom", *this->iter);
      return nullptr;
    }
    case '(':
      break; // TODO
    case ')':
      this->reportError(this->curToken(), "unmatched `)'");
      return nullptr;
    case '[':
      break; // TODO
    case ']':
      if (this->flag.isEitherUnicodeMode()) {
        this->reportError(this->curToken(), "unmatched `]'");
        return nullptr;
      }
      goto CHAR;
    case '{':
    case '}':
      if (this->flag.isEitherUnicodeMode()) {
        this->reportError(this->curToken(), "lone quantifier bracket `%c'", *this->iter);
        return nullptr;
      }
      goto CHAR;
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
    default:
      break;
    }

  CHAR:
    int codePoint = -1;
    if (unsigned int len = UnicodeUtil::utf8ToCodePoint(this->iter, this->end(), codePoint)) {
      append(node, std::make_unique<CharNode>(this->curToken(len), codePoint));
      this->iter += len;
    } else {
      this->reportError(this->curToken(), "invalid UTF-8 byte: `%02x'", *this->iter);
      return nullptr;
    }
  }
  if (!node) {
    node = std::make_unique<EmptyNode>(this->curPos());
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
    if (this->flag.is(Mode::BMP)) {
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
    return std::make_unique<BackRefNode>(this->getTokenFrom(start), tmp, false);
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
    if (this->flag.is(Mode::BMP)) {
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
    if (this->flag.is(Mode::BMP)) {
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
    codePoint = this->parseUnicodeEscape(this->flag.is(Mode::BMP));
    if (UnicodeUtil::isValidCodePoint(codePoint)) {
      goto CHAR;
    }
    if (this->flag.is(Mode::BMP) && !this->hasError()) {
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
    if (unsigned int len = UnicodeUtil::utf8ToCodePoint(this->iter, this->end(), codePoint)) {
      StringRef tmp(this->iter, len);
      this->iter += len;
      if (this->flag.is(Mode::BMP)) {
        goto CHAR;
      }
      this->reportError(this->getTokenFrom(start), "invalid escape: `\\%s'",
                        tmp.toString().c_str());
      return nullptr;
    }
    this->reportError(this->curToken(), "invalid UTF-8 byte: `%02x'", *this->iter);
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

std::unique_ptr<BackRefNode> Parser::parseNamedBackRef() { // TODO
  const auto old = this->iter;
  assert(this->startsWith("\\k"));
  this->iter += 2;
  (void)old;
  return nullptr;
}

std::string Parser::parseCaptureGroupName() { // TODO
  std::string name;

  return name;
}

} // namespace arsh::regex