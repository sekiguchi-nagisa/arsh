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
static constexpr bool splitter_requirement_v =
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

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_FORMAT_HPP
