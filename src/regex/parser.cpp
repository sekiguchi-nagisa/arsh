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

// ###########################
// ##     CharClassNode     ##
// ###########################

static bool mayContainStringsImpl(const Node &node) {
  if (isa<PropertyNode>(node)) {
    return cast<PropertyNode>(node).getType() == PropertyNode::Type::EMOJI;
  }
  if (isa<CharClassNode>(node)) {
    return cast<CharClassNode>(node).mayContainStrings();
  }
  return false; // TODO: \q{substring}
}

bool CharClassNode::finalize(Token endToken) {
  this->updateToken(endToken);
  this->u32 = 0;
  switch (this->getType()) {
  case Type::UNION:
  case Type::RANGE:
    for (auto &e : this->getChars()) {
      if (mayContainStringsImpl(*e)) {
        this->u32++;
        goto END;
      }
    }
    break;
  case Type::INTERSECT:
    for (auto &e : this->getChars()) {
      if (isCharClassOp(*e)) {
        continue;
      }
      if (!mayContainStringsImpl(*e)) {
        this->u32 = 0;
        goto END;
      }
    }
    this->u32++;
    break;
  case Type::SUBTRACT:
    if (mayContainStringsImpl(*this->getChars()[0])) {
      this->u32++;
    }
    break;
  }
END:
  if (this->isInvert()) {
    return !this->mayContainStrings();
  }
  return true;
}

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

void Parser::append(std::unique_ptr<Node> &&node) {
  auto &frame = this->frames.back();
  if (!frame.node) {
    frame.node = std::move(node);
  } else if (isa<AltNode>(*frame.node)) {
    cast<AltNode>(*frame.node).appendToLast(std::move(node));
  } else if (isa<SeqNode>(*frame.node)) {
    cast<SeqNode>(*frame.node).append(std::move(node));
  } else {
    frame.node = std::make_unique<SeqNode>(std::move(frame.node), std::move(node));
  }
}

SyntaxTree Parser::operator()(const StringRef src, const Flag f) {
  this->ref = src;
  this->flag = f;
  this->overflow = false;
  this->captureGroupResolved = false;
  this->captureGroupCount = 0;
  this->prefetchedCaptureGroupCount = 0;
  this->prefetchedNamedCaptureGroupCount = 0;
  this->namedCaptureGroups.clear();
  this->iter = this->begin();
  this->namedRefNodes.clear();
  this->error.reset();
  this->frames.clear();

  // actual parse function
  auto node = this->parse();

  // build NamedCaptureGroups
  using Entry = std::pair<std::string, NamedCaptureEntry>;
  std::vector<Entry> entries;
  for (auto &e : this->namedCaptureGroups) {
    auto entry = e.second.size() == 1 ? NamedCaptureEntry(e.second[0])
                                      : NamedCaptureEntry(std::move(e.second));
    entries.emplace_back(std::move(e.first), std::move(entry)); // NOLINT
  }
  std::sort(entries.begin(), entries.end(), [](const Entry &x, const Entry &y) {
    unsigned int xi = x.second.hasMultipleIndices() ? x.second[0] : x.second.getIndex();
    unsigned int yi = y.second.hasMultipleIndices() ? y.second[0] : y.second.getIndex();
    return xi < yi;
  });
  StrRefMap<unsigned short> offsetMap;
  for (unsigned int i = 0; i < entries.size(); i++) {
    offsetMap.emplace(entries[i].first, static_cast<unsigned short>(i));
  }

  // check named-reference
  if (node) {
    for (auto &e : this->namedRefNodes) {
      auto &name = e->getName();
      if (auto i = offsetMap.find(name); i != offsetMap.end()) {
        e->setIndex(i->second);
      } else {
        this->reportUndefinedNamedRef(e->getToken(), name.c_str());
        node = nullptr;
        break;
      }
    }
  }

  return {this->flag, this->captureGroupCount, std::move(node),
          NamedCaptureGroups(std::move(offsetMap), std::move(entries))};
}

static bool isQuantifierStart(char ch) {
  switch (ch) {
  case '?':
  case '*':
  case '+':
  case '{':
    return true;
  default:
    return false;
  }
}

std::unique_ptr<Node> Parser::parse() {
  this->frames.emplace_back();

  while (!this->isEnd()) {
    std::unique_ptr<Node> atomNode;
    switch (*this->iter) {
    case '^':
      this->append(std::make_unique<BoundaryNode>(this->curToken(), BoundaryNode::Type::START));
      this->iter++;
      continue;
    case '$':
      this->append(std::make_unique<BoundaryNode>(this->curToken(), BoundaryNode::Type::END));
      this->iter++;
      continue;
    case '\\': {
      if (auto r = this->parseAtomEscape(false)) {
        if (isa<BoundaryNode>(*r)) {
          this->append(std::move(r));
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
      if (this->enterGroup()) {
        continue;
      }
      return nullptr;
    case ')':
      if (auto node = this->exitGroup()) {
        if (isa<LookAroundNode>(*node) && !this->isEnd() && isQuantifierStart(*this->iter)) {
          if (auto &la = cast<LookAroundNode>(*node);
              this->flag.is(Mode::BMP) && (la.getType() == LookAroundNode::Type::LOOK_AHEAD ||
                                           la.getType() == LookAroundNode::Type::LOOK_AHEAD_NOT)) {
            atomNode = std::move(node);
            goto REPEAT; // only allow look-ahead in legacy mode
          }
          this->reportError(this->curToken(), "`%c' quantifier is not allowed after lookaround",
                            *this->iter);
          return nullptr;
        }
        atomNode = std::move(node);
        goto REPEAT;
      }
      return nullptr;
    case '[':
      atomNode = this->parseCharClass();
      goto REPEAT;
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
    case '|': {
      auto node = std::move(this->curNode());
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
      this->curNode() = std::move(node);
      cast<AltNode>(*this->curNode()).appendNull(); // for next alternative
      this->frames.back().clearAndMergeGroupNames();
      this->iter++;
      continue;
    }
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
    atomNode = this->tryToParseQuantifier(std::move(atomNode), this->flag.is(Mode::BMP));
    if (!atomNode) {
      return nullptr;
    }
    this->append(std::move(atomNode));
  }

  if (this->frames.size() > 1) {
    this->reportUnclosedGroup(this->getTokenFrom(this->iter));
    return nullptr;
  }
  auto node = std::move(this->curNode());
  this->frames.pop_back();
  if (!node) {
    node = std::make_unique<EmptyNode>(this->curPos());
  } else if (isa<AltNode>(*node) && !cast<AltNode>(*node).getPatterns().back()) {
    cast<AltNode>(*node).appendToLast(std::make_unique<EmptyNode>(this->curPos()));
  }
  return node;
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

static bool isClassSetReservedPunctuator(const char ch) {
  switch (ch) {
  case '&':
  case '-':
  case '!':
  case '#':
  case '%':
  case ',':
  case ':':
  case ';':
  case '<':
  case '=':
  case '>':
  case '@':
  case '`':
  case '~':
    return true;
  default:
    return false;
  }
}

static bool isClassSetSyntaxCharacter(const char ch) {
  switch (ch) {
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '/':
  case '-':
  case '\\':
  case '|':
    return true;
  default:
    return false;
  }
}

static bool startsWithClassSetReservedDoublePunctuator(const StringRef ref) {
  if (ref.size() < 2) {
    return false;
  }
  switch (ref[0]) {
  case '&':
  case '!':
  case '#':
  case '$':
  case '%':
  case '*':
  case '+':
  case ',':
  case '.':
  case ':':
  case ';':
  case '<':
  case '=':
  case '>':
  case '?':
  case '@':
  case '^':
  case '`':
  case '~':
    return ref[0] == ref[1];
  default:
    return false;
  }
}

std::unique_ptr<Node> Parser::parseAtomEscape(const bool inCharClass) {
  assert(this->startsWith("\\"));
  const auto start = this->iter++;
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
    this->reportError(this->getTokenFrom(start), "invalid escape: `\\c%s'", str.c_str());
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
  case '9':
    this->iter--;
    return this->parseBackRefOrOctal(inCharClass);
  case 'b':
    this->iter++;
    if (inCharClass) {
      return std::make_unique<CharNode>(this->getTokenFrom(start), '\b');
    }
    return std::make_unique<BoundaryNode>(this->getTokenFrom(start), BoundaryNode::Type::WORD);
  case 'B':
    this->iter++;
    if (inCharClass) {
      if (this->flag.isEitherUnicodeMode()) {
        this->reportError(this->getTokenFrom(start), "invalid escape: `\\B'");
        return nullptr;
      }
      return std::make_unique<CharNode>(this->getTokenFrom(start), 'B');
    }
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
    this->reportError(this->getTokenFrom(start), "invalid escape: `\\x%s'", str.c_str());
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
    return this->parseNamedBackRef(inCharClass);
  case '-':
    this->iter++;
    if (inCharClass || !this->flag.isEitherUnicodeMode()) {
      return std::make_unique<CharNode>(this->getTokenFrom(start), '-');
    }
    this->reportError(this->getTokenFrom(start), "invalid escape: `\\-'");
    return nullptr;
  default:
    if (this->flag.isEitherUnicodeMode() && (isSyntaxChar(*this->iter) || *this->iter == '/')) {
      codePoint = static_cast<unsigned char>(*this->iter++);
      goto CHAR;
    }
    if (inCharClass && this->flag.is(Mode::UNICODE_SET) &&
        isClassSetReservedPunctuator(*this->iter)) {
      codePoint = static_cast<unsigned char>(*this->iter++);
      goto CHAR;
    }
    codePoint = this->nextValidCodePoint();
    if (codePoint > -1) {
      if (this->flag.is(Mode::BMP)) {
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
      if (this->flag.is(Mode::UNICODE_SET) && !invert) {
        if (auto ret = ucp::parseEmojiProperty(left); ret.hasValue()) {
          return std::make_unique<PropertyNode>(this->getTokenFrom(old), ret.unwrap());
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

std::unique_ptr<Node> Parser::parseBackRefOrOctal(const bool inCharClass) {
  assert(this->startsWith("\\"));
  const auto start = this->iter;
  this->iter++; // consume '\\'
  const auto old = this->iter;
  while (!this->isEnd() && isDigit(*this->iter)) {
    this->iter++;
  }
  StringRef tmp(old, static_cast<unsigned int>(this->iter - old));
  assert(!tmp.empty());
  if (tmp == "0") {
    return std::make_unique<CharNode>(this->getTokenFrom(start), '\0');
  }
  if (tmp[0] == '0') { // octal
    if (this->flag.isEitherUnicodeMode()) {
      this->reportError(this->getTokenFrom(start), "invalid decimal escape: `%s'",
                        tmp.toString().c_str());
      return nullptr;
    }
  } else if (!inCharClass) {
    auto ret = convertToNum10<unsigned int>(tmp.begin(), tmp.end());
    assert(ret);
    this->resolveCaptureGroups();
    if (ret.value <= this->prefetchedCaptureGroupCount && ret.value > 0) {
      return std::make_unique<BackRefNode>(this->getTokenFrom(start), ret.value);
    }
    if (this->flag.isEitherUnicodeMode()) {
      this->reportError(this->getTokenFrom(start),
                        "backref index is greater than capture group count: `%d'", ret.value);
      return nullptr;
    }
  }

  // treat as octal
  if (this->flag.isEitherUnicodeMode()) {
    this->reportError(this->getTokenFrom(start), "invalid decimal escape: `%s'",
                      tmp.toString().c_str());
    return nullptr;
  }
  this->iter = old;
  if (!isOctal(*this->iter)) {
    char ch = *this->iter++;
    return std::make_unique<CharNode>(this->getTokenFrom(start), ch);
  }
  int codePoint = 0;
  for (unsigned int i = 0; i < 3 && !this->isEnd() && isOctal(*this->iter); i++) {
    const int oldCodePoint = codePoint;
    codePoint *= 8;
    codePoint += (*this->iter - '0');
    if (codePoint >= UINT8_MAX) {
      codePoint = oldCodePoint;
      break;
    }
    this->iter++;
  }
  return std::make_unique<CharNode>(this->getTokenFrom(start), codePoint);
}

std::unique_ptr<Node> Parser::parseNamedBackRef(const bool inCharClass) {
  const auto old = this->iter;
  assert(this->startsWith("\\k"));
  this->iter += 2;
  if (inCharClass) {
    if (this->flag.isEitherUnicodeMode()) {
      this->reportError(this->getTokenFrom(old), "invalid escape: `\\k'");
      return nullptr;
    }
    return std::make_unique<CharNode>(this->getTokenFrom(old), 'k');
  }
  auto name = this->parseCaptureGroupName(old, this->flag.is(Mode::BMP));
  if (!name.empty()) {
    this->resolveCaptureGroups();
  }
  if (name.empty() || !this->hasNameCaptureGroup()) {
    if (this->flag.is(Mode::BMP)) {
      this->iter = old + 2;
      return std::make_unique<CharNode>(this->getTokenFrom(old), 'k');
    }
    if (!name.empty()) {
      this->reportUndefinedNamedRef(this->getTokenFrom(old), name.c_str());
    }
    return nullptr;
  }
  auto node = std::make_unique<BackRefNode>(this->getTokenFrom(old), std::move(name));
  this->namedRefNodes.push_back(node.get());
  return node;
}

unsigned int Parser::newNamedCaptureGroup(const char *prefixStart, std::string &&name) {
  const auto end = this->frames.rend();
  for (auto cur = this->frames.rbegin(); cur != end; ++cur) {
    if (auto i = cur->existingGroupNames.find(name); i != cur->existingGroupNames.end()) {
      this->reportError(this->getTokenFrom(prefixStart), "duplicated capture group name: `%s'",
                        name.c_str());
      return 0;
    }
  }
  if (const unsigned int index = this->newCaptureGroup(prefixStart)) {
    this->frames.back().existingGroupNames.emplace(name);
    if (auto i = this->namedCaptureGroups.find(name); i != this->namedCaptureGroups.end()) {
      i->second.push_back(index);
    } else {
      FlexBuffer<unsigned int> values;
      values.push_back(index);
      this->namedCaptureGroups.emplace(std::move(name), std::move(values));
    }
    return index;
  }
  return 0;
}

void Parser::resolveCaptureGroups() {
  if (this->captureGroupResolved) {
    return;
  }
  const auto old = this->iter;
  this->captureGroupResolved = true;
  this->prefetchedCaptureGroupCount = this->captureGroupCount;
  this->prefetchedNamedCaptureGroupCount = this->namedCaptureGroups.size();
  int classLevel = 0;
  while (!this->isEnd()) {
    switch (*this->iter) {
    case '\\':
      this->iter++;
      if (!this->isEnd()) {
        this->iter++; // skip next char
      }
      continue;
    case '(':
      this->iter++;
      if (classLevel != 0) { // within char class
        continue;
      }
      if (this->startsWith("?")) {
        this->iter++;
        if (this->startsWith("<")) {
          this->iter++;
          if (!this->startsWith("=") && !this->startsWith("!")) { // maybe named capture group
            this->prefetchedCaptureGroupCount++;
            this->prefetchedNamedCaptureGroupCount++;
          }
        }
      } else {
        this->prefetchedCaptureGroupCount++;
      }
      continue;
    case '[':
      classLevel++;
      continue;
    case ']':
      classLevel--;
      continue;
    default:
      this->iter++;
      break;
    }
  }
  this->iter = old;
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

  std::string name;

  // ID_Start
  int codePoint = TRY_CP(this->nextValidCodePoint());
  if (codePoint == '>') {
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
  if (!name.empty() && this->isEnd()) {
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

Optional<Modifier> Parser::parseModifiers(char end) {
  const auto old = this->iter;
  std::string value;
  while (!this->isEnd() && *this->iter != end && *this->iter != ':' && *this->iter != ')') {
    value += *this->iter++;
  }
  std::string err;
  auto ret = Flag::parseModifier(value, &err);
  if (!ret.hasValue()) {
    this->reportError(this->getTokenFrom(old), "%s", err.c_str());
    return {};
  }
  return ret.unwrap();
}

bool Parser::enterGroup() {
  assert(this->startsWith("("));
  const auto old = this->iter;
  this->iter++;
  if (this->frames.size() == STACK_DEPTH_LIMIT) {
    this->reportOverflow(this->getTokenFrom(old));
    return false;
  }
  if (!this->startsWith("?")) { // capture group
    if (unsigned int index = this->newCaptureGroup(old)) {
      this->frames.emplace_back(this->getTokenFrom(old), GroupNode::Type::CAPTURE, index);
      return true;
    }
    return false;
  }
  this->iter++; // consume ?
  if (this->isEnd()) {
    goto INVALID;
  }
  switch (*this->iter) {
  case '=':
    this->iter++;
    this->frames.emplace_back(this->getTokenFrom(old), LookAroundNode::Type::LOOK_AHEAD);
    return true;
  case '!':
    this->iter++;
    this->frames.emplace_back(this->getTokenFrom(old), LookAroundNode::Type::LOOK_AHEAD_NOT);
    return true;
  case ':':
    this->iter++;
    this->frames.emplace_back(this->getTokenFrom(old), GroupNode::Type::NON_CAPTURE, 0);
    return true;
  default:
    break;
  }
  if (this->startsWith("<=")) {
    this->iter += 2;
    this->frames.emplace_back(this->getTokenFrom(old), LookAroundNode::Type::LOOK_BEHIND);
    return true;
  }
  if (this->startsWith("<!")) {
    this->iter += 2;
    this->frames.emplace_back(this->getTokenFrom(old), LookAroundNode::Type::LOOK_BEHIND_NOT);
    return true;
  }
  if (*this->iter == '<') {
    auto name = this->parseCaptureGroupName(old, false);
    if (name.empty()) {
      return false;
    }
    if (const unsigned int index = this->newNamedCaptureGroup(old, std::move(name))) {
      this->frames.emplace_back(this->getTokenFrom(old), GroupNode::Type::CAPTURE, index);
      return true;
    }
    return false;
  }

  // parse modifiers
  {
    Modifier set = Modifier::NONE;
    if (auto ret = this->parseModifiers('-'); ret.hasValue()) {
      set = ret.unwrap();
    } else {
      return false;
    }
    Modifier unset = Modifier::NONE;
    if (this->startsWith("-")) {
      this->iter++;
      if (auto ret = this->parseModifiers(':'); ret.hasValue()) {
        unset = ret.unwrap();
      } else {
        return false;
      }
    }
    if (set == unset && set == Modifier::NONE) { // (?-)
      goto INVALID;
    }
    if (!this->startsWith(":")) {
      goto INVALID;
    }
    this->iter++;
    constexpr std::pair<Modifier, char> targets[] = {
#define GEN_TABLE(E, S, D) {Modifier::E, S},
        EACH_RE_MODIFIER(GEN_TABLE)
#undef GEN_TABLE
    };
    for (auto &[m, c] : targets) {
      if (hasFlag(set, m) && hasFlag(unset, m)) {
        this->reportError(this->getTokenFrom(old), "repeated modifier in group: `%c'", c);
        return false;
      }
    }
    this->frames.emplace_back(this->getTokenFrom(old), set, unset);
    return true;
  }

INVALID:
  this->reportError(this->getTokenFrom(old), "invalid group: `%s'",
                    this->getStrRefFrom(old).toString().c_str());
  return false;
}

std::unique_ptr<Node> Parser::exitGroup() {
  assert(this->startsWith(")"));
  if (this->frames.size() == 1) {
    this->reportError(this->curToken(), "unmatched `)'");
    return nullptr;
  }
  const auto old = this->iter;
  this->iter++;
  auto &frame = this->frames.back();
  assert(frame.type != FrameType::NONE);
  auto node = std::move(frame.node);
  if (!node) {
    node = std::make_unique<EmptyNode>(this->getTokenFrom(old).pos);
  } else if (isa<AltNode>(*node) && !cast<AltNode>(*node).getPatterns().back()) {
    cast<AltNode>(*node).appendToLast(std::make_unique<EmptyNode>(this->getTokenFrom(old).pos));
  }
  switch (frame.type) {
  case FrameType::NONE:
    break; // unreachable
  case FrameType::GROUP:
    node = std::make_unique<GroupNode>(frame.start, frame.groupType, frame.groupIndex,
                                       frame.setModifiers, frame.unsetModifiers, std::move(node),
                                       this->getTokenFrom(old));
    break;
  case FrameType::LOOK_AROUND:
    node = std::make_unique<LookAroundNode>(frame.start, frame.lookaroundType, std::move(node),
                                            this->getTokenFrom(old));
    break;
  }
  this->frames.back().clearAndMergeGroupNames();
  auto mergedNames = std::move(this->frames.back().mergedExistingGroupNames);
  this->frames.pop_back();
  this->frames.back().existingGroupNames.insert(mergedNames.begin(), mergedNames.end());
  return node;
}

std::unique_ptr<Node> Parser::parseCharClass() {
  assert(this->startsWith("["));
  int classLevel = 0;
  while (!this->isEnd()) {
    std::unique_ptr<Node> node;
    switch (*this->iter) {
    case '\\':
      if (auto atomNode = this->parseAtomEscape(true)) {
        node = std::move(atomNode);
        break;
      }
      return nullptr;
    case '[':
      if (classLevel == 0 || this->flag.is(Mode::UNICODE_SET)) {
        const auto old = this->iter++;
        if (this->frames.size() == STACK_DEPTH_LIMIT) {
          this->reportOverflow(this->getTokenFrom(old));
          return nullptr;
        }
        classLevel++;
        bool invert = false;
        if (this->startsWith("^")) {
          this->iter++;
          invert = true;
        }
        this->frames.emplace_back(std::make_unique<CharClassNode>(this->getTokenFrom(old), invert));
        continue;
      }
      node = std::make_unique<CharNode>(this->curToken(), '[');
      this->iter++;
      break;
    case ']': {
      if (!this->curCharClass().finalize(this->curToken())) {
        this->reportError(this->curCharClass().getToken(),
                          "negated character class may contain strings");
        return nullptr;
      }
      this->iter++;
      classLevel--;
      if (classLevel) {
        node = std::move(this->frames.back().node);
        this->frames.pop_back();
        break;
      }
      goto END;
    }
    default: {
      if (this->flag.is(Mode::UNICODE_SET)) {
        const auto old = this->iter;
        if (const auto rem = this->remain(); startsWithClassSetReservedDoublePunctuator(rem)) {
          const auto op = rem.substr(0, 2).toString();
          this->iter += 2;
          this->reportError(this->getTokenFrom(old),
                            "invalid set operation in character class: `%s'", op.c_str());
          return nullptr;
        }
        if (!this->isEnd() && isClassSetSyntaxCharacter(*this->iter)) {
          const char ch = *this->iter++;
          this->reportError(this->getTokenFrom(old), "invalid character in character class: `%c'",
                            ch);
          return nullptr;
        }
      }
      // consume char
      const auto o = this->iter;
      int codePoint = this->nextValidCodePoint();
      if (codePoint == -1) {
        return nullptr;
      }
      node = std::make_unique<CharNode>(this->getTokenFrom(o), codePoint);
      break;
    }
    }

    // check char range
    if (this->curCharClass().hasUnterminatedCharOp() && this->curCharClass().isUnionOrRange()) {
      auto &left = cast<CharNode>(
          *this->curCharClass().getChars()[this->curCharClass().getChars().size() - 2]);
      if (!isa<CharNode>(*node)) {
        if (this->flag.is(Mode::BMP)) {
          auto token = this->curCharClass().getChars().back()->getToken();
          this->curCharClass().refChars().pop_back();
          this->curCharClass().add(std::make_unique<CharNode>(token, '-'));
        } else {
          auto old = this->begin() + left.getToken().pos;
          this->reportError(this->getTokenFrom(old), "invalid character range");
          return nullptr;
        }
      } else {
        auto &right = cast<CharNode>(*node);
        if (left.getCodePoint() > right.getCodePoint()) {
          auto old = this->begin() + left.getToken().pos;
          this->reportError(this->getTokenFrom(old), "character range out of order");
          return nullptr;
        }
      }
    }

    // resolve set operation
    if (this->flag.is(Mode::UNICODE_SET)) {
      auto &curClass = this->curCharClass();
      if (curClass.hasUnterminatedCharOp() || curCharClass().isUnionOrRange()) {
        curClass.add(std::move(node));
      } else {
        const auto cur = this->begin() + curClass.getToken().pos;
        this->reportError(this->getTokenFrom(cur), "cannot combine different set operation");
        return nullptr;
      }
      const auto old = this->iter;
      Token op;
      Optional<PropertyNode::Type> opType;
      auto classType = CharClassNode::Type::UNION;
      if (this->startsWith("&&")) {
        this->iter += 2;
        if (this->startsWith("&")) { // not allow `&&&`
          const auto old2 = this->iter;
          const char ch = *this->iter++;
          this->reportError(this->getTokenFrom(old2), "invalid character in character class: `%c'",
                            ch);
          return nullptr;
        }
        op = this->getTokenFrom(old);
        opType = PropertyNode::Type::INTERSECT;
        classType = CharClassNode::Type::INTERSECT;
      } else if (this->startsWith("--")) {
        this->iter += 2;
        op = this->getTokenFrom(old);
        opType = PropertyNode::Type::SUBTRACT;
        classType = CharClassNode::Type::SUBTRACT;
      } else if (this->startsWith("-")) {
        this->iter += 1;
        op = this->getTokenFrom(old);
        opType = PropertyNode::Type::RANGE;
        classType = CharClassNode::Type::RANGE;
      }
      if (classType != CharClassNode::Type::UNION) {
        if (!curClass.isCompatible(classType)) {
          const auto cur = this->begin() + curClass.getToken().pos;
          this->reportError(this->getTokenFrom(cur), "cannot combine different set operation");
          return nullptr;
        }
        curClass.setType(classType);
        if (classType == CharClassNode::Type::RANGE &&
            !isa<CharNode>(*curClass.getChars().back())) {
          const auto cur = this->begin() + curClass.getChars().back()->getToken().pos;
          this->reportError(this->getTokenFrom(cur), "invalid character range");
          return nullptr;
        }
        curClass.add(std::make_unique<PropertyNode>(op, opType.unwrap()));
        if (this->isEnd() || this->startsWith("]")) { // need more operand
          auto &opNode = *curClass.getChars().back();
          this->reportError(opNode.getToken(), "set operation needs operand: `%s'",
                            this->toStrRef(opNode.getToken()).toString().c_str());
          return nullptr;
        }
      }
    } else {
      auto &curClass = this->curCharClass();
      curClass.add(std::move(node));
      if (this->startsWith("-")) {
        Token token = this->curToken();
        this->iter++;
        if (this->isEnd() || this->startsWith("]")) { // end
          curClass.add(std::make_unique<CharNode>(token, '-'));
          continue;
        }
        if (!isa<CharNode>(*curClass.getChars().back())) {
          if (this->flag.is(Mode::BMP)) {
            curClass.add(std::make_unique<CharNode>(token, '-'));
            continue;
          }
          const auto cur = this->begin() + curClass.getChars().back()->getToken().pos;
          this->reportError(this->getTokenFrom(cur), "invalid character range");
          return nullptr;
        }
        curClass.setType(CharClassNode::Type::RANGE);
        curClass.add(std::make_unique<PropertyNode>(token, PropertyNode::Type::RANGE));
      }
    }
  }
  if (this->isEnd()) {
    this->reportError(this->curCharClass().getToken(), "unclosed character class");
    return nullptr;
  }

END:
  auto node = std::move(this->frames.back().node);
  this->frames.pop_back();
  return node;
}

} // namespace arsh::regex