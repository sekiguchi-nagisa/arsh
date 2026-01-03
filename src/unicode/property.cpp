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

#include "property.h"
#include "../misc/codepoint_set.hpp"
#include "../misc/enum_util.hpp"
#include "../misc/unicode.hpp"

namespace arsh {
namespace ucp {

#define CATEGORY_SET_RANGE BMPCodePointRange
#define CATEGORY_SET_RANGE_TABLE CodePointSetRef

#include "ucp_general_category.in"

static constexpr struct {
  char name[3];
} categoryNames[] = {
#define GEN_TABLE(E) {#E},
    EACH_UCP_CATEGORY(GEN_TABLE)
#undef GEN_TABLE
};

Optional<Category> parseCategory(const StringRef ref) {
  for (unsigned int index = 0; index < std::size(categoryNames); index++) {
    if (ref == categoryNames[index].name) {
      return static_cast<Category>(index);
    }
  }
  return {};
}

Optional<Category> getCategory(const int codePoint) {
  if (UnicodeUtil::isValidCodePoint(codePoint)) {
    for (unsigned int i = 0; i < std::size(category_set_table_except_Cn); i++) {
      if (category_set_table_except_Cn[i].contains(codePoint)) {
        return static_cast<Category>(i);
      }
    }
    return Category::Cn;
  }
  return {};
}

} // namespace ucp

const char *toString(const ucp::Category category) {
  if (auto index = toUnderlying(category); index < std::size(ucp::categoryNames)) {
    return ucp::categoryNames[index].name;
  }
  return nullptr;
}

} // namespace arsh