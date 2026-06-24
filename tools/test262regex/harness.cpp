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

#include "harness.h"
#include "js.h"

namespace arsh::re262 {

constexpr const char *TEST262_ERROR = "Test262Error";

static auto throwTest262Error(const std::shared_ptr<JSEnv> &global, const std::string &message) {
  unsigned int lineNum = 0;
  if (auto v = global->findOrUndef(JSEnv::CALLER_LINENO); std::holds_alternative<double>(v)) {
    lineNum = static_cast<unsigned int>(std::get<double>(v));
  }
  return throwError(global, TEST262_ERROR, lineNum, message);
}

static void defineDoNotEvaluate(const std::shared_ptr<JSEnv> &global) {
  const char *name = "$DONOTEVALUATE";
  auto func = createJSFunction(
      global, name, {}, nullptr,
      [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> Result<JSValue, JSThrown> {
        return throwTest262Error(env, "Test262: This statement should not be evaluate");
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
    std::string str;
    if (!isUndefined(message)) {
      toString(message, str);
      str += ' ';
    }
    str += "Expected SameValue(«";
    toPrettyString(actual, str);
    str += "», «";
    toPrettyString(expected, str);
    str += "») to be ";
    str += same ? "true" : "false";
    return throwTest262Error(env, str);
  };
  return createJSFunction(global, same ? "sameValue" : "notSameValue",
                          {"actual", "expected", "message"}, nullptr, std::move(impl));
}

static void defineAssert(const std::shared_ptr<JSEnv> &global) {
  auto func = createJSFunction(
      global, "assert", {"mustBeTrue", "message"}, nullptr,
      [](const JSFunctionPtr &func,
         const std::shared_ptr<JSEnv> &env) -> Result<JSValue, JSThrown> {
        auto mustBeTrue = env->findOrUndef(func->params[0]);
        if (std::holds_alternative<bool>(mustBeTrue) && std::get<bool>(mustBeTrue)) {
          return Ok(JSValue());
        }
        std::string str;
        if (auto message = env->findOrUndef(func->params[1]); isUndefined(message)) {
          str = "Expected true but got ";
          toPrettyString(mustBeTrue, str);
        } else {
          str = toString(message);
        }
        return throwTest262Error(env, str);
      });
  func->values["sameValue"] = createSameValue(global, true);
  func->values["notSameValue"] = createSameValue(global, false);
  global->define("assert", std::move(func));
}

void includeHarness(const std::shared_ptr<JSEnv> &global) {
  defineDerivedError(global, TEST262_ERROR);
  defineDoNotEvaluate(global);
  defineAssert(global);
}

} // namespace arsh::re262