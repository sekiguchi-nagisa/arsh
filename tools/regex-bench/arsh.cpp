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
 * Adapter which registers the arsh regex engine into the
 * `rust-leipzig/regex-performance` benchmark tool.
 *
 * The benchmark harness (see main.h) expects each engine to provide
 *   int <name>_find_all(char *pattern, char *subject, int subject_len, int repeat,
 *                       struct result *res);
 * so this file implements `arsh_find_all`, which counts the matches of every scan through
 * `regex::matchAll` (a fresh match context is created inside it for every scan).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "main.h"
#include "memory_tracker.h"

#ifndef BEGIN_MISC_LIB_NAMESPACE_DECL
#define BEGIN_MISC_LIB_NAMESPACE_DECL namespace arsh {
#endif
#ifndef END_MISC_LIB_NAMESPACE_DECL
#define END_MISC_LIB_NAMESPACE_DECL }
#endif

#include <misc/string_ref.hpp>
#include <regex/emit.h>
#include <regex/parser.h>
#include <regex/regex.h>

using namespace arsh;

/**
 * Some benchmark patterns start with an inline `(?i)` modifier while others pass no flag at all.
 * arsh only accepts flags via `Flag::parse`, so translate the leading `(?ims)` prefix (if any) to
 * the equivalent arsh flag string. The returned pattern must not contain the consumed prefix.
 */
static std::string splitInlineFlags(const std::string &pattern, std::string &flagStr) {
  flagStr = "u";
  if (pattern.size() >= 4 && pattern[0] == '(' && pattern[1] == '?' && pattern[3] == ')' &&
      strchr("ims", pattern[2]) != nullptr) {
    flagStr += pattern[2];
    return pattern.substr(4);
  }
  return pattern;
}

static Optional<regex::Regex> compilePattern(const std::string &pattern) {
  std::string flagStr;
  const auto pat = splitInlineFlags(pattern, flagStr);

  std::string err;
  auto flag = regex::Flag::parse(flagStr, &err);
  if (!flag.hasValue()) {
    return {};
  }
  regex::Parser parser;
  auto tree = parser(pat, flag.unwrap());
  if (parser.hasError()) {
    return {};
  }
  regex::CodeGen codeGen;
  return codeGen(std::move(tree));
}

/**
 * Scan the whole input once, counting all non-overlapping matches (emulating the global (g)
 * flag) via `regex::matchAll` (the newer api, replacing the manual `regex::match` loop).
 * The consumer only counts, so the capture data is discarded.
 *
 * @param re the compiled regex.
 * @param text the subject text.
 * @return the match status. on OK, the number of matches is stored to `*found`.
 */
static regex::MatchStatus countMatchesIn(const regex::Regex &re, const StringRef text,
                                         long long *found) {
  long long count = 0;
  auto consumer = [&count](ArrayRef<regex::Capture>) {
    count++;
    return true;
  };
  const auto status = regex::matchAll(re, text, 0, consumer, nullptr);
  *found = count;
  return status;
}

extern "C" int arsh_find_all(char *pattern, char *subject, int subject_len, int repeat,
                             struct result *res) {
  if (pattern == nullptr || subject == nullptr || subject_len < 0 || repeat <= 0 || res == nullptr) {
    return -1;
  }
  memory_tracker_init();
  const size_t memBase = memory_tracker_live();
  auto compiled = compilePattern(pattern);
  if (!compiled.hasValue()) {
    printf("arsh compilation failed: %s\n", pattern);
    return -1;
  }
  const auto &re = compiled.unwrap();
  /* the compiled regex instance itself (the scan context is created inside matchAll below) */
  res->mem_instance = memory_tracker_live() - memBase;
  const StringRef text(subject, static_cast<size_t>(subject_len));

  double *times = static_cast<double *>(calloc(static_cast<size_t>(repeat), sizeof(double)));
  if (times == nullptr) {
    return -1;
  }
  const int timesLen = repeat;

  memory_tracker_reset_peak();
  const size_t memScanBase = memory_tracker_live();

  long long found = 0;
  do {
    TIME_TYPE start, end;
    GET_TIME(start);
    const auto status = countMatchesIn(re, text, &found);
    GET_TIME(end);
    times[repeat - 1] = TIME_DIFF_IN_MS(start, end);
    if (status != regex::MatchStatus::OK) {
      free(times);
      return -1;
    }
  } while (--repeat > 0);

  res->mem_runtime = memory_tracker_peak() - memScanBase;
  res->matches = static_cast<int>(found);
  get_mean_and_derivation(times, static_cast<uint32_t>(timesLen), res);
  free(times);
  return 0;
}

