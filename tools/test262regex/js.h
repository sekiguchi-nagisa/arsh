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
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <misc/result.hpp>
#include <misc/string_ref.hpp>
#include <regex/regex.h>

namespace arsh::re262 {

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

using JSStringPtr = std::shared_ptr<std::u16string>;

struct JSRegex;
using JSRegexPtr = std::shared_ptr<JSRegex>;

struct JSFunction;
using JSFunctionPtr = std::shared_ptr<JSFunction>;

struct JSArray;
using JSArrayPtr = std::shared_ptr<JSArray>;

struct JSObject;
using JSObjectPtr = std::shared_ptr<JSObject>;

struct JSValue : std::variant<std::monostate, std::nullptr_t, bool, double, JSStringPtr, JSRegexPtr,
                              JSFunctionPtr, JSArrayPtr, JSObjectPtr> {
  using variant::variant;
};

inline bool isUndefined(const JSValue &value) {
  return std::holds_alternative<std::monostate>(value);
}

struct JSThrown {
  JSValue value;
};

class JSEnv;

struct JSObject {
  std::map<std::string, JSValue> values;
};

inline JSValue getOwnProperty(const JSObject &obj, const std::string &name) {
  if (auto iter = obj.values.find(name); iter != obj.values.end()) {
    return iter->second;
  }
  return {};
}

#define EACH_JS_EXTRA_RE_FLAG(E)                                                                   \
  E(HAS_INDICES, (1u << 0u), 'd') /* d */                                                          \
  E(GLOBAL, (1u << 1u), 'g')      /* g */                                                          \
  E(STICKY, (1u << 2u), 'y')      /* y */

struct JSRegex {
  enum class ExtraFlag : unsigned char {
    NONE = 0u,
#define GEN_ENUM(E, D, S) E = (D),
    EACH_JS_EXTRA_RE_FLAG(GEN_ENUM)
#undef GEN_ENUM
  };

  JSObjectPtr proto; // __PROTO__
  std::string pattern;
  regex::Regex regex;
  ExtraFlag extra;
  int lastIndex{0}; // utf16 offset

  JSRegex(JSObjectPtr proto, std::string pattern, regex::Regex regex, ExtraFlag extra)
      : proto(std::move(proto)), pattern(std::move(pattern)), regex(std::move(regex)),
        extra(extra) {}
};

struct JSFunction : JSObject {
  std::vector<std::string> params;
  std::weak_ptr<JSEnv> definedEnv;

  using Impl = std::function<Result<JSValue, JSThrown>(const JSFunctionPtr &,
                                                       const std::shared_ptr<JSEnv> &)>;
  Impl impl;
};

/**
 *
 * @param env
 * @param name
 * @param params
 * @param prototype if constructor, not null
 * @param impl
 * @return
 */
JSFunctionPtr createJSFunction(const std::shared_ptr<JSEnv> &env, const char *name,
                               std::vector<std::string> &&params, JSObjectPtr &&prototype,
                               JSFunction::Impl &&impl);

struct JSArray {
  std::vector<JSValue> values;
};

class JSEnv : public std::enable_shared_from_this<JSEnv> {
private:
  std::shared_ptr<JSEnv> parent;
  std::map<std::string, JSValue> values;

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

std::u16string toUTF16(StringRef ref);

inline JSStringPtr newJSString(StringRef ref) {
  return std::make_shared<std::u16string>(toUTF16(ref));
}

void toWTF8(const std::u16string &value, std::string &out);

inline std::string toWTF8(const std::u16string &value) {
  std::string out;
  toWTF8(value, out);
  return out;
}

void toPrettyString(const JSValue &value, std::string &out);

inline std::string toPrettyString(const JSValue &value) {
  std::string out;
  toPrettyString(value, out);
  return out;
}

void toString(const JSValue &value, std::string &out);

inline std::string toString(const JSValue &value) {
  std::string out;
  toString(value, out);
  return out;
}

ErrHolder<JSThrown> throwSyntaxError(const std::shared_ptr<JSEnv> &env, const char *sourceName,
                                     unsigned int lineNum, const std::string &message);

ErrHolder<JSThrown> throwTypeError(const std::shared_ptr<JSEnv> &env, const std::string &message);

std::shared_ptr<JSEnv> initJSEnv();

Result<JSValue, JSThrown> jsEval(const char *sourceName, StringRef source,
                                 std::shared_ptr<JSEnv> global = nullptr, bool debug = false);

} // namespace arsh::re262

namespace arsh {
template <>
struct allow_enum_bitop<re262::JSRegex::ExtraFlag> : std::true_type {};
} // namespace arsh

#endif // ARSH_TOOLS_TEST262_REGEX_JS_H
