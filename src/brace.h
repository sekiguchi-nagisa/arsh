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

#ifndef YDSH_BRACE_H
#define YDSH_BRACE_H

#include "misc/string_ref.hpp"

namespace ydsh {

// for sequence style brace expansion

struct BraceRange {
  int64_t begin; // inclusive
  int64_t end;   // inclusive
  int64_t step;
  unsigned int digits; // if 0, no-padding
  enum class Kind : unsigned int {
    CHAR,
    INT,

    // for error
    OUT_OF_RANGE,
    OUT_OF_RANGE_STEP,
  } kind;
};

/**
 * for sequence style brace expansion
 * @param ref
 * begin..end..step
 * @param isChar
 * if true, char range, otherwise, int range
 * @return
 */
BraceRange toBraceRange(StringRef ref, bool isChar);

std::string formatSeqValue(int64_t v, unsigned int digits, bool isChar);

bool tryUpdateSeqValue(int64_t &cur, const BraceRange &range);

} // namespace ydsh

#endif // YDSH_BRACE_H
