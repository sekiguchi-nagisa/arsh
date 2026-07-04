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

#ifndef ARSH_TOOLS_TEST262_JS_REGEX_H
#define ARSH_TOOLS_TEST262_JS_REGEX_H

#include "js.h"

#include <optional>

namespace arsh::re262 {

void defineJSRegex(const std::shared_ptr<JSEnv> &global);

JSRegexPtr createJSRegexFrom(const JSObjectPtr &prototype, StringRef pattern, StringRef flagStr,
                             std::string *err);

JSRegexPtr createJSRegexFromLiteral(const JSObjectPtr &prototype, StringRef literal,
                                    std::string *err);

std::string toStringFlags(const JSRegex &regex);

std::string toString(const JSRegex &regex);

JSValue getOwnProperty(const JSRegex &regex, const std::string &name);

void setOwnProperty(JSRegex &regex, const std::string &name, JSValue &&value);

/**
 *
 * @param regex
 * @param str
 * @return
 * if matched, return the match result
 * if not matched, return null
 * if `str` has lone-surrogate, return none
 */
std::optional<JSArrayPtr> execJSRegex(JSRegex &regex, const JSStringPtr &str);

} // namespace arsh::re262

#endif // ARSH_TOOLS_TEST262_JS_REGEX_H
