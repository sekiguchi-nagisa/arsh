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

#include <cmath>

#include "style.h"

// for style definition. these headers seem to be unused, but actually used
#include "styles/darcula.hpp"
#include "styles/null.hpp"

namespace ydsh::highlighter {

/**
 * see (https://www.compuphase.com/cmetric.htm)
 * @param o
 * @return
 */
double Color::distance(Color o) const {
  auto r1 = static_cast<int64_t>(this->red);
  auto g1 = static_cast<int64_t>(this->green);
  auto b1 = static_cast<int64_t>(this->blue);

  auto r2 = static_cast<int64_t>(o.red);
  auto g2 = static_cast<int64_t>(o.green);
  auto b2 = static_cast<int64_t>(o.blue);

  int64_t rmean = (r1 + r2) / 2;
  int64_t r = r1 - r2;
  int64_t g = g1 - g2;
  int64_t b = b1 - b2;

  return std::sqrt((((512 + rmean) * r * r) >> 8) + 4 * g * g + (((767 - rmean) * b * b) >> 8));
}

const StyleRule *Style::find(HighlightTokenClass tokenClass) const {
  auto iter = this->getRules().find(tokenClass);
  if (iter != this->getRules().end()) {
    return &iter->second;
  }
  return nullptr;
}

class StyleMap : public Singleton<StyleMap> {
private:
  StrRefMap<Style> values;

public:
  bool add(Style &&style) {
    StringRef name = style.getName();
    return this->values.emplace(name, std::move(style)).second;
  }

  [[nodiscard]] const Style *find(StringRef name) const {
    auto iter = this->values.find(name);
    if (iter != this->values.end()) {
      return &iter->second;
    }
    return nullptr;
  }
};

bool defineStyle(const char *name, std::unordered_map<HighlightTokenClass, StyleRule> &&rules) {
  auto &styleMap = StyleMap::instance();
  return styleMap.add(Style(name, std::move(rules)));
}

const Style *findStyle(StringRef styleName) {
  auto &styleMap = StyleMap::instance();
  return styleMap.find(styleName);
}

} // namespace ydsh::highlighter