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
 * Adapter which registers the SRELL regex engine into the
 * `rust-leipzig/regex-performance` benchmark tool.
 *
 * SRELL is a `std::regex`-like header-only library. See `srell_find_all` for the
 * benchmark entry point required by the harness (see main.h).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>

#include "main.h"

#include <srell.hpp>

static int searchAll(srell::regex &rx, const std::string &text) {
  auto begin = srell::sregex_iterator(text.begin(), text.end(), rx);
  auto end = srell::sregex_iterator();
  return static_cast<int>(std::distance(begin, end));
}

extern "C" int srell_find_all(char *pattern, char *subject, int subject_len, int repeat,
                              struct result *res) {
  TIME_TYPE start = 0, end = 0;
  int found = 0;

  try {
    srell::regex rx(pattern, srell::regex::optimize);
    const std::string text(subject, static_cast<size_t>(subject_len));

    double *times = static_cast<double *>(calloc(static_cast<size_t>(repeat), sizeof(double)));
    const int timesLen = repeat;

    do {
      GET_TIME(start);
      found = searchAll(rx, text);
      GET_TIME(end);
      times[repeat - 1] = TIME_DIFF_IN_MS(start, end);
    } while (--repeat > 0);

    res->matches = found;
    get_mean_and_derivation(times, static_cast<uint32_t>(timesLen), res);
    free(times);
  } catch (std::exception &ex) {
    fprintf(stderr, "Exception thrown compiling regex [%s]: %s\n", pattern, ex.what());
    res->time = 999999;
    res->time_sd = 0;
    res->matches = 0;
  }
  return 0;
}
