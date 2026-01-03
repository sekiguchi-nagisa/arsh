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

#include "set_builder.h"
#include "../misc/buffer.hpp"
#include "../misc/detect.hpp"
#include "../misc/unicode.hpp"

namespace arsh {

// #################################
// ##     CodePointSetBuilder     ##
// #################################

void CodePointSetBuilder::add(CodePointSetRef ref) {
  auto bmpRanges = ref.getBMPRanges();
  this->codePointRanges.reserve(this->codePointRanges.size() + bmpRanges.size());
  for (auto &e : bmpRanges) {
    this->codePointRanges.emplace_back(e.firstBMP(), e.lastBMP());
  }
  auto nonBmpRanges = ref.getNonBMPRanges();
  this->codePointRanges.reserve(this->codePointRanges.size() + nonBmpRanges.size());
  for (auto &e : nonBmpRanges) {
    this->codePointRanges.emplace_back(e.firstNonBMP(), e.lastNonBMP());
  }
  this->sortAndCompact();
}

template <typename Func>
static constexpr bool remove_requirement_v = std::is_same_v<bool, std::invoke_result_t<Func, int>>;

template <typename Func, enable_when<remove_requirement_v<Func>> = nullptr>
static void removeIf(std::vector<std::pair<int, int>> &ranges, Func func) {
  /**
   * consider the following cases
   * (1,5), if func(5) == true
   * => (1,4)
   * (1,5), if func(1) == true
   * => (2,5)
   * (1,5), if func(4) == true
   * => (1,3), (5,5)
   * (1,1), if func(1) == true
   * => remove
   */
  const unsigned int oldEnd = ranges.size();
  int lastIndex = -1;
  for (unsigned int index = 0; index < oldEnd; index++) {
    for (int codePoint = ranges[index].first; codePoint <= ranges[index].second; codePoint++) {
      if (!func(codePoint)) {
        continue;
      }
      auto &range = ranges[index];
      if (codePoint == range.first) {
        if (range.first == range.second) { // remove
          goto END;
        }
        range.first++;
      } else if (codePoint == range.second) {
        range.second--;
      } else {
        int newFirst = range.first;
        int newLast = codePoint - 1;
        range.first = codePoint + 1;
        if (static_cast<unsigned int>(lastIndex + 1) < index) {
          lastIndex++;
          ranges[lastIndex] = std::make_pair(newFirst, newLast);
        } else {
          ranges.emplace_back(newFirst, newLast);
        }
      }
    }
    lastIndex++;
    if (static_cast<unsigned int>(lastIndex) != index) {
      ranges[lastIndex] = ranges[index];
    }
  END: {}
  }

  // compact
  ranges.erase(ranges.begin() + (lastIndex + 1), ranges.begin() + oldEnd);
}

void CodePointSetBuilder::sub(CodePointSetRef ref) {
  removeIf(this->codePointRanges, [&ref](int codePoint) { return ref.contains(codePoint); });
  this->sortAndCompact();
}

void CodePointSetBuilder::intersect(CodePointSetRef ref) {
  removeIf(this->codePointRanges, [&ref](int codePoint) { return !ref.contains(codePoint); });
  this->sortAndCompact();
}

CodePointSet CodePointSetBuilder::build() {
  FlexBuffer<BMPCodePointRange> tmp;
  tmp.reserve(this->codePointRanges.size() / 2);
  unsigned short bmpSize = 0;
  unsigned int index = 0;
  for (; index < this->codePointRanges.size(); index++) {
    auto [first, last] = this->codePointRanges[index];
    if (UnicodeUtil::isBmpCodePoint(first)) {
      if (UnicodeUtil::isBmpCodePoint(last)) {
        tmp.push_back({static_cast<uint16_t>(first), static_cast<uint16_t>(last)});
        bmpSize++;
        continue;
      }
      tmp.push_back({static_cast<uint16_t>(first), static_cast<uint16_t>(UINT16_MAX)});
      bmpSize++;
      tmp.push_back({UINT16_MAX + 1});
      tmp.push_back({static_cast<uint32_t>(last)});
      index++;
    }
    break;
  }
  for (; index < this->codePointRanges.size(); index++) {
    auto [first, last] = this->codePointRanges[index];
    assert(UnicodeUtil::isSupplementaryCodePoint(first));
    assert(UnicodeUtil::isSupplementaryCodePoint(last));
    tmp.push_back({static_cast<uint32_t>(first)});
    tmp.push_back({static_cast<uint32_t>(last)});
  }
  return CodePointSet::take(bmpSize, std::move(tmp));
}

struct Compare {
  bool operator()(const std::pair<int, int> &x, const std::pair<int, int> &y) const {
    return x.first < y.first || (x.first == y.first && x.second < y.second);
  }
};

void CodePointSetBuilder::sortAndCompact() {
  if (this->codePointRanges.size() < 2) {
    return;
  }
  std::sort(this->codePointRanges.begin(), this->codePointRanges.end(), Compare());
  auto last = this->codePointRanges.begin();
  const auto end = this->codePointRanges.end();
  /*
   * merge the following cases
   * (1,5) (1,7) -> (1,7)
   * (1,5) (2,3) -> (1,5)
   * (1,5) (2,7) -> (1,7)
   * (1,5) (5,9) -> (1,9)
   * (1,5) (6,9) -> (1,9)
   * (1,5) (7,9) -> do nothing
   */
  for (auto iter = last + 1; iter != end; ++iter) {
    if (last->second + 1 >= iter->first) {
      last->second = std::max(last->second, iter->second);
    } else {
      ++last;
      if (last != iter) {
        *last = *iter;
      }
    }
  }
  this->codePointRanges.erase(last + 1, end);
}

} // namespace arsh