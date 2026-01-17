/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#include "word.h"

#include "../misc/enum_util.hpp"
#include "misc/unicode.hpp"

namespace arsh {

WordBreakProperty getWordBreakProperty(const int codePoint) {
  if (codePoint < 0) {
    return WordBreakProperty::Newline; // invalid code points are always word boundary
  }

#define UNICODE_PROPERTY_RANGE CodePointWithMeta
#define PROPERTY(E) toUnderlying(WordBreakProperty::E)

#include "word_break_property.in"

#undef PROPERTY
#undef UNICODE_PROPERTY_RANGE

  auto iter =
      std::upper_bound(std::begin(word_break_property_table), std::end(word_break_property_table),
                       codePoint, CodePointWithMeta::Comp());
  if (iter != std::end(word_break_property_table)) {
    return static_cast<WordBreakProperty>((iter - 1)->getMeta());
  }
  return WordBreakProperty::Any;
}

} // namespace arsh