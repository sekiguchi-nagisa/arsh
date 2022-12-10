/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef MISC_LIB_PARSER_BASE_HPP
#define MISC_LIB_PARSER_BASE_HPP

#include <vector>

#include "lexer_base.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

constexpr const char *TOKEN_MISMATCHED = "TokenMismatched";
constexpr const char *NO_VIABLE_ALTER = "NoViableAlter";
constexpr const char *INVALID_TOKEN = "InvalidToken";
constexpr const char *TOKEN_FORMAT = "TokenFormat";
constexpr const char *DEEP_NESTING = "DeepNesting";

template <typename T>
class ParseErrorBase {
private:
  T kind;
  Token errorToken;
  const char *errorKind;
  std::vector<T> expectedTokens;
  std::string message;

public:
  ParseErrorBase(T kind, Token errorToken, const char *errorKind, std::string &&message)
      : kind(kind), errorToken(errorToken), errorKind(errorKind), expectedTokens(),
        message(std::move(message)) {}

  ParseErrorBase(T kind, Token errorToken, const char *errorKind, std::vector<T> &&expectedTokens,
                 std::string &&message)
      : kind(kind), errorToken(errorToken), errorKind(errorKind),
        expectedTokens(std::move(expectedTokens)), message(std::move(message)) {}

  ~ParseErrorBase() = default;

  const Token &getErrorToken() const { return this->errorToken; }

  T getTokenKind() const { return this->kind; }

  const char *getErrorKind() const { return this->errorKind; }

  const std::vector<T> &getExpectedTokens() const { return this->expectedTokens; }

  const std::string &getMessage() const { return this->message; }
};

template <typename T>
struct EmptyTokenTracker {
  void operator()(T, Token) {}
};

template <typename T, typename LexerImpl, typename Tracker = EmptyTokenTracker<T>>
class ParserBase {
protected:
  /**
   * need 'T nextToken(Token)'
   */
  LexerImpl *lexer{nullptr};

  /**
   * kind of latest consumed token
   */
  T consumedKind{};

  T curKind{};

  Token curToken{};

  /**
   * need 'void operator()(T, Token)'
   */
  Tracker *tracker{nullptr};

  unsigned int callCount{0};

private:
  std::unique_ptr<ParseErrorBase<T>> error;

public:
  ParserBase() = default;

  ParserBase(ParserBase &&) noexcept = default;

  ParserBase &operator=(ParserBase &&o) noexcept {
    auto tmp(std::move(o));
    this->swap(tmp);
    return *this;
  }

  void swap(ParserBase &o) noexcept {
    std::swap(this->lexer, o.lexer);
    std::swap(this->curKind, o.curKind);
    std::swap(this->curToken, o.curToken);
    std::swap(this->consumedKind, o.consumedKind);
    std::swap(this->tracker, o.tracker);
    std::swap(this->callCount, o.callCount);
    std::swap(this->error, o.error);
  }

  void setTracker(Tracker *other) { this->tracker = other; }

  const Tracker *getTracker() const { return this->tracker; }

  const LexerImpl *getLexer() const { return this->lexer; }

  bool hasError() const { return static_cast<bool>(this->error); }

  const ParseErrorBase<T> &getError() const { return *this->error; }

  std::unique_ptr<ParseErrorBase<T>> takeError() && { return std::move(this->error); }

  void clear() { this->error.reset(); }

protected:
  ~ParserBase() = default;

  /**
   * low level api. not directly use it.
   */
  void fetchNext() { this->curKind = this->lexer->nextToken(this->curToken); }

  void trace() {
    this->consumedKind = this->curKind;
    if (this->tracker != nullptr) {
      (*this->tracker)(this->curKind, this->curToken);
    }
  }

  Token expect(T kind, bool fetchNext = true);

  void consume();

  T scan() {
    T kind = this->curKind;
    this->consume();
    return kind;
  }

  template <std::size_t N>
  void reportNoViableAlterError(const T (&alters)[N]) {
    this->reportNoViableAlterError(N, alters);
  }

  void reportTokenMismatchedError(T expected);

  void reportNoViableAlterError(unsigned int size, const T *alters);

  void reportInvalidTokenError(unsigned int size, const T *alters);

  void reportTokenFormatError(T kind, Token token, std::string &&msg) {
    this->createError(kind, token, TOKEN_FORMAT, std::move(msg));
  }

  void reportDeepNestingError();

  template <typename... Arg>
  void createError(Arg &&...arg) {
    this->error.reset(new ParseErrorBase<T>(std::forward<Arg>(arg)...));
  }
};

// ########################
// ##     ParserBase     ##
// ########################

template <typename T, typename LexerImpl, typename Tracker>
Token ParserBase<T, LexerImpl, Tracker>::expect(T kind, bool fetchNext) {
  auto token = this->curToken;
  if (this->curKind != kind) {
    this->reportTokenMismatchedError(kind);
    return token;
  }
  this->trace();
  if (fetchNext) {
    this->fetchNext();
  }
  return token;
}

template <typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::consume() {
  this->trace();
  this->fetchNext();
}

template <typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::reportTokenMismatchedError(T expected) {
  if (isInvalidToken(this->curKind)) {
    T alter[1] = {expected};
    this->reportInvalidTokenError(1, alter);
  } else {
    std::string message;
    if (!isEOSToken(this->curKind)) {
      message += "mismatched token `";
      message += toString(this->curKind);
      message += "', ";
    }
    message += "expected `";
    message += toString(expected);
    message += "'";

    std::vector<T> expectedTokens(1);
    expectedTokens[0] = expected;
    this->createError(this->curKind, this->curToken, TOKEN_MISMATCHED, std::move(expectedTokens),
                      std::move(message));
  }
}

template <typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::reportNoViableAlterError(unsigned int size,
                                                                 const T *alters) {
  if (isInvalidToken(this->curKind)) {
    this->reportInvalidTokenError(size, alters);
  } else {
    std::string message;
    if (!isEOSToken(this->curKind)) {
      message += "mismatched token `";
      message += toString(this->curKind);
      message += "', ";
    }
    if (size > 0 && alters != nullptr) {
      message += "expected ";
      for (unsigned int i = 0; i < size; i++) {
        if (i > 0) {
          message += ", ";
        }
        message += "`";
        message += toString(alters[i]);
        message += "'";
      }
    }

    std::vector<T> expectedTokens(alters, alters + size);
    this->createError(this->curKind, this->curToken, NO_VIABLE_ALTER, std::move(expectedTokens),
                      std::move(message));
  }
}

template <typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::reportInvalidTokenError(unsigned int size,
                                                                const T *alters) {
  std::string message = "invalid token, expected ";
  if (size > 0 && alters != nullptr) {
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        message += ", ";
      }
      message += "`";
      message += toString(alters[i]);
      message += "'";
    }
  }

  std::vector<T> expectedTokens(alters, alters + size);
  this->createError(this->curKind, this->curToken, INVALID_TOKEN, std::move(expectedTokens),
                    std::move(message));
}

template <typename T, typename LexerImpl, typename Tracker>
void ParserBase<T, LexerImpl, Tracker>::reportDeepNestingError() {
  std::string message = "parser recursion depth exceeded";
  this->createError(this->curKind, this->curToken, DEEP_NESTING, std::move(message));
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_PARSER_BASE_HPP
