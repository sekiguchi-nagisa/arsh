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

#ifndef ARSH_TOOLS_HIGHLIGHTER_STYLE_H
#define ARSH_TOOLS_HIGHLIGHTER_STYLE_H

#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include <misc/num_util.hpp>

#include <highlighter_base.h>

namespace arsh::highlighter {

/**
 * true color
 */
struct Color {
  unsigned char red{0};
  unsigned char green{0};
  unsigned char blue{0};

  bool initialized{false};

  /**
   * parse #ffffff or #fff style string
   * @param code
   * @return
   * if parse failed, return uninitialized color
   */
  static Color parse(StringRef code);

  constexpr explicit operator bool() const { return this->initialized; }

  [[nodiscard]] double distance(Color o) const;

  /**
   *
   * @param factor
   * -1 ~ 1
   * @return
   */
  [[nodiscard]] Color changeBrightness(double factor) const;

  double brightness() const {
    return static_cast<double>(this->red + this->green + this->blue) / 3.0 / 255.0;
  }

  std::string toString() const;
};

class RuleValidationError : public std::logic_error {
public:
  RuleValidationError(std::string_view message, std::string_view target)
      : std::logic_error(std::string(message).append(": ").append(target)) {}
};

class ValidRule {
private:
  std::string_view view;

public:
  constexpr explicit ValidRule(std::string_view view) : view(validate(view)) {}

  [[nodiscard]] constexpr std::string_view getView() const { return this->view; }

  constexpr explicit operator bool() const { return true; } // for testing

private:
  static constexpr bool checkColorCode(std::string_view code) {
    switch (code.size()) {
    case 3:
    case 6:
      break;
    default:
      return false;
    }
    for (auto ch : code) {
      if (!isHex(ch)) {
        return false;
      }
    }
    return true;
  }

  static constexpr bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
  }

  static constexpr auto find(std::string_view value, char ch,
                             std::string_view::size_type offset = 0) {
    for (std::string_view::size_type pos = offset; pos < value.size(); pos++) {
      if (value[pos] == ch) {
        return pos;
      }
    }
    return std::string_view::npos;
  }

  static constexpr std::string_view validate(std::string_view value) {
    for (std::string_view::size_type pos = 0; pos != std::string_view::npos;) {
      auto ret = find(value, ' ', pos);
      auto sub = value.substr(pos, ret - pos);
      pos = ret != std::string_view::npos ? ret + 1 : ret;

      if (sub.empty()) {
        continue;
      }

      if (sub == "bold" || sub == "nobold" || sub == "italic" || sub == "noitalic" ||
          sub == "underline" || sub == "nounderline" || sub == "bg:" || sub == "border:") {
        continue;
      }

      if (startsWith(sub, "bg:#")) {
        sub.remove_prefix(std::string_view("bg:#").size());
        if (!checkColorCode(sub)) {
          throw RuleValidationError("invalid background color code", sub);
        }
      } else if (startsWith(sub, "border:#")) {
        sub.remove_prefix(std::string_view("border:#").size());
        if (!checkColorCode(sub)) {
          throw RuleValidationError("invalid border color code", sub);
        }
      } else if (startsWith(sub, "#")) {
        sub.remove_prefix(1);
        if (!checkColorCode(sub)) {
          throw RuleValidationError("invalid color code", sub);
        }
      } else {
        throw RuleValidationError("invalid rule", sub);
      }
    }
    return value;
  }
};

struct StyleRule {
  Color text;
  Color background;
  Color border; // for html
  bool bold{false};
  bool italic{false};
  bool underline{false};

  [[nodiscard]] StyleRule synthesize(const ValidRule &valid) const;
};

struct Style {
  StyleRule foreground;
  StyleRule background;
  std::unordered_map<HighlightTokenClass, StyleRule> rules;

  const StyleRule *find(HighlightTokenClass tokenClass) const;

  const StyleRule &findOrDefault(HighlightTokenClass tokenClass) const {
    if (auto *rule = this->find(tokenClass)) {
      return *rule;
    }
    return this->foreground;
  }
};

class StyleMap {
private:
  std::unordered_map<std::string, Style> values;

public:
  StyleMap();

  bool add(const char *name, Style &&style) {
    return this->values.emplace(name, std::move(style)).second;
  }

  [[nodiscard]] const Style *find(const std::string &name) const;

  const auto &getValues() const { return this->values; }

  bool defineStyle(const char *name, std::unordered_map<HighlightTokenClass, ValidRule> &&rules);
};

#define DEFINE_HIGHLIGHT_STYLE(name)                                                               \
  struct style_wrapper_##name {                                                                    \
    using Rules = std::unordered_map<HighlightTokenClass, ValidRule>;                              \
    static Rules buildRules();                                                                     \
  };                                                                                               \
  style_wrapper_##name::Rules style_wrapper_##name::buildRules()

} // namespace arsh::highlighter

#endif // ARSH_TOOLS_HIGHLIGHTER_STYLE_H
