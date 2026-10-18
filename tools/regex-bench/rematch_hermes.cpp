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
 * Hermes regex engine adapter for the standalone `rematch` tool (see rematch_abi.h).
 *
 * The engine is split into a compiler (`hermes::regex::Regex`, templated on the character
 * traits) and a bytecode executor (`hermes::regex::searchWithBytecode`). As in
 * tools/regex-bench/hermes.cpp, the pattern is compiled with `UTF16RegexTraits` and executed
 * with `UTF16RegexTraits` because the tool matches in Unicode mode.
 *
 * Hermes has no `v` (unicode sets) mode, so the `v` mode is mapped to `u`; unsupported
 * constructs (e.g. `\p{Basic_Emoji}`) simply fail to compile.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rematch_abi.h"
#include "rematch_utf16.h"

#include <hermes/Regex/Executor.h>
#include <hermes/Regex/Regex.h>
#include <hermes/Regex/RegexTraits.h>

#include <llvh/ADT/ArrayRef.h>

using namespace hermes::regex;

namespace {

std::u16string toUTF16(const char *input, int inputLen) {
  uint32_t len = 0;
  uint16_t *buf = rematch_utf8_to_utf16(input, static_cast<uint32_t>(inputLen), &len);
  if (buf == nullptr) {
    return {};
  }
  std::u16string out(reinterpret_cast<const char16_t *>(buf), len);
  free(buf);
  return out;
}

/* translate the arsh-like mode/modifiers into Hermes' flag string ('u' is always set) */
std::u16string toFlags(int mode, int icase, int multiline, int dotall) {
  (void)mode; /* Hermes only has u/v-less unicode; v is mapped to u */
  std::u16string flags = u"u";
  if (icase) {
    flags.push_back(u'i');
  }
  if (multiline) {
    flags.push_back(u'm');
  }
  if (dotall) {
    flags.push_back(u's');
  }
  return flags;
}

void setError(char *err, int errSize, const char *message) {
  if (err != nullptr && errSize > 0) {
    std::snprintf(err, static_cast<size_t>(errSize), "%s", message);
  }
}

} // namespace

extern "C" int rematch_hermes_match(const char *pattern, int patternLen,
                                    const char *input, int inputLen,
                                    int mode, int icase, int multiline, int dotall,
                                    rematch_capture *captures, int maxCaptures,
                                    char *err, int errSize) {
  if (pattern == nullptr || input == nullptr || patternLen < 0 || inputLen < 0 ||
      maxCaptures <= 0 || captures == nullptr) {
    setError(err, errSize, "invalid argument");
    return -1;
  }

  const auto pattern16 = toUTF16(pattern, patternLen);
  const auto flags = toFlags(mode, icase, multiline, dotall);

  Regex<UTF16RegexTraits> regex(llvh::ArrayRef<char16_t>(pattern16.data(), pattern16.size()),
                                llvh::ArrayRef<char16_t>(flags.data(), flags.size()));
  if (!regex.valid()) {
    setError(err, errSize, constants::messageForError(regex.getError()));
    return -1;
  }
  const auto bytecode = regex.compile();

  uint32_t textLen = 0;
  uint16_t *textBuf = rematch_utf8_to_utf16(input, static_cast<uint32_t>(inputLen), &textLen);
  if (textBuf == nullptr) {
    setError(err, errSize, "out of memory");
    return -2;
  }

  std::vector<CapturedRange> ranges;
  const auto result = searchWithBytecode(bytecode, reinterpret_cast<const char16_t *>(textBuf), 0,
                                         textLen, &ranges, constants::matchDefault);
  int ret = 0;
  if (result == MatchRuntimeResult::StackOverflow) {
    setError(err, errSize, "stack overflow");
    ret = -2;
  } else if (result == MatchRuntimeResult::Match) {
    const int count = static_cast<int>(ranges.size());
    const int n = count < maxCaptures ? count : maxCaptures;
    for (int i = 0; i < n; i++) {
      if (!ranges[i].matched()) {
        captures[i].offset = REMATCH_CAPTURE_UNSET;
        captures[i].size = REMATCH_CAPTURE_UNSET;
      } else {
        const uint32_t start = rematch_utf16_to_byte_offset(textBuf, ranges[i].start);
        const uint32_t end = rematch_utf16_to_byte_offset(textBuf, ranges[i].end);
        captures[i].offset = start;
        captures[i].size = end - start;
      }
    }
    ret = n;
  }
  free(textBuf);
  return ret;
}
