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

#include <cassert>

#include "../misc/buffer.hpp"
#include "../misc/unicode.hpp"
#include "set_builder.h"

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
  auto packedRanges = ref.getPackedNonBMPRanges();
  this->codePointRanges.reserve(this->codePointRanges.size() + packedRanges.size());
  for (auto &e : packedRanges) {
    this->codePointRanges.emplace_back(e.firstNonBMP(), e.lastNonBMP());
  }
  auto nonBmpRanges = ref.getNonBMPRanges();
  this->codePointRanges.reserve(this->codePointRanges.size() + nonBmpRanges.size());
  for (auto &e : nonBmpRanges) {
    this->codePointRanges.emplace_back(e.firstNonBMP(), e.lastNonBMP());
  }
  this->sortAndCompact();
}

void CodePointSetBuilder::add(const CodePointSetBuilder &other) {
  this->codePointRanges.reserve(this->codePointRanges.size() + other.getCodePointRanges().size());
  for (auto &e : other.getCodePointRanges()) {
    this->codePointRanges.push_back(e);
  }
  this->sortAndCompact();
}

void CodePointSetBuilder::add(int first, int last) {
  int actualFirst = std::max(0, std::min(first, last));
  int actualLast = std::min(UnicodeUtil::CODE_POINT_MAX, std::max(first, last));
  this->codePointRanges.emplace_back(actualFirst, actualLast);
  this->sortAndCompact();
}

void CodePointSetBuilder::complement() {
  std::vector<std::pair<int, int>> buf;
  const unsigned int size = this->codePointRanges.size();
  for (unsigned int i = 0; i < size; i++) {
    auto [first, last] = this->codePointRanges[i];
    int newFirst = 0;
    int newLast = 0;
    if (i == 0) {
      if (first == 0) {
        continue;
      }
      newFirst = 0;
      newLast = first - 1;
    } else {
      newFirst = this->codePointRanges[i - 1].second + 1;
      newLast = first - 1;
    }
    assert(newFirst <= newLast);
    buf.emplace_back(newFirst, newLast);
  }
  if (size) {
    if (int last = this->codePointRanges.back().second; last != UnicodeUtil::CODE_POINT_MAX) {
      buf.emplace_back(last + 1, UnicodeUtil::CODE_POINT_MAX);
    }
  } else {
    buf.emplace_back(0, UnicodeUtil::CODE_POINT_MAX);
  }
  this->codePointRanges = std::move(buf);
}

void CodePointSetBuilder::remove(const CodePointSetRef ref, const bool negate) {
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
  const unsigned int oldEnd = this->codePointRanges.size();
  int lastIndex = -1;
  for (unsigned int index = 0; index < oldEnd; index++) {
    for (int codePoint = this->codePointRanges[index].first;
         codePoint <= this->codePointRanges[index].second; codePoint++) {
      bool matched = ref.contains(codePoint);
      if (negate) {
        matched = !matched;
      }
      if (!matched) {
        continue;
      }
      auto &range = this->codePointRanges[index];
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
          this->codePointRanges[lastIndex] = std::make_pair(newFirst, newLast);
        } else {
          this->codePointRanges.emplace_back(newFirst, newLast);
        }
      }
    }
    lastIndex++;
    if (static_cast<unsigned int>(lastIndex) != index) {
      this->codePointRanges[lastIndex] = this->codePointRanges[index];
    }
  END: {}
  }

  // compact
  this->codePointRanges.erase(this->codePointRanges.begin() + (lastIndex + 1),
                              this->codePointRanges.begin() + oldEnd);
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