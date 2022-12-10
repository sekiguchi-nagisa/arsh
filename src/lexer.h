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

#ifndef YDSH_LEXER_H
#define YDSH_LEXER_H

#include <cstdint>
#include <cstdlib>

#include "misc/lexer_base.hpp"
#include "token_kind.h"

namespace ydsh {

#include <yycond.h>

class LexerMode {
private:
  LexerCond cond_;
  bool skipNL_;

public:
  LexerMode(LexerCond cond, bool skipNL) : cond_(cond), skipNL_(skipNL) {}

  LexerMode(LexerCond cond) : LexerMode(cond, false) {} // NOLINT

  LexerMode() : LexerMode(yycSTMT) {}

  LexerCond cond() const { return this->cond_; }

  bool skipNL() const { return this->skipNL_; }

  std::string toString() const;
};

struct SrcPos {
  unsigned int lineNum;
  unsigned int chars;
};

/**
 * for consume comment or \\n
 */
struct TriviaStore {
  virtual ~TriviaStore() = default;

  virtual void operator()(Token token) = 0;
};

class Lexer : public ydsh::LexerBase, public RefCount<Lexer> {
private:
  static_assert(sizeof(LexerMode) == sizeof(uint16_t));
  static_assert(std::is_trivially_copyable<LexerMode>::value);

  CStrPtr scriptDir;

  std::vector<LexerMode> modeStack;

  LexerMode curMode{yycSTMT};

  LexerMode prevMode{yycSTMT};

  TokenKind compTokenKind{TokenKind::INVALID};

  /**
   * if true, enable code completion (may emit complete token)
   */
  bool complete{false};

  bool prevNewLine{false};

  /**
   * only available in command mode.
   */
  bool prevSpace{false};

  ObserverPtr<TriviaStore> triviaStore;

public:
  NON_COPYABLE(Lexer);

  Lexer(Lexer &&) = default;

  Lexer() = default;

  Lexer(const char *sourceName, ByteBuffer &&buf, CStrPtr &&scriptDir)
      : LexerBase(sourceName, std::move(buf)), scriptDir(std::move(scriptDir)) {
    if (!this->scriptDir || *this->scriptDir == '\0') {
      this->scriptDir.reset(strdup("."));
    }
  }

  ~Lexer() = default;

  static IntrusivePtr<Lexer> fromFullPath(const char *fullPath, ByteBuffer &&buf);

  SrcPos getSrcPos(Token token) const;

  /**
   *
   * @return
   * not null
   */
  const char *getScriptDir() const { return this->scriptDir.get(); }

  void setPos(unsigned int pos) {
    assert(this->buf.data() + pos <= this->limit);
    this->cursor = this->buf.data() + pos;
  }

  void setPrevNewline(bool newline) { this->prevNewLine = newline; }

  void setPrevSpace(bool space) { this->prevSpace = space; }

  bool isPrevNewLine() const { return this->prevNewLine; }

  bool isPrevSpace() const { return this->prevSpace; }

  LexerMode getPrevMode() const { return this->prevMode; }

  void setLexerCond(LexerCond cond) {
    auto c = this->curMode;
    this->setLexerMode(LexerMode(cond, c.skipNL()));
  }

  void setLexerMode(LexerMode mode) { this->curMode = mode; }

  void pushLexerMode(LexerMode mode) {
    this->modeStack.push_back(this->curMode);
    this->setLexerMode(mode);
  }

  void popLexerMode() {
    if (!this->modeStack.empty()) {
      this->curMode = this->modeStack.back();
      this->modeStack.pop_back();
    }
  }

  LexerMode getLexerMode() const { return this->curMode; }

  void setComplete(bool allow) { this->complete = allow; }

  bool isComplete() const { return this->complete; }

  bool inCompletionPoint() const { return this->complete && this->cursor + 1 == this->limit; }

  void setCompTokenKind(TokenKind kind) { this->compTokenKind = kind; }

  TokenKind getCompTokenKind() const { return this->compTokenKind; }

  void setTriviaStore(ObserverPtr<TriviaStore> store) { this->triviaStore = store; }

  void addTrivia(unsigned int startPos) {
    if (this->triviaStore) {
      Token token{
          .pos = startPos,
          .size = this->getPos() - startPos,
      };
      this->triviaStore(token);
    }
  }

  /**
   * lexer entry point.
   * write next token to token.
   * return the kind of next token.
   */
  TokenKind nextToken(Token &token);

  // token to value converting api.

  /**
   * convert single quote string literal token to string.
   * if token is illegal format(ex. illegal escape sequence), return false.
   */
  bool singleToString(Token token, std::string &out) const;

  /**
   * convert escaped single quote string literal token to string.
   * if token is illegal format(ex. illegal escape sequence), return false.
   */
  bool escapedSingleToString(Token token, std::string &out) const;

  /**
   * convert double quote string element token to string.
   */
  std::string doubleElementToString(Token token) const;

  /**
   * convert token to command argument
   */
  std::string toCmdArg(Token token) const;

  /**
   * convert token to name(remove '$' char)
   * ex. $hoge, ${hoge}, hoge
   */
  std::string toName(Token token) const;

  /**
   * for int literal parsing.
   * also parse bit representation (octal/hex notation) of number.
   * unlike convertToNum api, accept out-of-range number such as 0xFFFFFFFFFFFFFFFF
   * @param token
   * @return
   */
  std::pair<int64_t, bool> toInt64(Token token) const;

  std::pair<double, bool> toDouble(Token token) const;

  bool toEnvName(Token token, std::string &out) const;
};

using LexerPtr = IntrusivePtr<Lexer>;

struct EscapeSeqResult {
  enum Kind : unsigned char {
    OK_CODE,    // success as code point
    OK_BYTE,    // success as byte
    END,        // reach end
    NEED_CHARS, // need one or more characters
    UNKNOWN,    // unknown escape sequence
    RANGE,      // out-of-range unicode (U+000000~U+10FFFF)
  } kind;
  unsigned short consumedSize;
  int codePoint;

  explicit operator bool() const { return this->kind == OK_CODE || this->kind == OK_BYTE; }
};

/**
 * common escape sequence handling
 * @param begin
 * must be start with '\'
 * @param end
 * @param needOctalPrefix
 * if true, octal escape sequence start with '0'
 * @return
 */
EscapeSeqResult parseEscapeSeq(const char *begin, const char *end, bool needOctalPrefix);

} // namespace ydsh

#endif // YDSH_LEXER_H
