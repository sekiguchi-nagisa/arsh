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

#include <misc/unicode.hpp>
#include <regex/emit.h>
#include <regex/parser.h>

namespace arsh::re262 {

#define TRY(...)                                                                                   \
  ({                                                                                               \
    auto v__ = (__VA_ARGS__);                                                                      \
    if (!v__) {                                                                                    \
      return v__;                                                                                  \
    }                                                                                              \
    std::move(v__.value);                                                                          \
  })

static JSFunctionPtr createRegExpExec(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> JSResult {
    JSRegexPtr regex;
    if (auto v = env->findOrUndef(builtin::THIS); std::holds_alternative<JSRegexPtr>(v)) {
      regex = std::get<JSRegexPtr>(v);
    } else {
      return throwError(env, builtin::TYPE_ERROR,
                        u"Method RegExp.prototype.exec called on incompatible receiver");
    }
    JSStringPtr str;
    if (auto v = env->findOrUndef("str"); std::holds_alternative<JSStringPtr>(v)) {
      str = std::get<JSStringPtr>(v);
    } else {
      str = std::make_shared<JSString>(toString(v));
    }
    assert(regex);
    return execJSRegex(env, *regex, str);
  };
  return createJSFunction(global, "exec", {"str"}, nullptr, std::move(impl));
}

static JSFunctionPtr createRegExpTest(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> JSResult {
    JSRegexPtr regex;
    if (auto v = env->findOrUndef(builtin::THIS); std::holds_alternative<JSRegexPtr>(v)) {
      regex = std::get<JSRegexPtr>(v);
    } else {
      return throwError(env, builtin::TYPE_ERROR,
                        u"Method RegExp.prototype.test called on incompatible receiver");
    }
    JSStringPtr str;
    if (auto v = env->findOrUndef("str"); std::holds_alternative<JSStringPtr>(v)) {
      str = std::get<JSStringPtr>(v);
    } else {
      str = std::make_shared<JSString>(toString(v));
    }
    assert(regex);
    auto ret = TRY(execJSRegex(env, *regex, str));
    return Ok(std::holds_alternative<JSArrayPtr>(ret));
  };
  return createJSFunction(global, "test", {"str"}, nullptr, std::move(impl));
}

static int nextUTF16Index(const JSString &str, int index, const bool unicode) {
  assert(index > -1);
  index++;
  if (unicode && static_cast<unsigned int>(index) < str.size() &&
      UnicodeUtil::isHighSurrogate(str[index - 1]) && UnicodeUtil::isLowSurrogate(str[index])) {
    index++;
  }
  return index;
}

static JSFunctionPtr createRegExpMatch(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> JSResult {
    JSRegexPtr regex;
    if (auto v = env->findOrUndef(builtin::THIS); std::holds_alternative<JSRegexPtr>(v)) {
      regex = std::get<JSRegexPtr>(v);
    } else {
      return throwError(env, builtin::TYPE_ERROR,
                        u"Method RegExp.prototype[Symbol.match] called on incompatible receiver");
    }
    JSStringPtr str;
    if (auto v = env->findOrUndef("str"); std::holds_alternative<JSStringPtr>(v)) {
      str = std::get<JSStringPtr>(v);
    } else {
      str = std::make_shared<JSString>(toString(v));
    }
    assert(regex);
    if (hasFlag(regex->extra, JSRegex::ExtraFlag::GLOBAL)) {
      regex->lastIndex = 0;
    }
    auto ret = TRY(execJSRegex(env, *regex, str));
    if (isNull(ret) || !hasFlag(regex->extra, JSRegex::ExtraFlag::GLOBAL)) {
      return Ok(std::move(ret));
    }

    // for global
    auto array = std::get<JSArrayPtr>(ret);
    array->array.resize(1); // only maintain first element
    array->values.clear();
    int lastIndex = 0;
    while (true) {
      if (lastIndex == regex->lastIndex) {
        regex->lastIndex =
            nextUTF16Index(*str, regex->lastIndex, regex->regex.getFlag().isEitherUnicodeMode());
      }
      lastIndex = regex->lastIndex;
      ret = TRY(execJSRegex(env, *regex, str));
      if (isNull(ret)) {
        break;
      }
      array->array.push_back(std::get<JSArrayPtr>(ret)->array[0]);
    }
    return Ok(std::move(array));
  };
  return createJSFunction(global, builtin::SYMBOL_MATCH, {"str"}, nullptr, std::move(impl));
}

static JSFunctionPtr createRegExpEscape(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    std::string str;
    if (auto v = env->findOrUndef(func->params[0]); std::holds_alternative<JSStringPtr>(v)) {
      str = toWTF8(*std::get<JSStringPtr>(v));
    } else {
      return throwError(env, builtin::TYPE_ERROR, u"input argument must be string");
    }
    std::string out;
    bool r = regex::escape(str, out.max_size(), out);
    static_cast<void>(r);
    assert(r);
    return Ok(std::make_shared<JSString>(toUTF16(out)));
  };
  return createJSFunction(global, "escape", {"string"}, nullptr, std::move(impl));
}

void defineJSRegex(const std::shared_ptr<JSEnv> &global) {
  auto prototype = std::make_shared<JSObject>();
  prototype->values["test"] = createRegExpTest(global);
  prototype->values["exec"] = createRegExpExec(global);
  prototype->values[builtin::SYMBOL_MATCH] = createRegExpMatch(global);
  auto func = createJSFunction(
      global, builtin::REGEXP, {"pattern", "flags"}, std::move(prototype),
      [](const JSFunctionPtr &self, const std::shared_ptr<JSEnv> &env) -> JSResult {
        std::string pattern;
        std::string flags;
        if (auto v = env->findOrUndef("pattern"); std::holds_alternative<JSRegexPtr>(v)) {
          pattern = std::get<JSRegexPtr>(v)->pattern;
          flags = toStringFlags(*std::get<JSRegexPtr>(v));
        } else {
          pattern = toWTF8(toString(v));
        }
        flags = toWTF8(toString(env->findOrUndef("flags")));
        std::string err;
        auto prototype = self->values.at(builtin::PROTOTYPE);
        assert(std::holds_alternative<JSObjectPtr>(prototype));
        if (auto obj = createJSRegexFrom(std::get<JSObjectPtr>(prototype), pattern, flags, &err)) {
          env->assign(builtin::THIS, obj);
          return Ok(std::move(obj));
        }
        return throwError(env, builtin::SYNTAX_ERROR, toUTF16(err));
      });
  func->values["escape"] = createRegExpEscape(global);
  global->define(builtin::REGEXP, std::move(func));
}

static std::optional<std::pair<regex::Flag, JSRegex::ExtraFlag>> parseFlags(const StringRef ref) {
  JSRegex::ExtraFlag extra{};
  std::string remain;
  for (char ch : ref) {
    switch (ch) {
#define GEN_CASE(E, D, S)                                                                          \
  case S:                                                                                          \
    if (hasFlag(extra, JSRegex::ExtraFlag::E)) {                                                   \
      return {};                                                                                   \
    }                                                                                              \
    setFlag(extra, JSRegex::ExtraFlag::E);                                                         \
    break;
      EACH_JS_EXTRA_RE_FLAG(GEN_CASE)
#undef GEN_CASE
    default:
      remain += ch;
      break;
    }
  }
  if (auto flag = regex::Flag::parse(remain, regex::Mode::BMP, nullptr); flag.hasValue()) {
    return {{flag.unwrap(), extra}};
  }
  return {};
}

JSRegexPtr createJSRegexFrom(const JSObjectPtr &prototype, StringRef pattern, StringRef flagStr,
                             std::string *err) {
  auto ret = parseFlags(flagStr);
  if (!ret.has_value()) {
    if (err) {
      *err += "invalid regular expression flags";
    }
    return nullptr;
  }
  const auto [flag, extra] = ret.value();
  if (pattern.empty()) {
    pattern = "(?:)";
  }
  regex::Parser parser;
  auto tree = parser(pattern, flag);
  if (parser.hasError()) {
    if (err) {
      *err = parser.getError()->message;
    }
    return nullptr;
  }
  regex::CodeGen codeGen;
  if (auto re = codeGen(std::move(tree)); re.hasValue()) {
    return std::make_shared<JSRegex>(prototype, pattern.toString(), std::move(re.unwrap()), extra);
  }
  if (err) {
    *err = codeGen.getError();
  }
  return nullptr;
}

JSRegexPtr createJSRegexFromLiteral(const JSObjectPtr &prototype, StringRef literal,
                                    std::string *err) {
  if (literal.size() < 2 || literal.front() != '/' || literal.lastIndexOf("/") == 0 ||
      literal.lastIndexOf("/") == StringRef::npos) {
    if (err) {
      *err += "invalid regular expression literal";
    }
    return nullptr;
  }
  auto ret = literal.lastIndexOf("/");
  auto pattern = literal.slice(1, ret);
  auto flags = literal.substr(ret + 1);
  return createJSRegexFrom(prototype, pattern, flags, err);
}

std::string toStringFlags(const JSRegex &regex) {
  auto ret = regex.regex.getFlag().str();
  constexpr struct {
    JSRegex::ExtraFlag flag;
    char ch;
  } table[] = {
#define GEN_TABLE(E, D, S) {JSRegex::ExtraFlag::E, S},
      EACH_JS_EXTRA_RE_FLAG(GEN_TABLE)
#undef GEN_TABLE
  };
  for (auto [ff, ch] : table) {
    if (hasFlag(regex.extra, ff)) {
      ret += ch;
    }
  }
  std::sort(ret.begin(), ret.end());
  return ret;
}

std::string toString(const JSRegex &regex) {
  std::string ret = "/";
  ret += regex.pattern;
  ret += '/';
  ret += toStringFlags(regex);
  return ret;
}

JSValue getOwnProperty(const JSRegex &regex, const std::string &name) {
  if (name == builtin::PROTO) {
    return regex.proto;
  }
  if (name == "lastIndex") {
    const double d = regex.lastIndex;
    return d;
  }
  if (name == "source") {
    return newJSString(regex.pattern);
  }
  if (name == "flags") {
    return newJSString(toStringFlags(regex));
  }
  if (name == "dotAll") {
    return regex.regex.getFlag().has(regex::Modifier::DOT_ALL);
  }
  if (name == "ignoreCase") {
    return regex.regex.getFlag().has(regex::Modifier::IGNORE_CASE);
  }
  if (name == "multiline") {
    return regex.regex.getFlag().has(regex::Modifier::MULTILINE);
  }
  if (name == "global") {
    return hasFlag(regex.extra, JSRegex::ExtraFlag::GLOBAL);
  }
  if (name == "sticky") {
    return hasFlag(regex.extra, JSRegex::ExtraFlag::STICKY);
  }
  if (name == "hasIndices") {
    return hasFlag(regex.extra, JSRegex::ExtraFlag::HAS_INDICES);
  }
  if (name == "unicode") {
    return regex.regex.getFlag().is(regex::Mode::UNICODE);
  }
  if (name == "unicodeSets") {
    return regex.regex.getFlag().is(regex::Mode::UNICODE_SET);
  }
  return getOwnProperty(*regex.proto, name);
}

void setOwnProperty(JSRegex &regex, const std::string &name, JSValue &&value) {
  if (name == "lastIndex" && std::holds_alternative<double>(value)) {
    regex.lastIndex = static_cast<int>(std::get<double>(value));
  }
}

static unsigned int toCodePointOffset(const std::u16string &str, const unsigned int index) {
  unsigned int offset = 0;
  const auto limit = static_cast<unsigned int>(index);
  for (unsigned int i = 0; i < limit; i++) {
    int codePoint = str[i];
    if (UnicodeUtil::isHighSurrogate(codePoint) && i + 1 < limit &&
        UnicodeUtil::isLowSurrogate(str[i + 1])) {
      i++;
    }
    offset++;
  }
  return offset;
}

static unsigned int toUTF16Offset(const StringRef ref, const unsigned int codePointOffset) {
  unsigned int offset = 0;
  const char *end = ref.begin() + codePointOffset;
  for (const char *iter = ref.begin(); iter != end;) {
    int codePoint;
    if (unsigned int len = UnicodeUtil::wtf8ToCodePoint(iter, end, codePoint); len) {
      iter += len;
    } else { // put dummy
      iter++;
      codePoint = UnicodeUtil::REPLACEMENT_CHAR_CODE;
    }
    offset++;
    if (UnicodeUtil::isSupplementaryCodePoint(codePoint)) {
      offset++;
    }
  }
  return offset;
}

template <typename CaptureStore>
static constexpr bool capture_store_requirement_v =
    std::is_same_v<void, std::invoke_result_t<CaptureStore, StringRef, regex::Capture>>;

template <typename Func, enable_when<capture_store_requirement_v<Func>> = nullptr>
static void collectNamedGroups(const regex::Regex &re, const std::vector<regex::Capture> &captures,
                               const Func &func) {
  for (auto &[name, entry] : re.getNamedCaptureGroups().getEntries()) {
    regex::Capture cap;
    if (entry.hasMultipleIndices()) {
      for (unsigned int i = 0; i < entry.getSize(); i++) {
        if (unsigned int capIndex = entry[i]; captures[capIndex]) {
          cap = captures[capIndex];
          break;
        }
      }
    } else {
      cap = captures[entry.getIndex()];
    }
    func(name, cap);
  }
}

static JSValue toNamedGroups(const regex::Regex &re, const StringRef ref,
                             const std::vector<regex::Capture> &captures) {
  if (re.getNamedCaptureGroups().getEntries().empty()) {
    return {};
  }
  auto groups = std::make_shared<JSObject>();
  collectNamedGroups(re, captures, [groups, ref](StringRef name, regex::Capture cap) {
    groups->values[name.toString()] =
        cap ? newJSString(ref.substr(cap.offset, cap.size)) : JSValue();
  });
  return groups;
}

static JSValue toNamedGroupIndices(const regex::Regex &re, const StringRef ref,
                                   const std::vector<regex::Capture> &captures) {
  if (re.getNamedCaptureGroups().getEntries().empty()) {
    return {};
  }
  auto groups = std::make_shared<JSObject>();
  collectNamedGroups(re, captures, [groups, ref](StringRef name, regex::Capture cap) {
    JSValue value;
    if (cap) {
      value = std::make_shared<JSArray>(JSArray{{
          static_cast<double>(toUTF16Offset(ref, cap.offset)),
          static_cast<double>(toUTF16Offset(ref, cap.offset + cap.size)),
      }});
    }
    groups->values[name.toString()] = std::move(value);
  });
  return groups;
}

static JSValue toIndices(const regex::Regex &re, const StringRef ref,
                         const std::vector<regex::Capture> &captures) {
  auto indices = std::make_shared<JSArray>();
  for (unsigned int i = 0; i < captures.size(); i++) {
    auto &cap = captures[i];
    JSValue value;
    if (cap) {
      value = std::make_shared<JSArray>(JSArray{{
          static_cast<double>(toUTF16Offset(ref, cap.offset)),
          static_cast<double>(toUTF16Offset(ref, cap.offset + cap.size)),
      }});
    }
    indices->array.push_back(std::move(value));
  }
  indices->values["groups"] = toNamedGroupIndices(re, ref, captures);
  return indices;
}

static std::pair<JSArrayPtr, unsigned int>
toMatchResult(const JSRegex &regex, const JSStringPtr &str, const StringRef ref,
              const std::vector<regex::Capture> &captures) {
  auto obj = std::make_shared<JSArray>();
  unsigned int lastIndex = toUTF16Offset(ref, captures[0].offset + captures[0].size);
  for (auto &cap : captures) {
    obj->array.push_back(cap ? newJSString(ref.substr(cap.offset, cap.size)) : JSValue());
  }
  obj->values["index"] = static_cast<double>(toUTF16Offset(ref, captures[0].offset));
  obj->values["input"] = str;
  obj->values["groups"] = toNamedGroups(regex.regex, ref, captures);
  if (hasFlag(regex.extra, JSRegex::ExtraFlag::HAS_INDICES)) {
    obj->values["indices"] = toIndices(regex.regex, ref, captures);
  }
  return {obj, lastIndex};
}

std::optional<JSArrayPtr> execJSRegex(JSRegex &regex, const JSStringPtr &str) {
  assert(str);
  unsigned int startOffset = 0;
  if (hasFlag(regex.extra, JSRegex::ExtraFlag::GLOBAL) ||
      hasFlag(regex.extra, JSRegex::ExtraFlag::STICKY)) {
    if (regex.lastIndex < 0) {
      startOffset = 0;
    } else if (static_cast<size_t>(regex.lastIndex) > str->size()) {
      return nullptr;
    } else {
      startOffset = toCodePointOffset(*str, static_cast<unsigned int>(regex.lastIndex));
    }
    regex.lastIndex = 0;
  }
  std::vector<regex::Capture> captures;
  const std::string text = toWTF8(*str);
  auto s = regex::match(regex.regex, text, startOffset, captures, nullptr);
  switch (s) {
  case regex::MatchStatus::OK: {
    auto [ret, lastIndex] = toMatchResult(regex, str, text, captures);
    if (hasFlag(regex.extra, JSRegex::ExtraFlag::GLOBAL) ||
        hasFlag(regex.extra, JSRegex::ExtraFlag::STICKY)) {
      regex.lastIndex = static_cast<int>(lastIndex);
    }
    return ret;
  }
  case regex::MatchStatus::INVALID_UTF8:
  case regex::MatchStatus::INPUT_LIMIT:
    return {};
  default:
    return nullptr;
  }
}

} // namespace arsh::re262