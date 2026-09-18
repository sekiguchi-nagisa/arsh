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

#include <algorithm>
#include <limits>

#include "misc/unicode.hpp"
#include "unicode/case_fold.h"

namespace arsh {

#include "simple_case_fold_table.h"

static bool isSimpleCaseFoldTarget(int codePoint) {
  return codePoint >= 0 && codePoint <= max_fold_code_point;
}

static uint32_t lookupSimpleCaseFoldEntry(unsigned int cp) {
  const auto index = simple_case_fold_block_indexes[cp >> simple_case_fold_block_shift];
  return simple_case_fold_blocks[(index * simple_case_fold_block_size) +
                                 (cp % simple_case_fold_block_size)];
}

int doSimpleCaseFolding(int codePoint) {
  if (isSimpleCaseFoldTarget(codePoint)) {
    auto entry = lookupSimpleCaseFoldEntry(static_cast<unsigned int>(codePoint));
    int delta = static_cast<int>(entry & ~1u) / 2;
    return codePoint + delta;
  }
  return codePoint;
}

struct FullFoldEntry {
  int code;
  CaseFoldingResult::FullFoldingEntry folds;

  static constexpr void append(CaseFoldingResult::FullFoldingEntry &, unsigned int) {}

  template <typename... R>
  static constexpr void append(CaseFoldingResult::FullFoldingEntry &folds, unsigned int index,
                               int t, R &&...remain) {
    if (t < 0 || t > std::numeric_limits<uint16_t>::max()) {
      throw 1243; // NOLINT
    }
    folds[index] = static_cast<uint16_t>(t);
    append(folds, index + 1, std::forward<R>(remain)...);
  }

  template <typename... T>
  static constexpr CaseFoldingResult::FullFoldingEntry init(T &&...arg) {
    static_assert(sizeof...(T) == CaseFoldingResult::FULL_FOLD_ENTRY_SIZE);
    CaseFoldingResult::FullFoldingEntry folds{};
    append(folds, 0, std::forward<T>(arg)...);
    return folds;
  }

  template <typename... T>
  constexpr FullFoldEntry(int code, T &&...arg) // NOLINT
      : code(code), folds{init(std::forward<T>(arg)...)} {}
};

#define CASE_FOLD_T_ENTRY std::pair<uint16_t, uint16_t>
#define CASE_FOLD_F_ENTRY FullFoldEntry

#include "full_case_fold.in"

struct CompareFullFoldEntry {
  bool operator()(const FullFoldEntry &x, int y) const { return x.code < y; }

  bool operator()(int x, const FullFoldEntry &y) const { return x < y.code; }
};

CaseFoldingResult doCaseFolding(int codePoint, const CaseFoldOp op) {
  if (hasFlag(op, CaseFoldOp::TURKIC)) {
    for (auto [before, after] : case_fold_T_table) {
      if (before == codePoint) {
        return CaseFoldingResult(after);
      }
    }
  }
  if (hasFlag(op, CaseFoldOp::FULL_FOLD)) {
    // for 'F'
    if (codePoint <= (std::end(case_fold_F_table) - 1)->code) {
      auto iter = std::lower_bound(std::begin(case_fold_F_table), std::end(case_fold_F_table),
                                   codePoint, CompareFullFoldEntry());
      if (iter != std::end(case_fold_F_table) && iter->code == codePoint) {
        CaseFoldingResult::FullFoldingEntry entry;
        for (unsigned int i = 0; i < std::size(iter->folds); i++) {
          entry[i] = iter->folds[i];
        }
        return CaseFoldingResult(entry);
      }
    }
    // for 'C'
    if (isSimpleCaseFoldTarget(codePoint)) {
      auto entry = lookupSimpleCaseFoldEntry(static_cast<unsigned int>(codePoint));
      if ((entry & 0x01u) == 0) { // only allow 'C'
        int delta = static_cast<int>(entry & ~1u) / 2;
        return CaseFoldingResult(codePoint + delta);
      }
    }
    return CaseFoldingResult(codePoint);
  }
  return CaseFoldingResult(doSimpleCaseFolding(codePoint));
}

} // namespace arsh