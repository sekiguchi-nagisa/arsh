/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef MISC_LIB_FORMAT_HPP
#define MISC_LIB_FORMAT_HPP

#include <cstdarg>

#include "detect.hpp"
#include "fatal.h"
#include "string_ref.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

inline void formatTo(std::string &, const char *, ...) __attribute__((format(printf, 2, 3)));

inline void formatTo(std::string &out, const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    fatal_perror("");
  }
  va_end(arg);

  out += str;
  free(str);
}

inline unsigned int countDigits(uint64_t n) {
  unsigned int c;
  if (n == 0) {
    return 1;
  }
  for (c = 0; n > 0; c++) {
    n /= 10;
  }
  return c;
}

inline std::string padLeft(uint64_t num, unsigned int width, char padding) {
  std::string value;
  unsigned int digits = countDigits(num);
  if (digits < width) {
    value.resize(width - digits, padding);
  }
  value += std::to_string(num);
  return value;
}

template <typename Func>
constexpr bool splitter_requirement_v =
    std::is_same_v<bool, std::invoke_result_t<Func, StringRef, bool>>;

template <typename Func, enable_when<splitter_requirement_v<Func>> = nullptr>
inline bool splitByDelim(const StringRef ref, const char delim, Func func) {
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto retPos = ref.find(delim, pos);
    auto sub = ref.slice(pos, retPos);
    pos = retPos;
    bool foundDelim = false;
    if (retPos != StringRef::npos) {
      foundDelim = true;
      pos++;
    }
    if (!func(sub, foundDelim)) {
      return false;
    }
  }
  return true;
}

/**
 * split identifier with word
 *
 * @tparam Func
 * @param ref
 * @param func
 */
template <typename Func,
          enable_when<std::is_same_v<void, std::invoke_result_t<Func, StringRef>>> = nullptr>
inline void splitCamelCaseIdentifier(const StringRef ref, Func func) {
  const auto is_lower = [](char ch) -> bool { return ch >= 'a' && ch <= 'z'; };
  const auto is_lower_or_digit = [](char ch) -> bool {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
  };

  const auto size = ref.size();
  StringRef::size_type start = 0;
  for (StringRef::size_type i = 0; i < size; i++) {
    StringRef::size_type stop = 0;
    char ch = ref[i];
    if (ch == '_') {
      if (i > start) {
        stop = i;
      } else {
        start = i + 1;
        continue;
      }
    } else if (i + 1 == size) {
      stop = size;
    } else if (ch >= 'A' && ch <= 'Z') {
      if ((i + 1 < size && is_lower(ref[i + 1])) || (i > 0 && is_lower_or_digit(ref[i - 1]))) {
        stop = i;
      } else {
        continue;
      }
    } else {
      continue;
    }

    if (start < stop) {
      auto sub = ref.slice(start, stop);
      func(sub);
      start = ch == '_' ? i + 1 : i;
    }
  }
}

constexpr bool isLetter(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); }

constexpr bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

constexpr bool isLetterOrDigit(char ch) { return isLetter(ch) || isDigit(ch); }

constexpr bool isIdentifierStart(char ch) { return isLetter(ch) || ch == '_'; }

/**
 *
 * @param value
 * @return
 * if follow [a-zA-Z_][0-9a-zA-Z_]*, return true
 */
inline bool isValidIdentifier(const StringRef value) {
  if (value.empty()) {
    return false;
  }
  auto begin = value.begin();
  const auto end = value.end();
  if (char ch = *begin; !isIdentifierStart(ch)) {
    return false;
  }
  for (begin++; begin != end; begin++) {
    char ch = *begin;
    if (!isIdentifierStart(ch) && !isDigit(ch)) {
      return false;
    }
  }
  return true;
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_FORMAT_HPP
