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

#include "misc/string_ref.hpp"

namespace arsh {

int vformatTo(std::string &out, const char *fmt, va_list arg);

int formatTo(std::string &out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

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
 * @param maxSize
 * @param out
 * if reach maxSize, truncate and put '...'
 */
void appendAsPrintable(StringRef ref, size_t maxSize, std::string &out);

inline void appendAsPrintable(const StringRef ref, std::string &out) {
  appendAsPrintable(ref, out.max_size(), out);
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

#endif // ARSH_FORMAT_UTIL_H
