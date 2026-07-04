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

#ifndef ARSH_TOOLS_TEST262_REGEX_META_H
#define ARSH_TOOLS_TEST262_REGEX_META_H

#include <optional>
#include <string>
#include <vector>

#include <misc/enum_util.hpp>
#include <misc/string_ref.hpp>

namespace arsh::re262 {

#define EACH_TEST_META_PHASE(E)                                                                    \
  E(PARSE, "parse")                                                                                \
  E(RUNTIME, "runtime")

#define EACH_TEST_META_FLAG(E)                                                                     \
  E(ONLY_STRICT, "onlyStrict", (1u << 0u))                                                         \
  E(NO_STRICT, "noStrict", (1u << 1u))                                                             \
  E(GENERATED, "generated", (1u << 2u))

struct TestMetaData {
  enum class Flag : unsigned char {
    NONE = 0,
#define GEN_ENUM(E, S, D) E = (D),
    EACH_TEST_META_FLAG(GEN_ENUM)
#undef GEN_ENUM
  };

  std::string author;
  std::string description;
  std::string info;
  std::string esid;
  std::vector<std::string> features;
  std::vector<std::string> includes;
  Flag flags{Flag::NONE};

  enum class Phase : unsigned char {
#define GEN_ENUM(E, S) E,
    EACH_TEST_META_PHASE(GEN_ENUM)
#undef GEN_ENUM
  };

  struct Negative {
    Phase phase;
    std::string type;
  };

  std::optional<Negative> negative;

  static std::optional<TestMetaData> extractFrom(StringRef input, std::string *err);
};

std::string toString(TestMetaData::Flag flags);

const char *toString(TestMetaData::Phase phase);

inline std::string format(const TestMetaData::Negative &negative) {
  std::string str = "negative(phase=";
  str += toString(negative.phase);
  str += ", type=";
  str += negative.type;
  str += ')';
  return str;
}

} // namespace arsh::re262

namespace arsh {
template <>
struct allow_enum_bitop<re262::TestMetaData::Flag> : std::true_type {};
} // namespace arsh

#endif // ARSH_TOOLS_TEST262_REGEX_META_H
