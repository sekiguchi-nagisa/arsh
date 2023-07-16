/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef MISC_LIB_TIME_UTIL_HPP
#define MISC_LIB_TIME_UTIL_HPP

#include <chrono>

#include "num_util.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

using timestamp = std::chrono::time_point<std::chrono::system_clock>; // for unix timestamp

inline timestamp getCurrentTimestamp() { return timestamp::clock::now(); }

inline timespec timestampToTimespec(timestamp ts) {
  long nanoSec =
      std::chrono::duration_cast<std::chrono::nanoseconds>(ts.time_since_epoch()).count();
  return {
      .tv_sec = timestamp::clock::to_time_t(ts),
      .tv_nsec = nanoSec % 1000000000,
  };
}

enum class ParseTimespecStatus {
  OK,
  INVALID_UNIX_TIME,
  INVALID_NANO_SEC,
};

/**
 * parse `UNIX_TIME[.NANO_SEC]' format
 * @param begin
 * @param end
 * @return
 * if has error, unrecognized part is -1
 */
inline ParseTimespecStatus parseUnixTimeWithNanoSec(const char *begin, const char *end,
                                                    timespec &out) {
  static_assert(sizeof(time_t) == sizeof(int64_t));

  out = {0, 0};
  auto *frac = static_cast<const char *>(memchr(begin, '.', end - begin));
  if (auto ret = convertToNum<int64_t>(begin, frac ? frac : end, 10)) {
    out.tv_sec = ret.value;
  } else {
    return ParseTimespecStatus::INVALID_UNIX_TIME;
  }

  if (frac) {
    unsigned int v = 0;
    unsigned int count = 0;
    for (frac++; frac != end && count < 9; frac++) {
      char c = *frac;
      if (isDecimal(c)) {
        v *= 10;
        v += (c - '0');
      } else {
        return ParseTimespecStatus::INVALID_NANO_SEC;
      }
      count++;
    }
    if (count == 0) { // consume no fractional part
      return ParseTimespecStatus::INVALID_NANO_SEC;
    }
    for (; count < 9; count++) {
      v *= 10;
    }
    if (frac == end) {
      out.tv_nsec = static_cast<int>(v);
    } else {
      return ParseTimespecStatus::INVALID_NANO_SEC;
    }
  }
  return ParseTimespecStatus::OK;
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_TIME_UTIL_HPP
