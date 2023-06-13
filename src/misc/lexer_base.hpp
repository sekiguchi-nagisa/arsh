/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#ifndef MISC_LIB_LEXER_BASE_HPP
#define MISC_LIB_LEXER_BASE_HPP

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "buffer.hpp"
#include "noncopyable.h"
#include "resource.hpp"
#include "string_ref.hpp"
#include "token.hpp"
#include "unicode.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

class LineNumTable {
private:
  unsigned int offset{1};

  std::vector<unsigned int> table;

public:
  void setOffset(unsigned int v) { this->offset = v; }

  unsigned int getOffset() const { return this->offset; }

  void addNewlinePos(unsigned int pos) {
    if (this->table.empty() || pos > this->table.back()) {
      this->table.push_back(pos);
    }
  }

  /**
   * get line number at source pos
   * @param pos
   * @return
   */
  unsigned int lookup(unsigned int pos) const {
    auto iter = std::lower_bound(this->table.begin(), this->table.end(), pos);
    if (this->table.end() == iter) {
      return this->getMaxLineNum();
    }
    return iter - this->table.begin() + this->offset;
  }

  unsigned int getNewlinePos(unsigned int lineNum) const {
    unsigned int index = lineNum - this->offset;
    return this->table[index];
  }

  unsigned int getMaxLineNum() const { return this->table.size() + this->offset; }
};

namespace detail {

/**
 * base lexer for re2c
 */
template <bool T>
class LexerBase {
protected:
  static_assert(T, "not allowed instantiation");

  std::string sourceName;

  LineNumTable lineNumTable;

  /**
   * must be terminated with null character
   */
  ByteBuffer buf;

  /**
   * current reading pointer of buf.
   */
  const char *cursor{nullptr};

  /**
   * limit of buf.
   */
  const char *limit{nullptr};

  /**
   * for backtracking.
   */
  const char *marker{nullptr};

  /**
   * for trailing context
   */
  const char *ctxMarker{nullptr};

  LexerBase() = default;

  ~LexerBase() = default;

public:
  NON_COPYABLE(LexerBase);

  explicit LexerBase(const char *sourceName) : sourceName(sourceName) {}

  LexerBase(LexerBase &&) noexcept = default;

  /**
   *
   * @param src
   * must be null terminated.
   * @return
   */
  explicit LexerBase(const char *sourceName, const char *src)
      : LexerBase(sourceName, src, strlen(src)) {}

  /**
   *
   * @param data
   * @param size
   * @return
   */
  LexerBase(const char *sourceName, const char *data, unsigned int size) : LexerBase(sourceName) {
    this->appendToBuf(data, size, true);
  }

  LexerBase(const char *sourceName, ByteBuffer &&buffer) : LexerBase(sourceName) {
    this->buf = std::move(buffer);
    if (this->buf.empty() || this->buf.back() != '\n') {
      this->buf += '\n';
    }
    this->buf += '\0';
    this->cursor = this->buf.data();
    this->limit = this->cursor + this->getUsedSize();
  }

  LexerBase &operator=(LexerBase &&lex) noexcept {
    this->swap(lex);
    return *this;
  }

  void swap(LexerBase &lex) noexcept {
    std::swap(this->sourceName, lex.sourceName);
    std::swap(this->lineNumTable, lex.lineNumTable);
    this->buf.swap(lex.buf);
    std::swap(this->cursor, lex.cursor);
    std::swap(this->limit, lex.limit);
    std::swap(this->marker, lex.marker);
    std::swap(this->ctxMarker, lex.ctxMarker);
  }

  const std::string &getSourceName() const { return this->sourceName; }

  void setLineNumOffset(unsigned int lineNum) { this->lineNumTable.setOffset(lineNum); }

  unsigned int getLineNumOffset() const { return this->lineNumTable.getOffset(); }

  unsigned int getLineNumByPos(unsigned int pos) const { return this->lineNumTable.lookup(pos); }

  unsigned int getMaxLineNum() const { return this->lineNumTable.getMaxLineNum(); }

  /**
   * get current reading position.
   */
  unsigned int getPos() const { return this->cursor - this->buf.data(); }

  /**
   * used size of buf. must be this->getUsedSize() <= this->getBufSize().
   */
  unsigned int getUsedSize() const { return this->buf.size() - 1; }

  bool isEnd() const { return this->cursor - 1 == this->limit; }

  bool withinRange(Token token) const { return token.pos + token.size <= this->getUsedSize(); }

  StringRef toStrRef(Token token) const {
    assert(this->withinRange(token));
    return StringRef(this->buf.data() + token.pos, token.size);
  }

  /**
   * get text of token.
   */
  std::string toTokenText(Token token) const {
    assert(this->withinRange(token));
    return std::string(this->buf.data() + token.pos, token.size);
  }

  bool startsWith(Token token, int ch) const {
    assert(this->withinRange(token));
    return this->buf[token.pos] == ch;
  }

  /**
   * shift EOS token to left.
   * @param token
   * @return
   */
  Token shiftEOS(Token token) const {
    if (token.endPos() >= this->getUsedSize()) {
      token.pos = this->getUsedSize() - 1;
      token.size = 0;
    }
    return token;
  }

  /**
   * get line token which token belongs to.
   * @param token
   * @return
   * last character of return token is always newline
   */
  Token getLineToken(Token token) const;

  /**
   * get token text without invalid characters.
   * @param token
   * @return
   */
  std::string formatTokenText(Token token) const;

  /**
   *
   * @param lineToken
   * @param token
   * @param eaw
   * width of east asian Ambiguous character.
   * if 0, auto set width (CJK aware).
   * if 1, half-width.
   * if 2, full-width
   * @return
   */
  std::string formatLineMarker(Token lineToken, Token token, int eaw = 0) const;

  /**
   *
   * @param data
   * @param size
   * size of data
   * @param isEnd
   * if true, append '\n\0'
   */
  void appendToBuf(const char *data, unsigned int size, bool isEnd);

private:
  unsigned int toCodePoint(unsigned int offset, int &code) const {
    return UnicodeUtil::utf8ToCodePoint(this->buf.data() + offset, this->getUsedSize() - offset,
                                        code);
  }

protected:
  void updateNewline(unsigned int pos);
};

// #######################
// ##     LexerBase     ##
// #######################

template <bool T>
Token LexerBase<T>::getLineToken(Token token) const {
  if (token.endPos() >= this->getUsedSize()) {
    token.pos = this->getUsedSize() - 1;
    token.size = 0;
  }

  unsigned int startIndex = 0;
  for (int64_t i = token.pos; i > -1; i--) {
    if (this->buf[i] == '\n' && i != token.pos) {
      startIndex = i + 1;
      break;
    }
  }

  // find stop index of line
  unsigned int stopIndex = token.endPos();
  if (token.size > 0) {
    stopIndex--;
    for (unsigned int usedSize = this->getUsedSize(); stopIndex < usedSize; stopIndex++) {
      if (this->buf[stopIndex] == '\n') {
        stopIndex++;
        break;
      }
    }
  } else {
    stopIndex++;
  }
  return Token{
      .pos = startIndex,
      .size = stopIndex - startIndex,
  };
}

template <bool T>
std::string LexerBase<T>::formatTokenText(Token token) const {
  std::string str;
  unsigned int stop = token.pos + token.size;
  for (unsigned int i = token.pos; i < stop;) {
    int code = 0;
    unsigned int size = this->toCodePoint(i, code);
    if (code < 0) {
      break;
    }
    str.append(this->buf.data() + i, size);
    i += size;
  }
  return str;
}

template <bool T>
std::string LexerBase<T>::formatLineMarker(Token lineToken, Token token, int eaw) const {
  assert(lineToken.pos <= token.pos);

  auto charWidth = AmbiguousCharWidth::HALF;
  if (eaw == 2 || (eaw != 1 && UnicodeUtil::isCJKLocale())) {
    charWidth = AmbiguousCharWidth::FULL;
  }

  std::string lineMarker;
  for (unsigned int i = lineToken.pos; i < token.pos;) {
    int code = 0;
    i += this->toCodePoint(i, code);
    if (code < 0) {
      return lineMarker;
    }
    if (code == '\t' || code == '\n') {
      lineMarker += static_cast<char>(code);
      continue;
    }
    int width = UnicodeUtil::width(code, charWidth);
    if (width == 1) {
      lineMarker += " ";
    } else if (width == 2) {
      lineMarker += "  ";
    }
  }
  const unsigned int stopPos = token.size + token.pos;
  if (token.size == 0) {
    lineMarker += "^";
  }
  for (unsigned int i = token.pos; i < stopPos;) {
    unsigned int prev = i;
    int code = 0;
    i += this->toCodePoint(i, code);
    if (code < 0) {
      return lineMarker;
    }
    if (code == '\t' || code == '\n') {
      if (token.size == 1) {
        lineMarker += "^";
      } else {
        lineMarker += static_cast<char>(code);
        continue;
      }
    }
    int width = UnicodeUtil::width(code, charWidth);
    if (width == 1) {
      lineMarker += (prev == token.pos ? "^" : "~");
    } else if (width == 2) {
      lineMarker += (prev == token.pos ? "^~" : "~~");
    }
  }
  return lineMarker;
}

template <bool T>
void LexerBase<T>::appendToBuf(const char *data, unsigned int size, bool isEnd) {
  // save position
  const unsigned int pos = this->getPos();
  const unsigned int markerPos = this->marker - this->buf.data();
  const unsigned int ctxMarkerPos = this->ctxMarker - this->buf.data();

  if (!this->buf.empty()) {
    this->buf.pop_back(); // pop null character
  }
  this->buf.append(data, size);
  if (isEnd && (this->buf.empty() || this->buf.back() != '\n')) {
    this->buf += '\n';
  }
  this->buf += '\0';

  // restore position
  this->cursor = this->buf.data() + pos;
  this->limit = this->buf.data() + this->getUsedSize();
  this->marker = this->buf.data() + markerPos;
  this->ctxMarker = this->buf.data() + ctxMarkerPos;
}

template <bool T>
void LexerBase<T>::updateNewline(unsigned int pos) {
  const unsigned int stopPos = this->getPos();
  for (unsigned int i = pos; i < stopPos; ++i) {
    if (this->buf[i] == '\n') {
      this->lineNumTable.addNewlinePos(i);
    }
  }
}

} // namespace detail

using LexerBase = detail::LexerBase<true>;

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_LEXER_BASE_HPP
