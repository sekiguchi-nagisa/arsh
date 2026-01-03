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

#ifndef ARSH_UNICODE_SET_BUILDER_H
#define ARSH_UNICODE_SET_BUILDER_H

#include <vector>

#include "../misc/codepoint_set.hpp"

namespace arsh {

class CodePointSetBuilder {
private:
  // code point range (inclusive, inclusive)
  std::vector<std::pair<int, int>> codePointRanges;

public:
  const auto &getCodePointRanges() const { return this->codePointRanges; }

  /**
   * for union
   * @param ref
   */
  void add(CodePointSetRef ref);

  /**
   * for difference
   * @param ref
   */
  void sub(CodePointSetRef ref);

  void intersect(CodePointSetRef ref);

  CodePointSet build();

private:
  void sortAndCompact(); // TODO: lazy compaction
};

} // namespace arsh

#endif // ARSH_UNICODE_SET_BUILDER_H
