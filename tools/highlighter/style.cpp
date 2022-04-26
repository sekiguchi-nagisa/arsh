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
#include "styles/algol.hpp"
#include "styles/colorful.hpp"
#include "styles/darcula.hpp"
#include "styles/monokai.hpp"
#include "styles/null.hpp"

namespace ydsh::highlighter {

// ###################
// ##     Color     ##
// ###################

Color Color::parse(StringRef code) {
  if (code.empty() || code[0] != '#') {
    return {};
  }
  code.removePrefix(1);

  char data[6] = {};
  if (code.size() == 3) { // convert rgb -> rrggbb
    data[0] = data[1] = code[0];
    data[2] = data[3] = code[1];
    data[4] = data[5] = code[2];
    code = StringRef(data, std::size(data));
  } else if (code.size() != 6) {
    return {};
  }

  unsigned int value = 0;
  for (auto ch : code) {
    if (!isHex(ch)) {
      return {};
    }
    value *= 16;
    value += hexToNum(ch);
  }
  return Color{
      .red = static_cast<unsigned char>((value >> 16) & 0xFF),
      .green = static_cast<unsigned char>((value >> 8) & 0xFF),
      .blue = static_cast<unsigned char>(value & 0xFF),
      .initialized = true,
  };
}

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

/**
 * see ()
 * @param factor
 * if negative,darker color, if positive lighter color
 * @return
 */
Color Color::changeBrightness(double factor) const {
  Color c = *this;
  if (factor < 0) {
    factor = 1 + factor;
    c.red = static_cast<unsigned char>(c.red * factor);
    c.green = static_cast<unsigned char>(c.green * factor);
    c.blue = static_cast<unsigned char>(c.blue * factor);
  } else {
    c.red = static_cast<unsigned char>((255 - c.red) * factor + c.red);
    c.green = static_cast<unsigned char>((255 - c.green) * factor + c.green);
    c.blue = static_cast<unsigned char>((255 - c.blue) * factor + c.blue);
  }
  return c;
}

std::string Color::toString() const {
  std::string value;
  if (this->initialized) {
    char buf[16];
    snprintf(buf, std::size(buf), "#%02x%02x%02x", this->red, this->green, this->blue);
    value += buf;
  } else {
    value += "<invalid>";
  }
  return value;
}

// #######################
// ##     StyleRule     ##
// #######################

StyleRule StyleRule::synthesize(const ValidRule &valid) const {
  const StringRef value(valid.getView().data(), valid.getView().size());
  StyleRule rule = *this;

  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto ret = value.find(' ', pos);
    auto sub = value.substr(pos, ret - pos);
    pos = ret != StringRef::npos ? ret + 1 : ret;

    if (sub.empty()) {
      continue;
    }
    if (sub == "bold") {
      rule.bold = true;
    } else if (sub == "nobold") {
      rule.bold = false;
    } else if (sub == "italic") {
      rule.italic = true;
    } else if (sub == "noitalic") {
      rule.italic = false;
    } else if (sub == "underline") {
      rule.underline = true;
    } else if (sub == "nounderline") {
      rule.underline = false;
    } else if (sub == "bg:") {
      rule.background = {};
    } else if (sub.startsWith("bg:#")) {
      sub.removePrefix(strlen("bg:"));
      Color color = Color::parse(sub);
      assert(color);
      rule.background = color;
    } else if (sub == "border:") {
      rule.border = {};
    } else if (sub.startsWith("border:#")) {
      sub.removePrefix(strlen("border:"));
      Color color = Color::parse(sub);
      assert(color);
      rule.border = color;
    } else if (sub.startsWith("#")) {
      Color color = Color::parse(sub);
      assert(color);
      rule.text = color;
    }
  }
  return rule;
}

const StyleRule *Style::find(HighlightTokenClass tokenClass) const {
  auto iter = this->getRules().find(tokenClass);
  if (iter != this->getRules().end()) {
    return &iter->second;
  }
  return nullptr;
}

// ######################
// ##     StyleMap     ##
// ######################

#define LOAD_HIGHLIGHT_STYLE(name) defineStyle(#name, style_wrapper_##name::buildRules())

StyleMap::StyleMap() {
  LOAD_HIGHLIGHT_STYLE(algol);
  LOAD_HIGHLIGHT_STYLE(colorful);
  LOAD_HIGHLIGHT_STYLE(darcula);
  LOAD_HIGHLIGHT_STYLE(monokai);
  LOAD_HIGHLIGHT_STYLE(null);
}

const Style *StyleMap::find(StringRef name) const {
  auto iter = this->values.find(name);
  if (iter != this->values.end()) {
    return &iter->second;
  }
  return nullptr;
}

bool StyleMap::defineStyle(const char *name,
                           std::unordered_map<HighlightTokenClass, ValidRule> &&rules) {
  auto background = StyleRule();
  if (auto iter = rules.find(HighlightTokenClass::BACKGROUND_); iter != rules.end()) {
    background = background.synthesize(iter->second);
  }

  auto foreground = StyleRule();
  if (auto iter = rules.find(HighlightTokenClass::FOREGROUND_); iter != rules.end()) {
    foreground = foreground.synthesize(iter->second);
  }

  auto lineno = StyleRule();
  if (auto iter = rules.find(HighlightTokenClass::LINENO_); iter != rules.end()) {
    lineno = lineno.synthesize(iter->second);
  } else {
    auto color = background.background;
    lineno.text = color.changeBrightness(color.brightness() > 0.5 ? -0.5 : 0.5);
  }

  std::unordered_map<HighlightTokenClass, StyleRule> map;
  for (auto &e : rules) {
    switch (e.first) {
    case HighlightTokenClass::FOREGROUND_:
    case HighlightTokenClass::BACKGROUND_:
    case HighlightTokenClass::LINENO_:
      continue;
    default:
      break;
    }
    auto rule = foreground.synthesize(e.second);
    map.emplace(e.first, rule);
  }
  map.emplace(HighlightTokenClass::LINENO_, lineno);

  return this->add(Style(name, foreground, background, std::move(map)));
}

} // namespace ydsh::highlighter