/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#ifndef ARSH_FORMAT_UTIL_H
#define ARSH_FORMAT_UTIL_H

#include <cstdarg>

#include "misc/enum_util.hpp"
#include "misc/string_ref.hpp"

namespace arsh {

int vformatTo(std::string &out, const char *fmt, va_list arg);

int formatTo(std::string &out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

struct QuoteParam {
  bool asCmd;
  bool carryBackslash;
};

/**
 * quote string that can be reused in command name or command argument.
 * unlike lexer definition, if contains unprintable characters or invalid utf8 sequence,
 * convert to hex notation even if a command name (asCmd is true)
 * @param ref
 * @param out
 * @param param
 * @return
 * if contains unprintable characters or invalid utf8 sequences, return false
 * otherwise, return true
 */
bool quoteAsCmdOrShellArg(StringRef ref, std::string &out, QuoteParam param);

inline bool quoteAsCmdOrShellArg(StringRef ref, std::string &out, bool asCmd) {
  return quoteAsCmdOrShellArg(ref, out, {.asCmd = asCmd, .carryBackslash = false});
}

inline std::string quoteAsShellArg(StringRef ref) {
  std::string ret;
  quoteAsCmdOrShellArg(ref, ret, false);
  return ret;
}

/**
 * unquote command/command-argument literal
 * @param ref
 * @param unescape
 * normally true
 * @return
 */
std::string unquoteCmdArgLiteral(StringRef ref, bool unescape);

/**
 * convert to printable string
 * @param ref
 * @param maxSize
 * @param out
 * @return
 * if reach maxSize, truncate and put '...' and return false
 */
bool appendAsPrintable(StringRef ref, size_t maxSize, std::string &out);

inline bool appendAsPrintable(const StringRef ref, std::string &out) {
  return appendAsPrintable(ref, out.max_size(), out);
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

enum class EscapeSeqOption : unsigned char {
  NONE = 0,
  NEED_OCTAL_PREFIX = 1u << 0u, // octal escape sequence starts with '0'
  CONTROL_CHAR = 1u << 1u,      // interpret \cx escape sequence
};

template <>
struct allow_enum_bitop<EscapeSeqOption> : std::true_type {};

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
 * @param option
 * @return
 */
EscapeSeqResult parseEscapeSeq(const char *begin, const char *end, EscapeSeqOption option);

} // namespace arsh

#endif // ARSH_FORMAT_UTIL_H
