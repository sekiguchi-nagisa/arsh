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

#ifndef ARSH_REGEX_FLAG_HPP
#define ARSH_REGEX_FLAG_HPP

#include "misc/enum_util.hpp"
#include "misc/flag_util.hpp"
#include "misc/result.hpp"
#include "misc/string_ref.hpp"

namespace arsh::regex {

constexpr const char *VERSION = "ES2025";

enum class Mode : unsigned char {
  BMP,         // only match UTF16 bmp
  UNICODE,     // 'u' (default)
  UNICODE_SET, // 'v'
};

#define EACH_RE_MODIFIER(E)                                                                        \
  E(IGNORE_CASE, 'i', (1u << 0u))                                                                  \
  E(MULTILINE, 'm', (1u << 1u))                                                                    \
  E(DOT_ALL, 's', (1u << 2u))

enum class Modifier : unsigned char {
  NONE = 0u,
#define GEN_ENUM(E, S, D) E = (D),
  EACH_RE_MODIFIER(GEN_ENUM)
#undef GEN_ENUM
};

} // namespace arsh::regex

template <>
struct arsh::allow_enum_bitop<arsh::regex::Modifier> : std::true_type {};

namespace arsh::regex {

class Flag {
private:
  Mode mode_{Mode::BMP};
  Modifier modifiers_{};

public:
  constexpr Flag() = default;

  constexpr Flag(Mode mode, Modifier modifiers) : mode_(mode), modifiers_(modifiers) {}

  Mode mode() const { return this->mode_; }

  Modifier modifiers() const { return this->modifiers_; }

  bool is(Mode m) const { return this->mode() == m; }

  bool has(Modifier m) const { return hasFlag(this->modifiers(), m); }

  bool isEitherUnicodeMode() const {
    return toUnderlying(this->mode()) >= toUnderlying(Mode::UNICODE);
  }

  static Optional<Flag> parse(const StringRef ref, std::string *err) {
    return parse(ref, Mode::UNICODE, err);
  }

  static Optional<Flag> parse(const StringRef ref, const Mode defaultMode, std::string *err) {
    Optional<Mode> mode;
    Modifier modifiers{};
    for (char ch : ref) {
      switch (ch) {
#define GEN_CASE(E, S, D)                                                                          \
  case S:                                                                                          \
    setFlag(modifiers, Modifier::E);                                                               \
    break;
        EACH_RE_MODIFIER(GEN_CASE)
#undef GEN_CASE
      case 'u':
        if (mode.hasValue() && mode.unwrap() != Mode::UNICODE) {
          if (err) {
            *err += "cannot specify `u' flag, since `v' flag has already been specified";
          }
          return {};
        }
        mode = Mode::UNICODE;
        break;
      case 'v':
        if (mode.hasValue() && mode.unwrap() != Mode::UNICODE_SET) {
          if (err) {
            *err += "cannot specify `v' flag, since `u' flag has already been specified";
          }
          return {};
        }
        mode = Mode::UNICODE_SET;
        break;
      default:
        if (err) {
          *err += "invalid regex flag: `";
          *err += ch;
          *err += '\'';
        }
        return {};
      }
    }
    return Flag(mode.hasValue() ? mode.unwrap() : defaultMode, modifiers);
  }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_FLAG_HPP
