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

#ifndef ARSH_TOOLS_TEST262_REGEX_JS_H
#define ARSH_TOOLS_TEST262_REGEX_JS_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <misc/result.hpp>
#include <misc/string_ref.hpp>
#include <regex/regex.h>

namespace arsh::re262 {

using JSStringPtr = std::shared_ptr<std::u16string>;

struct JSRegex;
using JSRegexPtr = std::shared_ptr<JSRegex>;

struct JSFunction;
using JSFunctionPtr = std::shared_ptr<JSFunction>;

struct JSArray;
using JSArrayPtr = std::shared_ptr<JSArray>;

struct JSObject;
using JSObjectPtr = std::shared_ptr<JSObject>;

using JSValue = std::variant<std::monostate, std::nullptr_t, bool, double, JSStringPtr, JSRegexPtr,
                             JSFunctionPtr, JSArrayPtr, JSObjectPtr>;

inline bool isUndefined(const JSValue &value) {
  return std::holds_alternative<std::monostate>(value);
}

struct JSThrown {
  JSValue value;
};

class JSEnv;

struct JSObject {
  std::unordered_map<std::string, JSValue> values;
};

#define EACH_JS_EXTRA_RE_FLAG(E)                                                                   \
  E(HAS_INDICES, (1u << 0u))                                                                       \
  E(GLOBAL, (1u << 1u))                                                                            \
  E(STICKY, (1u << 2u))

struct JSRegex : JSObject {
  enum class ExtraFlag : unsigned char {
    NONE = 0u,
#define GEN_ENUM(E, D) E = (D),
    EACH_JS_EXTRA_RE_FLAG(GEN_ENUM)
#undef GEN_ENUM
  };

  regex::Regex regex;
  ExtraFlag extra;
};

struct JSFunction : JSObject {
  std::vector<std::string> params;
  std::weak_ptr<JSEnv> definedEnv;
  std::function<Result<JSValue, JSThrown>(const JSFunctionPtr &, const std::shared_ptr<JSEnv> &)>
      impl;
};

struct JSArray {
  std::vector<JSValue> values;
};

namespace builtin {

// for builtin constructor
constexpr const char *SYNTAX_ERROR = "SyntaxError";
constexpr const char *REF_ERROR = "ReferenceError";
constexpr const char *TYPE_ERROR = "TypeError";
constexpr const char *REGEXP = "RegExp";

// for builtin field
constexpr const char *THIS = "this";
constexpr const char *PROTOTYPE = "prototype";
constexpr const char *PROTO = "__proto__";

} // namespace builtin

class JSEnv : public std::enable_shared_from_this<JSEnv> {
private:
  std::shared_ptr<JSEnv> parent;
  std::unordered_map<std::string, JSValue> values;

  explicit JSEnv(std::shared_ptr<JSEnv> parent) : parent(std::move(parent)) {}

public:
  static std::shared_ptr<JSEnv> createGlobal() {
    return std::shared_ptr<JSEnv>(new JSEnv(nullptr));
  }

  std::shared_ptr<JSEnv> createChild() {
    return std::shared_ptr<JSEnv>(new JSEnv(this->shared_from_this()));
  }

  const auto &getParent() const { return this->parent; }

  void define(const std::string &name, JSValue value);

  const JSValue *find(const std::string &name) const;

  JSValue findOrUndef(const std::string &name) const {
    if (auto *ret = this->find(name)) {
      return *ret;
    }
    return {};
  }

  const JSValue *assign(const std::string &name, JSValue value);

  std::shared_ptr<JSEnv> findGlobalEnv() {
    auto tmp = this->shared_from_this();
    while (tmp->getParent()) {
      tmp = tmp->getParent();
    }
    return tmp;
  }
};

std::shared_ptr<JSEnv> initJSEnv();

Result<JSValue, JSThrown> jsEval(const char *sourceName, StringRef source,
                                 std::shared_ptr<JSEnv> global = nullptr, bool debug = false);

} // namespace arsh::re262

namespace arsh {
template <>
struct allow_enum_bitop<re262::JSRegex::ExtraFlag> : std::true_type {};
} // namespace arsh

#endif // ARSH_TOOLS_TEST262_REGEX_JS_H
