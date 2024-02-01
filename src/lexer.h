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

#ifndef ARSH_LEXER_H
#define ARSH_LEXER_H

#include <cstdint>
#include <cstdlib>

#include "misc/lexer_base.hpp"
#include "token_kind.h"

namespace arsh {

#include <yycond.h>

class LexerMode {
private:
  LexerCond cond_;
  bool skipNL_;
  unsigned short hereDepth_;

public:
  LexerMode(LexerCond cond, bool skipNL, unsigned short hereDepth = 0)
      : cond_(cond), skipNL_(skipNL), hereDepth_(hereDepth) {}

  LexerMode(LexerCond cond) : LexerMode(cond, false) {} // NOLINT

  LexerMode() : LexerMode(yycSTMT) {}

  LexerCond cond() const { return this->cond_; }

  bool skipNL() const { return this->skipNL_; }

  unsigned short hereDepth() const { return this->hereDepth_; }

  std::string toString() const;

  bool isHereDoc() const {
    return this->cond() == LexerCond::yycEXP_HERE || this->cond() == LexerCond::yycHERE;
  }
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

class HereDocState {
public:
  enum class Attr : unsigned char {
    EXPAND = 1u << 0u,
    IGNORE_TAB = 1u << 1u,
  };

  struct Entry {
    Token token;
    Attr attr;
    unsigned int pos; // here doc position
  };

private:
  std::vector<Entry> hereDocStarts;

public:
  /**
   *
   * @param token
   * must be unquoted (quote removed)
   * @param attr
   */
  void add(Token token, Attr attr, unsigned int pos) {
    this->hereDocStarts.push_back({token, attr, pos});
  }

  const auto &curHereDocState() const { return this->hereDocStarts[0]; }

  void shift() { this->hereDocStarts.erase(this->hereDocStarts.begin()); }

  explicit operator bool() const { return !this->hereDocStarts.empty(); }
};

template <>
struct allow_enum_bitop<HereDocState::Attr> : std::true_type {};

class Lexer : public LexerBase, public RefCount<Lexer> {
private:
  static_assert(sizeof(LexerMode) == sizeof(unsigned int));
  static_assert(std::is_trivially_copyable_v<LexerMode>);

  CStrPtr scriptDir;

  std::vector<LexerMode> modeStack;

  std::vector<HereDocState> hereDocStates;

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
    this->hereDocStates.emplace_back();
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
    this->setLexerMode(LexerMode(cond, c.skipNL(), c.hereDepth()));
  }

  void pushLexerMode(LexerMode mode) {
    this->modeStack.push_back(this->curMode);
    this->setLexerMode(mode);
  }

  void popLexerMode() {
    if (this->curMode.hereDepth() == this->hereDocStates.size()) {
      this->hereDocStates.pop_back();
    }
    if (!this->modeStack.empty()) {
      this->curMode = this->modeStack.back();
      this->modeStack.pop_back();
    }
  }

  LexerMode getLexerMode() const { return this->curMode; }

  void setComplete(bool allow) { this->complete = allow; }

  bool isComplete() const { return this->complete; }

  bool inCompletionPoint() const { return this->complete && this->cursor + 1 == this->limit; }

  TokenKind getCompTokenKind() const { return this->compTokenKind; }

  void setTriviaStore(ObserverPtr<TriviaStore> store) { this->triviaStore = store; }

  void setHereDocStart(TokenKind hereOp, Token startToken, unsigned int redirPos);

  const auto &getHereDocState() const { return this->hereDocStates.back().curHereDocState(); }

  unsigned int hereDocStateDepth() const { return this->hereDocStates.size(); }

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
   * @param token
   * @param unescape
   * normally true
   */
  std::string toCmdArg(Token token, bool unescape = true) const;

  std::string toHereDocBody(Token token, HereDocState::Attr attr) const;

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

private:
  /**
   * low level api. normally should not use it
   * @param mode
   */
  void setLexerMode(LexerMode mode) { this->curMode = mode; }

  void setCompTokenKind(TokenKind kind) { this->compTokenKind = kind; }

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
   * for unclosed token
   * @param startPos
   */
  void shiftNewline(unsigned int startPos) {
    Token token{
        .pos = startPos,
        .size = this->getPos() - startPos,
    };
    auto ref = this->toStrRef(token);
    if (!ref.empty() && ref.back() == '\n') {
      this->cursor--;
    }
  }

  bool canEmitNewline() {
    if (this->complete && this->cursor == this->limit) {
      return false;
    }
    return true;
  }

  void tryEnterHereDocMode() {
    if (this->hereDocStates.back()) {
      auto attr = this->hereDocStates.back().curHereDocState().attr;
      this->pushLexerMode(hasFlag(attr, HereDocState::Attr::EXPAND) ? yycEXP_HERE : yycHERE);
    }
  }

  /**
   *
   * @param startPos
   * @return
   * if exit here doc (token is heredoc end), return true
   */
  bool tryExitHereDocMode(unsigned int startPos);

  void pushLexerModeWithHere(LexerCond cond) {
    this->hereDocStates.emplace_back();
    LexerMode newMode(cond, true, static_cast<unsigned short>(this->hereDocStates.size()));
    this->pushLexerMode(newMode);
  }
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

/**
 * quote string that can be reused in command name or command argument.
 * unlike lexer definition, if contains unprintable characters or invalid utf8 sequence,
 * convert to hex notation even if command name (asCmd is true)
 * @param ref
 * @param out
 * @param asCmd
 * quote as command name
 * @return
 * if contains unprintable characters or invalid utf8 sequences, return false
 * otherwise, return true
 */
bool quoteAsCmdOrShellArg(StringRef ref, std::string &out, bool asCmd);

inline std::string quoteAsShellArg(StringRef ref) {
  std::string ret;
  quoteAsCmdOrShellArg(ref, ret, false);
  return ret;
}

/**
 * convert to printable string
 * @param ref
 * @return
 */
std::string toPrintable(StringRef ref);

inline bool appendAsUnescaped(const StringRef value, const size_t maxSize, std::string &out) {
  const auto size = value.size();
  for (StringRef::size_type i = 0; i < size; i++) {
    char ch = value[i];
    if (ch == '\\' && i + 1 < size) {
      ch = value[++i];
    }
    if (out.size() == maxSize) {
      return false;
    }
    out += ch;
  }
  return true;
}

} // namespace arsh

#endif // ARSH_LEXER_H
