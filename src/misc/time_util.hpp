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

inline timestamp newUnixTimestamp(int64_t epocTimeSec) {
  auto duration = std::chrono::duration<int64_t>(epocTimeSec);
  return timestamp(duration);
}

inline timestamp getCurrentTimestamp() { return timestamp::clock::now(); }

inline IntConversionResult<int64_t> parseDecimalUnixTimestamp(const char *begin, const char *end,
                                                              timestamp &out) {
  auto ret = convertToDecimal<int64_t>(begin, end);
  if (ret) {
    out = newUnixTimestamp(ret.value);
  }
  return ret;
}

inline time_t timestampToTime(const timestamp &t) { return timestamp::clock::to_time_t(t); }

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_TIME_UTIL_HPP
