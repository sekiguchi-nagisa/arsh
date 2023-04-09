/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef MISC_LIB_EDIT_DISTANCE_HPP
#define MISC_LIB_EDIT_DISTANCE_HPP

#include <algorithm>

#include "buffer.hpp"
#include "string_ref.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

class EditDistance {
private:
  FlexBuffer<unsigned int> row;
  const unsigned int substitutionCost;

public:
  explicit EditDistance(unsigned int cost) : substitutionCost(cost) {}

  EditDistance() : EditDistance(1) {}

  /**
   * compute Levenshtein distance.
   * based on
   *  http://en.wikipedia.org/wiki/Levenshtein_distance
   *  https://www.baeldung.com/cs/levenshtein-distance-computation
   *
   * @param src
   * @param target
   * @return
   */
  unsigned int operator()(StringRef src, StringRef target) {
    const auto srcSize = src.size();
    const auto targetSize = target.size();
    this->row.resize(targetSize + 1);

    for (StringRef::size_type i = 0; i <= targetSize; i++) {
      this->row[i] = i;
    }

    for (StringRef::size_type i = 0; i < srcSize; i++) {
      this->row[0] = i + 1;
      unsigned int prev = i;
      for (StringRef::size_type j = 0; j < targetSize; j++) {
        unsigned int cur = prev;
        prev = this->row[j + 1];

        unsigned int deletion = prev + 1;
        unsigned int insertion = this->row[j] + 1;
        unsigned int substitution = cur + (src[i] != target[j] ? this->substitutionCost : 0);
        this->row[j + 1] = std::min({deletion, insertion, substitution});
      }
    }
    return this->row[targetSize];
  }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_EDIT_DISTANCE_HPP
