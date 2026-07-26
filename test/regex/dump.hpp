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

#ifndef ARSH_REGEX_TEST_DUMP_HPP
#define ARSH_REGEX_TEST_DUMP_HPP

#include <vector>

#include <misc/result.hpp>
#include <misc/unicode.hpp>
#include <regex/capture.h>
#include <regex/flag.h>

namespace arsh::regex {

#define JSONIFY(m) t(#m, m)

struct Target {
  Union<std::string, std::vector<uint8_t>> pattern;
  Union<std::string, std::vector<uint8_t>> input;
  Mode mode{Mode::BMP};
  Modifier modifiers{};

  Flag flag() const { return {this->mode, this->modifiers}; }

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(pattern);
    JSONIFY(input);
    JSONIFY(mode);
    JSONIFY(modifiers);
  }

  void beforeSerialize() {
    toArray(this->pattern);
    toArray(this->input);
  }

  void afterDeserialize() {
    toString(this->pattern);
    toString(this->input);
  }

private:
  static bool isUTF8(const StringRef ref) {
    const char *end = ref.end();
    for (const char *iter = ref.begin(); iter != end;) {
      if (unsigned int len = UnicodeUtil::utf8ValidateChar(iter, end)) {
        iter += len;
        continue;
      }
      return false;
    }
    return true;
  }

  static void toArray(Union<std::string, std::vector<uint8_t>> &value) {
    if (is<std::string>(value) && !isUTF8(get<std::string>(value))) {
      const StringRef ref = get<std::string>(value);
      std::vector<uint8_t> tmp;
      tmp.reserve(ref.size());
      for (char ch : ref) {
        tmp.push_back(static_cast<uint8_t>(ch));
      }
      value = std::move(tmp);
    }
  }

  static void toString(Union<std::string, std::vector<uint8_t>> &value) {
    if (is<std::vector<uint8_t>>(value)) {
      auto &array = get<std::vector<uint8_t>>(value);
      std::string tmp;
      tmp.reserve(array.size());
      for (auto &e : array) {
        tmp += static_cast<char>(e);
      }
      value = std::move(tmp);
    }
  }
};

inline std::string formatCaptures(const std::vector<Capture> &captures) {
  std::string ret;
  for (auto &c : captures) {
    if (!c) {
      ret += "(unset)\n";
      continue;
    }
    ret += "(offset=";
    ret += std::to_string(c.offset);
    ret += ", size=";
    ret += std::to_string(c.size);
    ret += ")\n";
  }
  return ret;
}

} // namespace arsh::regex

#endif // ARSH_REGEX_TEST_DUMP_HPP
