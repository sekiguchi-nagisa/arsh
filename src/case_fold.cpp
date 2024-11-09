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

#include "case_fold.h"
#include "misc/array_ref.hpp"
#include "misc/unicode.hpp"

namespace arsh {

using CaseMappingShortEntry = std::pair<uint16_t, uint16_t>;
using CaseMappingFullFoldEntry = uint16_t[CaseFoldingResult::FULL_FOLD_ENTRY_SIZE + 1];

#define CASE_FOLD_shortC_ENTRY CaseMappingShortEntry
#define CASE_FOLD_longC_ENTRY std::pair<int, int>
#define CASE_FOLD_S_ENTRY CaseMappingShortEntry
#define CASE_FOLD_F_ENTRY CaseMappingFullFoldEntry
#define CASE_FOLD_T_ENTRY CaseMappingShortEntry

#include "misc/case_fold.in"

#undef CASE_FOLD_shortC_ENTRY
#undef CASE_FOLD_longC_ENTRY
#undef CASE_FOLD_S_ENTRY
#undef CASE_FOLD_F_ENTRY
#undef CASE_FOLD_T_ENTRY

using CaseMappingRange = ArrayRef<CaseMappingShortEntry>;

struct CompareShortEntry {
  bool operator()(const CaseMappingShortEntry &x, uint16_t y) const { return x.first < y; }

  bool operator()(uint16_t x, const CaseMappingShortEntry &y) const { return x < y.first; }
};

struct CompareLongEntry {
  bool operator()(const std::pair<int, int> &x, int y) const { return x.first < y; }

  bool operator()(int x, const std::pair<int, int> &y) const { return x < y.first; }
};

struct CompareFullFoldEntry {
  bool operator()(const CaseMappingFullFoldEntry &x, uint16_t y) const { return x[0] < y; }

  bool operator()(uint16_t x, const CaseMappingFullFoldEntry &y) const { return x < y[0]; }
};

static CaseFoldingResult doCaseFoldingBMP(uint16_t codePoint, const CaseFoldOp op) {
  CaseMappingRange targets[3];
  unsigned int targetSize = 0;
  if (hasFlag(op, CaseFoldOp::TURKIC)) {
    targets[targetSize++] = CaseMappingRange(case_fold_T_table);
  }
  targets[targetSize++] = CaseMappingRange(case_fold_shortC_table);
  if (!hasFlag(op, CaseFoldOp::FULL_FOLD)) {
    targets[targetSize++] = CaseMappingRange(case_fold_S_table);
  }

  for (unsigned int i = 0; i < targetSize; i++) {
    const auto ref = targets[i];
    auto iter = std::lower_bound(ref.begin(), ref.end(), codePoint, CompareShortEntry());
    if (iter != ref.end() && iter->first == codePoint) {
      return CaseFoldingResult(iter->second);
    }
  }

  // full case folding
  if (hasFlag(op, CaseFoldOp::FULL_FOLD)) {
    auto iter = std::lower_bound(std::begin(case_fold_F_table), std::end(case_fold_F_table),
                                 codePoint, CompareFullFoldEntry());
    if (iter != std::end(case_fold_F_table) && (*iter)[0] == codePoint) {
      CaseFoldingResult::FullFoldingEntry entry;
      for (unsigned int i = 1; i < std::size(*iter); i++) {
        entry[i - 1] = (*iter)[i];
      }
      return CaseFoldingResult(entry);
    }
  }

  return CaseFoldingResult(codePoint);
}

CaseFoldingResult doCaseFolding(int codePoint, CaseFoldOp op) {
  if (UnicodeUtil::isBmpCodePoint(codePoint)) {
    return doCaseFoldingBMP(static_cast<uint16_t>(codePoint), op);
  }

  auto iter = std::lower_bound(std::begin(case_fold_longC_table), std::end(case_fold_longC_table),
                               codePoint, CompareShortEntry());
  if (iter != std::end(case_fold_longC_table) && iter->first == codePoint) {
    return CaseFoldingResult(iter->second);
  }
  return CaseFoldingResult(codePoint);
}

} // namespace arsh