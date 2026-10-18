/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

/**
 * SRELL regex engine adapter for the standalone `rematch` tool (see rematch_abi.h).
 *
 * SRELL is a `std::regex`-like header-only library. The UTF-8 traits (`u8cregex` /
 * `u8csmatch`) are used so `\p{...}` and non-BMP subjects behave like the other
 * ECMAScript-compatible engines (see tools/regex-bench/srell.cpp).
 *
 * SRELL supports the `v` (unicode sets) mode via its `unicodesets` extension, so both `u` and
 * `v` are mapped to the closest matching mode.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "rematch_abi.h"

#include <srell.hpp>

extern "C" int rematch_srell_match(const char *pattern, int patternLen,
                                   const char *input, int inputLen,
                                   int mode, int icase, int multiline, int dotall,
                                   rematch_capture *captures, int maxCaptures,
                                   char *err, int errSize) {
  if (pattern == nullptr || input == nullptr || patternLen < 0 || inputLen < 0 ||
      maxCaptures <= 0 || captures == nullptr) {
    if (err != nullptr && errSize > 0) {
      std::snprintf(err, static_cast<size_t>(errSize), "invalid argument");
    }
    return -1;
  }

  auto flags = srell::regex_constants::ECMAScript;
  if (mode == REMATCH_MODE_UNICODE_SET) {
    flags = flags | srell::regex_constants::unicodesets;
  }
  if (icase) {
    flags = flags | srell::regex_constants::icase;
  }
  if (multiline) {
    flags = flags | srell::regex_constants::multiline;
  }
  if (dotall) {
    flags = flags | srell::regex_constants::dotall;
  }

  try {
    srell::u8cregex regex(pattern, static_cast<std::size_t>(patternLen),
                          static_cast<srell::regex_constants::syntax_option_type>(flags));
    const std::string text(input, static_cast<std::size_t>(inputLen));
    srell::u8csmatch match;
    if (!srell::regex_search(text, match, regex)) {
      return 0; /* no match */
    }
    const auto size = static_cast<int>(match.size());
    const int n = size < maxCaptures ? size : maxCaptures;
    for (int i = 0; i < n; i++) {
      if (!match[static_cast<std::size_t>(i)].matched) {
        captures[i].offset = REMATCH_CAPTURE_UNSET;
        captures[i].size = REMATCH_CAPTURE_UNSET;
      } else {
        captures[i].offset = static_cast<uint32_t>(match.position(static_cast<std::size_t>(i)));
        captures[i].size = static_cast<uint32_t>(match.length(static_cast<std::size_t>(i)));
      }
    }
    return n;
  } catch (const std::exception &ex) {
    if (err != nullptr && errSize > 0) {
      std::snprintf(err, static_cast<size_t>(errSize), "%s", ex.what());
    }
    return -1;
  }
}
