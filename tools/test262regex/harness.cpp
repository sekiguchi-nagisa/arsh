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

#include "js.h"
#include "misc/unicode.hpp"

namespace arsh::re262 {

constexpr const char *TEST262_ERROR = "Test262Error";

static auto throwTest262Error(const std::shared_ptr<JSEnv> &global, JSString &&message) {
  return throwError(global, TEST262_ERROR, std::move(message));
}

static void defineDoNotEvaluate(const std::shared_ptr<JSEnv> &global) {
  const char *name = "$DONOTEVALUATE";
  auto func = createJSFunction(
      global, name, {}, nullptr,
      [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> Result<JSValue, JSThrown> {
        return throwTest262Error(env, u"Test262: This statement should not be evaluate");
      });
  global->define(name, std::move(func));
}

static bool isSameValueImpl(const JSValue &x, const JSValue &y) {
  if (strictlyEquals(x, y)) {
    if (std::holds_alternative<double>(x) && std::get<double>(x) == 0.0) {
      return 1.0 / std::get<double>(x) == 1.0 / std::get<double>(y); // +/-0 vs -/+0
    }
    return true;
  }
  return !strictlyEquals(x, x) && !strictlyEquals(y, y); // NaN vs NaN
}

static JSFunctionPtr createSameValue(const std::shared_ptr<JSEnv> &global, bool same) {
  auto impl = [same](const JSFunctionPtr &func,
                     const std::shared_ptr<JSEnv> &env) -> Result<JSValue, JSThrown> {
    auto actual = env->findOrUndef(func->params[0]);
    auto expected = env->findOrUndef(func->params[1]);
    auto message = env->findOrUndef(func->params[2]);
    if (isSameValueImpl(actual, expected) == same) {
      return Ok(JSValue());
    }
    JSString str;
    if (!isUndefined(message)) {
      toString(message, str);
      str += u' ';
    }
    str += u"Expected SameValue(«";
    toPrettyString(actual, str);
    str += u"», «";
    toPrettyString(expected, str);
    str += u"») to be ";
    str += same ? u"true" : u"false";
    return throwTest262Error(env, std::move(str));
  };
  return createJSFunction(global, same ? "sameValue" : "notSameValue",
                          {"actual", "expected", "message"}, nullptr, std::move(impl));
}

static Result<JSValue, JSThrown> assertImpl(const std::shared_ptr<JSEnv> &env,
                                            const JSValue &mustBeTrue, const JSValue &message) {
  if (std::holds_alternative<bool>(mustBeTrue) && std::get<bool>(mustBeTrue)) {
    return Ok(JSValue());
  }
  JSString str;
  if (isUndefined(message)) {
    str = u"Expected true but got ";
    toPrettyString(mustBeTrue, str);
  } else {
    str = toString(message);
  }
  return throwTest262Error(env, std::move(str));
}

static void defineAssert(const std::shared_ptr<JSEnv> &global) {
  auto func = createJSFunction(global, "assert", {"mustBeTrue", "message"}, nullptr,
                               [](const JSFunctionPtr &func,
                                  const std::shared_ptr<JSEnv> &env) -> Result<JSValue, JSThrown> {
                                 auto mustBeTrue = env->findOrUndef(func->params[0]);
                                 auto message = env->findOrUndef(func->params[1]);
                                 return assertImpl(env, mustBeTrue, message);
                               });
  func->values["sameValue"] = createSameValue(global, true);
  func->values["notSameValue"] = createSameValue(global, false);
  global->define("assert", std::move(func));
}

#define TRY(...)                                                                                   \
  ({                                                                                               \
    auto v__ = (__VA_ARGS__);                                                                      \
    if (!v__) {                                                                                    \
      return v__;                                                                                  \
    }                                                                                              \
    std::move(v__.asOk());                                                                         \
  })

static Result<JSValue, JSThrown> toCodePoint(const std::shared_ptr<JSEnv> &env,
                                             const JSValue &value) {
  auto d = toNumber(value);
  if (std::isnan(d) || d < 0 || d > UnicodeUtil::CODE_POINT_MAX) {
    JSString str = u"Invalid code point ";
    toUTF16(std::to_string(d), str);
    return throwError(env, builtin::RANGE_ERROR, std::move(str));
  }
  return Ok(d);
}

static void defineBuildString(const std::shared_ptr<JSEnv> &global) {
  const char *name = "buildString";
  auto impl = [](const JSFunctionPtr &func,
                 const std::shared_ptr<JSEnv> &env) -> Result<JSValue, JSThrown> {
    auto args = env->findOrUndef(func->params[0]);
    std::u16string out;
    // loneCodePoints
    if (auto v = TRY(findProperty(env, args, "loneCodePoints"));
        std::holds_alternative<JSArrayPtr>(v)) {
      for (auto &e : std::get<JSArrayPtr>(v)->values) {
        int codePoint = static_cast<int>(std::get<double>(TRY(toCodePoint(env, e))));
        auto [high, low] = UnicodeUtil::codePointToUtf16(codePoint);
        out += high;
        if (high != low) {
          out += low;
        }
      }
    } else {
      return throwError(env, builtin::TYPE_ERROR, u"loneCodePoints must be Array");
    }
    // ranges
    if (auto v = TRY(findProperty(env, args, "ranges")); std::holds_alternative<JSArrayPtr>(v)) {
      for (auto &e : std::get<JSArrayPtr>(v)->values) {
        if (!std::holds_alternative<JSArrayPtr>(e)) {
          return throwError(env, builtin::TYPE_ERROR, u"ranges must be Array");
        }
        auto &range = std::get<JSArrayPtr>(e)->values;
        const auto first = static_cast<int>(std::get<double>(TRY(toCodePoint(env, range.at(0)))));
        const auto last = static_cast<int>(std::get<double>(TRY(toCodePoint(env, range.at(1)))));
        for (int codePoint = first; codePoint <= last; codePoint++) {
          auto [high, low] = UnicodeUtil::codePointToUtf16(codePoint);
          out += high;
          if (high != low) {
            out += low;
          }
        }
      }
    } else {
      return throwError(env, builtin::TYPE_ERROR, u"ranges must be Array");
    }
    return Ok(std::make_shared<std::u16string>(std::move(out)));
  };
  auto func = createJSFunction(global, name, {"args"}, nullptr, std::move(impl));
  global->define(name, std::move(func));
}

static std::vector<int> intoCodePoints(const std::u16string &value) {
  std::vector<int> ret;
  for (size_t i = 0; i < value.size(); i++) {
    int codePoint = value[i];
    if (UnicodeUtil::isHighSurrogate(codePoint) && i + 1 < value.size() &&
        UnicodeUtil::isLowSurrogate(value[i + 1])) {
      codePoint = UnicodeUtil::utf16ToCodePoint(codePoint, value[i + 1]);
      i++;
    }
    ret.push_back(codePoint);
  }
  return ret;
}

static Result<JSValue, JSThrown> assertRegExpTest(const std::shared_ptr<JSEnv> &env,
                                                  const JSValue &regExp, const JSValue &expression,
                                                  const JSValue &string, const bool shouldMatch) {
  auto func = TRY(findProperty(env, regExp, "test"));
  assert(std::holds_alternative<JSFunctionPtr>(func));
  auto ret = TRY(callJSFunction(env, env->callerLineNum(), std::get<JSFunctionPtr>(func),
                                JSValue(regExp), {string}));
  assert(std::holds_alternative<bool>(ret));
  if (std::get<bool>(ret) != shouldMatch) {
    JSString out = u"`";
    toString(expression, out);
    out += u"` should ";
    out += shouldMatch ? u"" : u"not ";
    out += u"match ";
    toString(string, out);
    out += u" (";
    toPrettyString(string, out, true);
    out += u")";
    return assertImpl(env, false, std::make_shared<JSString>(out));
  }
  return Ok(nullptr);
}

static void defineTestPropertyEscapes(const std::shared_ptr<JSEnv> &global) {
  const char *name = "testPropertyEscapes";
  auto impl = [](const JSFunctionPtr &func,
                 const std::shared_ptr<JSEnv> &env) -> Result<JSValue, JSThrown> {
    auto regExp = env->findOrUndef(func->params[0]);
    auto string = env->findOrUndef(func->params[1]);
    auto expression = env->findOrUndef(func->params[2]);
    if (!std::holds_alternative<JSStringPtr>(string)) {
      return throwError(env, builtin::TYPE_ERROR, u"must be String");
    }
    auto codes = intoCodePoints(*std::get<JSStringPtr>(string));
    for (auto &code : codes) {
      std::u16string str;
      auto [high, low] = UnicodeUtil::codePointToUtf16(code);
      str += high;
      if (high != low) {
        str += low;
      }
      TRY(assertRegExpTest(env, regExp, expression,
                           std::make_shared<std::u16string>(std::move(str)), true));
    }
    return Ok(JSValue());
  };
  auto func =
      createJSFunction(global, name, {"regExp", "string", "expression"}, nullptr, std::move(impl));
  global->define(name, std::move(func));
}

static JSStringPtr joinAsString(const JSArrayPtr &array) {
  JSString ret;
  for (auto &e : array->values) {
    JSStringPtr str;
    if (std::holds_alternative<JSStringPtr>(e)) {
      str = std::get<JSStringPtr>(e);
    } else {
      str = std::make_shared<JSString>(toString(e));
    }
    ret += *str;
  }
  return std::make_shared<JSString>(std::move(ret));
}

static void defineTestPropertyOfStrings(const std::shared_ptr<JSEnv> &global) {
  const char *name = "testPropertyOfStrings";
  auto impl = [](const JSFunctionPtr &func,
                 const std::shared_ptr<JSEnv> &env) -> Result<JSValue, JSThrown> {
    auto args = env->findOrUndef(func->params[0]);
    auto regExp = TRY(findProperty(env, args, "regExp"));
    auto expression = TRY(findProperty(env, args, "expression"));
    auto matchStrings = TRY(findProperty(env, args, "matchStrings"));
    auto nonMatchStrings = TRY(findProperty(env, args, "nonMatchStrings"));
    // check match strings
    if (!std::holds_alternative<JSArrayPtr>(matchStrings)) {
      return throwError(env, builtin::TYPE_ERROR, u"matchStrings must be Array");
    }
    if (auto allMatchStrings = joinAsString(std::get<JSArrayPtr>(matchStrings));
        !assertRegExpTest(env, regExp, expression, allMatchStrings, true)) {
      for (auto &string : std::get<JSArrayPtr>(matchStrings)->values) {
        TRY(assertRegExpTest(env, regExp, expression, string, true));
      }
    }
    // check non-match strings
    if (!std::holds_alternative<JSArrayPtr>(nonMatchStrings)) {
      return throwError(env, builtin::TYPE_ERROR, u"nonMatchStrings must be Array");
    }
    if (auto allNonMatchStrings = joinAsString(std::get<JSArrayPtr>(nonMatchStrings));
        !allNonMatchStrings->empty() &&
        !assertRegExpTest(env, regExp, expression, allNonMatchStrings, false)) {
      for (auto &string : std::get<JSArrayPtr>(nonMatchStrings)->values) {
        TRY(assertRegExpTest(env, regExp, expression, string, false));
      }
    }
    return Ok(JSValue());
  };
  auto func = createJSFunction(global, name, {"args"}, nullptr, std::move(impl));
  global->define(name, func);
  global->define("testExtendedCharacterClass", std::move(func));
}

void includeHarness(const std::shared_ptr<JSEnv> &global) {
  defineDerivedError(global, TEST262_ERROR);
  defineDoNotEvaluate(global);
  defineAssert(global);
  defineBuildString(global);
  defineTestPropertyEscapes(global);
  defineTestPropertyOfStrings(global);
}

} // namespace arsh::re262