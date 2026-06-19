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

#include "js_regex.h"

#include <optional>

namespace arsh::re262 {

static std::optional<std::pair<regex::Flag, JSRegex::ExtraFlag>> parseFlags(StringRef ref) {
  (void)ref;
  return {};
}

JSRegexPtr createJSRegexFrom(StringRef pattern, StringRef flagStr, std::string *err) {
  auto ret = parseFlags(flagStr);
  if (!ret.has_value()) {
    if (err) {
      *err += "Invalid regular expression flags";
    }
    return nullptr;
  }
  // const auto [flag, extra] = ret.value();

  (void)pattern;
  return nullptr;
}

JSRegexPtr createJSRegexFromLiteral(StringRef literal, std::string *err) {
  if (literal.size() < 2 || literal.front() != '/' || literal.lastIndexOf("/") == 0 ||
      literal.lastIndexOf("/") == StringRef::npos) {
    if (err) {
      *err += "invalid regex literal";
    }
    return nullptr;
  }
  auto ret = literal.lastIndexOf("/");
  auto pattern = literal.slice(1, ret);
  auto flags = literal.substr(ret + 1);
  return createJSRegexFrom(pattern, flags, err);
}

} // namespace arsh::re262