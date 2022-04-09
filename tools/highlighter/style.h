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

#ifndef YDSH_TOOLS_HIGHLIGHTER_STYLE_H
#define YDSH_TOOLS_HIGHLIGHTER_STYLE_H

#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include <misc/num_util.hpp>

#include "emitter.h"

namespace ydsh::highlighter {

/**
 * true color
 */
struct Color {
  unsigned char red{0};
  unsigned char green{0};
  unsigned char blue{0};

  bool initialized{false};

  constexpr explicit operator bool() const { return this->initialized; }

  [[nodiscard]] double distance(Color o) const;
};

struct StyleRule {
  Color text;
  Color background;
  Color border; // for html
  bool bold{false};
  bool italic{false};
  bool underline{false};
};

namespace detail {

constexpr Color parseColorCode(std::string_view code) {
  char data[6] = {};
  if (code.size() == 3) { // convert rgb -> rrggbb
    data[0] = data[1] = code[0];
    data[2] = data[3] = code[1];
    data[4] = data[5] = code[2];
    code = std::string_view(data, std::size(data));
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

constexpr bool startsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

constexpr auto find(std::string_view value, char ch, std::string_view::size_type offset = 0) {
  for (std::string_view::size_type pos = offset; pos < value.size(); pos++) {
    if (value[pos] == ch) {
      return pos;
    }
  }
  return std::string_view::npos;
}

/**
 *
 * @tparam N
 * @param value
 * must be string literal
 * @return
 */
constexpr StyleRule parseStyleRule(std::string_view value) {
  StyleRule rule;
  for (std::string_view::size_type pos = 0; pos != std::string_view::npos;) {
    auto ret = find(value, ' ', pos);
    auto sub = value.substr(pos, ret - pos);
    pos = ret != std::string_view::npos ? ret + 1 : ret;

    if (sub.empty()) {
      continue;
    }
    if (sub == "bold") {
      rule.bold = true;
    } else if (sub == "italic") {
      rule.italic = true;
    } else if (sub == "underline") {
      rule.underline = true;
    } else if (sub == "bg:") {
      rule.background = {};
    } else if (startsWith(sub, "bg:#")) {
      sub.remove_prefix(std::string_view("bg:#").size());
      if (Color color = parseColorCode(sub)) {
        rule.background = color;
      } else {
        throw std::logic_error("background color code");
      }
    } else if (startsWith(sub, "border:#")) {
      sub.remove_prefix(std::string_view("border:#").size());
      if (Color color = parseColorCode(sub)) {
        rule.border = color;
      } else {
        throw std::logic_error("border color code");
      }
    } else if (startsWith(sub, "#")) {
      sub.remove_prefix(1);
      if (Color color = parseColorCode(sub)) {
        rule.text = color;
      } else {
        throw std::logic_error("color code");
      }
    } else {
      throw std::logic_error("invalid rule");
    }
  }
  return rule;
}

} // namespace detail

constexpr StyleRule styleRule(std::string_view value) { return detail::parseStyleRule(value); }

class Style {
private:
  CStrPtr name;
  std::unordered_map<HighlightTokenClass, StyleRule> rules;

public:
  using Rules = std::unordered_map<HighlightTokenClass, StyleRule>;

  Style(const char *name, std::unordered_map<HighlightTokenClass, StyleRule> &&rules)
      : name(CStrPtr(strdup(name))), rules(std::move(rules)) {}

  [[nodiscard]] const char *getName() const { return this->name.get(); }

  [[nodiscard]] const auto &getRules() const { return this->rules; }

  const StyleRule *find(HighlightTokenClass tokenClass) const;
};

bool defineStyle(const char *name, const std::unordered_map<HighlightTokenClass, StyleRule> &rules);

const Style *findStyle(StringRef styleName);

#define DEFINE_HIGHLIGHT_STYLE(name)                                                               \
  struct [[maybe_unused]] style_wrapper_##name {                                                   \
    static bool value;                                                                             \
    static Style::Rules buildRules();                                                              \
  };                                                                                               \
  bool style_wrapper_##name::value = defineStyle(#name, style_wrapper_##name::buildRules());       \
  Style::Rules style_wrapper_##name::buildRules()

} // namespace ydsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_STYLE_H
