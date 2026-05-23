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

#include "fold_table.h"

#include <algorithm>
#include <unordered_map>

#include "misc/hash.hpp"
#include "misc/unicode.hpp"
#include "unicode/case_fold.h"

namespace arsh::fold {

using CaseMappingShortEntry = std::pair<uint16_t, uint16_t>;

#define CASE_FOLD_shortC_ENTRY CaseMappingShortEntry
#define CASE_FOLD_longC_ENTRY std::pair<int, int>
#define CASE_FOLD_S_ENTRY CaseMappingShortEntry

#include "unicode/simple_case_fold.in"

struct CompareShortEntry {
  bool operator()(const CaseMappingShortEntry &x, uint16_t y) const { return x.first < y; }

  bool operator()(uint16_t x, const CaseMappingShortEntry &y) const { return x < y.first; }
};

struct CompareLongEntry {
  bool operator()(const std::pair<int, int> &x, int y) const { return x.first < y; }

  bool operator()(int x, const std::pair<int, int> &y) const { return x < y.first; }
};

enum class EntryType : unsigned char {
  C = 0,
  S = 1,
};

static std::pair<int, EntryType> lookupSimpleCaseFoldEntry(const int codePoint) {
  if (auto iter =
          std::lower_bound(std::begin(case_fold_shortC_table), std::end(case_fold_shortC_table),
                           codePoint, CompareShortEntry());
      iter != std::end(case_fold_shortC_table) && iter->first == codePoint) {
    return {iter->second, EntryType::C};
  }
  if (auto iter = std::lower_bound(std::begin(case_fold_longC_table),
                                   std::end(case_fold_longC_table), codePoint, CompareLongEntry());
      iter != std::end(case_fold_longC_table) && iter->first == codePoint) {
    return {iter->second, EntryType::C};
  }
  if (auto iter = std::lower_bound(std::begin(case_fold_S_table), std::end(case_fold_S_table),
                                   codePoint, CompareShortEntry());
      iter != std::end(case_fold_S_table) && iter->first == codePoint) {
    return {iter->second, EntryType::S};
  }
  return {codePoint, EntryType::C};
}

static int computeMaxFoldCodePoint() {
  int codePoint = 0;
  for (auto &[before, after] : case_fold_shortC_table) {
    codePoint = std::max<int>(codePoint, before);
  }
  for (auto &[before, after] : case_fold_longC_table) {
    codePoint = std::max<int>(codePoint, before);
  }
  for (auto &[before, after] : case_fold_S_table) {
    codePoint = std::max<int>(codePoint, before);
  }
  return codePoint;
}

// #######################
// ##     FoldTable     ##
// #######################

struct BlockHasher {
  size_t operator()(const FoldTable::Block &block) const {
    FNVHash::type hash = 42;
    for (auto &e : block.values) {
      uint8_t u8[4];
      memcpy(u8, &e, sizeof(decltype(e)));
      for (auto &u : u8) {
        FNVHash::update(hash, u);
      }
    }
    return hash;
  }
};

static int computeFoldEntry(const int codePoint) {
  assert(UnicodeUtil::isCodePoint(codePoint));
  auto [c, e] = lookupSimpleCaseFoldEntry(codePoint);
  int d = c - codePoint;
  int delta = (d * 2) + static_cast<int>(toUnderlying(e));
  assert(delta <= INT32_MAX && delta >= INT32_MIN);
  return delta;
}

FoldTable FoldTable::create() {
  std::vector<uint8_t> indexes;
  std::vector<Block> blocks;
  std::unordered_map<Block, uint16_t, BlockHasher> blockMap;
  blocks.emplace_back();
  blockMap.emplace(Block(), 0);

  const int maxFoldCodePoint = computeMaxFoldCodePoint();
  assert(maxFoldCodePoint > 0);
  unsigned int N = 1;
  for (; (N << SHIFT) < static_cast<unsigned int>(maxFoldCodePoint); N++)
    ;
  indexes.resize(N, 0);

  for (unsigned int index = 0; index < N; index++) {
    Block newBlock;
    for (unsigned short j = 0; j < BLOCK_SIZE; j++) {
      int codePoint = static_cast<int>(index * BLOCK_SIZE) + j;
      auto delta = computeFoldEntry(codePoint);
      newBlock.values[j] = delta;
    }
    if (auto iter = blockMap.find(newBlock); iter != blockMap.end()) {
      indexes[index] = iter->second;
    } else {
      const uint16_t blockIndex = blocks.size();
      blocks.push_back(newBlock);
      indexes[index] = blockIndex;
      blockMap.emplace(newBlock, blockIndex);
    }
  }

  return {std::move(indexes), std::move(blocks), maxFoldCodePoint};
}

std::pair<int, char> FoldTable::fold(int codePoint) const {
  if (codePoint >= 0 && codePoint <= this->maxFoldCodePoint) {
    const unsigned int index = this->blockIndexes[static_cast<unsigned int>(codePoint) >> SHIFT];
    const uint32_t v =
        this->blocks[index].values[static_cast<unsigned int>(codePoint) % BLOCK_SIZE];
    int delta = static_cast<int>(v & ~1) / 2;
    char t = (v & 0x01) == 1 ? 'S' : 'C';
    return {codePoint + delta, t};
  }
  return {codePoint, 'C'};
}

} // namespace arsh::fold