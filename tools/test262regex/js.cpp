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
#include "js_lexer.h"
#include "js_regex.h"

#include <constant.h>
#include <misc/parser_base.hpp>
#include <misc/unicode.hpp>

namespace arsh::re262 {

#define TRY(...)                                                                                   \
  ({                                                                                               \
    auto v__ = (__VA_ARGS__);                                                                      \
    if (!v__) {                                                                                    \
      return v__;                                                                                  \
    }                                                                                              \
    std::move(v__.value);                                                                          \
  })

// ###################
// ##     JSEnv     ##
// ###################

bool JSEnv::define(const std::string &name, JSValue value) {
  return this->values.emplace(name, std::move(value)).second;
}

const JSValue *JSEnv::find(const std::string &name) const {
  for (auto *ptr = this; ptr; ptr = ptr->parent.get()) {
    if (auto iter = ptr->values.find(name); iter != ptr->values.end()) {
      return &iter->second;
    }
  }
  return nullptr;
}

const JSValue *JSEnv::assign(const std::string &name, JSValue value) {
  for (auto *ptr = this; ptr; ptr = ptr->parent.get()) {
    if (auto iter = ptr->values.find(name); iter != ptr->values.end()) {
      iter->second = std::move(value);
      return &iter->second;
    }
  }
  return nullptr;
}

static JSValue getOwnProperty(const JSArray &recv, const std::string &name) {
  if (name == "length") {
    return static_cast<double>(recv.array.size());
  }
  return getOwnProperty(static_cast<JSObject>(recv), name);
}

static JSValue findOwnProperty(const JSValue &recv, const std::string &name) {
  return std::visit(
      [name](auto &&element) -> JSValue {
        using T = std::decay_t<decltype(element)>;
        if constexpr (std::is_same_v<T, JSRegexPtr> || std::is_same_v<T, JSFunctionPtr> ||
                      std::is_same_v<T, JSObjectPtr> || std::is_same_v<T, JSArrayPtr>) {
          return getOwnProperty(*element, name);
        } else {
          return {};
        }
      },
      recv);
}

JSResult findProperty(const std::shared_ptr<JSEnv> &env, unsigned int callerLineNum,
                      const JSValue &recv, const std::string &name) {
  if (isUndefined(recv) || isNull(recv)) {
    JSString message = u"Cannot read properties of ";
    toPrettyString(recv, message);
    message += u" (reading '";
    toUTF16(name, message);
    message += u"')";
    return throwError(env, builtin::TYPE_ERROR, callerLineNum, std::move(message));
  }
  JSValue actualRecv = recv;
  if (std::holds_alternative<JSStringPtr>(recv)) {
    if (name == "length") {
      return Ok(static_cast<double>(std::get<JSStringPtr>(recv)->size()));
    }
    actualRecv = env->findGlobalEnv()->findOrUndef(builtin::STRING);
    actualRecv = getOwnProperty(*std::get<JSFunctionPtr>(actualRecv), builtin::PROTOTYPE);
  } else if (std::holds_alternative<double>(recv)) {
    actualRecv = env->findGlobalEnv()->findOrUndef(builtin::NUMBER);
    actualRecv = getOwnProperty(*std::get<JSFunctionPtr>(actualRecv), builtin::PROTOTYPE);
  }
  JSValue ret;
  const bool proto = name == builtin::PROTO;
  while (!isUndefined(actualRecv)) {
    ret = findOwnProperty(actualRecv, name);
    if (!isUndefined(ret) || proto) {
      break;
    }
    actualRecv = findOwnProperty(actualRecv, builtin::PROTO);
  }
  return Ok(std::move(ret));
}

JSResult assignProperty(const std::shared_ptr<JSEnv> &env, unsigned int callerLineNum,
                        const JSValue &recv, const std::string &name, JSValue &&value) {
  return std::visit(
      [&](auto &&element) -> JSResult {
        using T = std::decay_t<decltype(element)>;
        if constexpr (std::is_same_v<T, JSFunctionPtr> || std::is_same_v<T, JSObjectPtr> ||
                      std::is_same_v<T, JSArrayPtr>) {
          element->values[name] = value;
          return Ok(std::move(value));
        } else if constexpr (std::is_same_v<T, JSRegexPtr>) {
          setOwnProperty(*element, name, JSValue(value));
          return Ok(std::move(value));
        } else {
          JSString str = u"Cannot create property '";
          toUTF16(name, str);
          str += u"' on ";
          toPrettyString(recv, str);
          return throwError(env, builtin::TYPE_ERROR, callerLineNum, std::move(str));
        }
      },
      recv);
}

void toUTF16(StringRef ref, std::u16string &out) {
  const char *end = ref.end();
  for (const char *iter = ref.begin(); iter != end;) {
    int codePoint;
    if (unsigned int len = UnicodeUtil::wtf8ToCodePoint(iter, end, codePoint); len) {
      iter += len;
    } else { // put dummy
      iter++;
      codePoint = UnicodeUtil::REPLACEMENT_CHAR_CODE;
    }
    auto [high, low] = UnicodeUtil::codePointToUtf16(codePoint);
    out += high;
    if (high != low) {
      out += low;
    }
  }
}

static bool isInteger(double d) { return std::isfinite(d) && d == std::trunc(d); }

static bool isSafeInteger(double d) { return isInteger(d) && std::abs(d) <= MAX_SAFE_INTEGER; }

void toWTF8(const std::u16string &value, std::string &out) {
  for (size_t i = 0; i < value.size(); i++) {
    int codePoint = value[i];
    if (UnicodeUtil::isHighSurrogate(codePoint) && i + 1 < value.size() &&
        UnicodeUtil::isLowSurrogate(value[i + 1])) {
      codePoint = UnicodeUtil::utf16ToCodePoint(codePoint, value[i + 1]);
      i++;
    }
    char buf[4];
    if (unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, buf)) {
      out.append(buf, len);
    }
  }
}

static void formatCodePoints(const std::u16string &value, std::u16string &out) {
  for (size_t i = 0; i < value.size(); i++) {
    int codePoint = value[i];
    if (UnicodeUtil::isHighSurrogate(codePoint) && i + 1 < value.size() &&
        UnicodeUtil::isLowSurrogate(value[i + 1])) {
      codePoint = UnicodeUtil::utf16ToCodePoint(codePoint, value[i + 1]);
      i++;
    }
    char buf[16];
    snprintf(buf, std::size(buf), "U+%06X", codePoint);
    toUTF16(buf, out);
  }
}

static void formatInteger(const int64_t value, std::u16string &out, const unsigned char radix) {
  assert(radix >= 2 && radix <= 36);
  if (value < 0) {
    out += u'-';
  }
  uint64_t v;
  if (value < 0) {
    if (value == INT64_MIN) {
      v = static_cast<uint64_t>(INT64_MAX) + 1;
    } else {
      v = -1 * value;
    }
  } else {
    v = static_cast<uint64_t>(value);
  }
  std::u16string tmp;
  do {
    tmp += u"0123456789abcdefghijklmnopqrstuvwxyz"[v % radix];
    v /= radix;
  } while (v);
  std::reverse(tmp.begin(), tmp.end());
  out += tmp;
}

void toPrettyString(const JSValue &value, std::u16string &out, const PrettyStringOp op) {
  if (isUndefined(value)) {
    out += u"undefined";
  } else if (isNull(value)) {
    out += u"null";
  } else if (std::holds_alternative<bool>(value)) {
    out += std::get<bool>(value) ? u"true" : u"false";
  } else if (std::holds_alternative<double>(value)) {
    auto d = std::get<double>(value);
    if (d == 0.0) {
      out += u'0';
    } else if (std::isnan(d)) {
      out += u"NaN";
    } else if (std::isinf(d)) {
      out += std::signbit(d) ? u"-Infinity" : u"Infinity";
    } else if (isSafeInteger(d)) {
      formatInteger(static_cast<int64_t>(d), out, op.radix);
    } else {
      toUTF16(std::to_string(d), out); // TODO: radix
    }
  } else if (std::holds_alternative<JSStringPtr>(value)) {
    if (op.escape) {
      formatCodePoints(*std::get<JSStringPtr>(value), out);
    } else {
      out += *std::get<JSStringPtr>(value);
    }
  } else if (std::holds_alternative<JSRegexPtr>(value)) {
    toUTF16(toString(*std::get<JSRegexPtr>(value)), out);
  } else if (std::holds_alternative<JSFunctionPtr>(value)) {
    out += u"[Function: ";
    out += *std::get<JSStringPtr>(std::get<JSFunctionPtr>(value)->values.at("name"));
    out += u']';
  } else if (std::holds_alternative<JSArrayPtr>(value)) {
    auto &array = std::get<JSArrayPtr>(value);
    out += u'[';
    unsigned int count = 0;
    for (auto &e : array->array) {
      if (count++ > 0) {
        out += u',';
      }
      out += u' ';
      toPrettyString(e, out, op);
    }
    for (auto &[k, v] : array->values) {
      if (k == builtin::PROTO) {
        continue;
      }
      if (count++ > 0) {
        out += u',';
      }
      out += u' ';
      toUTF16(k, out);
      out += u": ";
      toPrettyString(v, out, op);
    }
    if (count) {
      out += u' ';
    }
    out += u']';
  } else if (std::holds_alternative<JSObjectPtr>(value) &&
             !std::get<JSObjectPtr>(value)->values.empty()) {
    auto &obj = std::get<JSObjectPtr>(value);
    out += u'{';
    unsigned int count = 0;
    for (auto &[k, v] : obj->values) {
      if (k == builtin::PROTO) {
        continue;
      }
      if (count++ > 0) {
        out += u',';
      }
      out += u' ';
      toUTF16(k, out);
      out += u": ";
      toPrettyString(v, out, op);
    }
    out += u" }";
  } else {
    out += u"{}";
  }
}

void toString(const JSValue &value, std::u16string &out) {
  if (std::holds_alternative<JSFunctionPtr>(value)) {
    out += u"function ";
    out += *std::get<JSStringPtr>(std::get<JSFunctionPtr>(value)->values.at("name"));
    out += u"() { [native code] }";
  } else if (std::holds_alternative<JSArrayPtr>(value)) {
    auto &array = std::get<JSArrayPtr>(value);
    unsigned int count = 0;
    for (auto &e : array->array) {
      if (count++ > 0) {
        out += ',';
      }
      if (!isNull(e) && !isUndefined(e)) {
        toString(e, out);
      }
    }
  } else if (std::holds_alternative<JSObjectPtr>(value)) {
    out += u"[object Object]";
  } else {
    toPrettyString(value, out);
  }
}

static bool toBool(const JSValue &value) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  }
  if (isUndefined(value)) {
    return false;
  }
  if (std::holds_alternative<double>(value)) {
    auto d = std::get<double>(value);
    if (d == 0 || std::isnan(d)) {
      return false;
    }
    return true;
  }
  if (isNull(value)) {
    return false;
  }
  if (std::holds_alternative<JSStringPtr>(value) && std::get<JSStringPtr>(value)->empty()) {
    return false;
  }
  return true;
}

double toNumber(const JSValue &value) {
  if (std::holds_alternative<double>(value)) {
    return std::get<double>(value);
  }
  if (isUndefined(value)) {
    return std::nan("");
  }
  if (isNull(value) || (std::holds_alternative<bool>(value) && !std::get<bool>(value))) {
    return +0.0;
  }
  if (std::holds_alternative<bool>(value) && std::get<bool>(value)) {
    return 1;
  }
  if (std::holds_alternative<JSStringPtr>(value)) {
    if (auto tmp = toWTF8(*std::get<JSStringPtr>(value)); tmp.empty()) {
      return 0.0;
    } else if (!StringRef(tmp).hasNullChar()) {
      if (auto ret = convertToDouble(tmp.c_str())) {
        if (std::isinf(ret.value)) {
          if (tmp != "Infinity" && tmp != "-Infinity" && tmp != "+Infinity") {
            return std::nan("");
          }
        }
        return ret.value;
      }
    }
  }
  return std::nan("");
}

static double toIntegerOrInf(const JSValue &value) {
  auto num = toNumber(value);
  if (num == 0.0 || std::isnan(num)) {
    return 0;
  }
  if (std::isinf(num)) {
    return num;
  }
  return std::trunc(num);
}

template <typename T, enable_when<std::is_unsigned_v<T> && sizeof(T) < sizeof(uint64_t)> = nullptr>
static T toFixedSizeInteger(const JSValue &value) {
  double num = toIntegerOrInf(value);
  if (std::isinf(num)) {
    return 0;
  }
  auto v = static_cast<int64_t>(num);
  return static_cast<T>(v % (static_cast<int64_t>(std::numeric_limits<T>::max()) + 1));
}

JSResult callJSFunction(const std::shared_ptr<JSEnv> &caller, unsigned int callerLineNum,
                        const JSFunctionPtr &func, JSValue &&recv, std::vector<JSValue> &&args) {
  auto funcEnv = func->definedEnv.lock()->createFunc();
  assert(funcEnv);
  funcEnv->define(builtin::THIS, std::move(recv));
  funcEnv->define(JSEnv::CALLER_FILENAME, caller->findOrUndef(JSEnv::DEFINED_FILENAME));
  funcEnv->define(JSEnv::CALLER_LINENO, static_cast<double>(callerLineNum));
  const size_t maxArgs = std::max(func->params.size(), args.size());
  for (size_t i = 0; i < maxArgs; i++) {
    if (i < func->params.size() && i < args.size()) {
      funcEnv->define(func->params[i], args[i]);
    }
  }
  funcEnv->define(builtin::ARGS, std::make_shared<JSArray>(std::move(args)));
  return func->impl(func, funcEnv);
}

JSResult throwError(const std::shared_ptr<JSEnv> &env, const char *name, unsigned int lineNum,
                    JSString &&message) {
  auto v = env->findGlobalEnv()->findOrUndef(name);
  assert(std::holds_alternative<JSFunctionPtr>(v));
  auto func = std::get<JSFunctionPtr>(v);
  std::vector<JSValue> args;
  args.emplace_back(std::make_shared<JSString>(std::move(message)));
  if (auto fileName = env->findOrUndef(JSEnv::DEFINED_FILENAME); !isUndefined(fileName)) {
    args.emplace_back(fileName);
    if (lineNum) {
      args.emplace_back(static_cast<double>(lineNum));
    }
  }
  return Err(callJSFunction(env, lineNum, func, JSValue(), std::move(args)));
}

bool strictlyEquals(const JSValue &x, const JSValue &y) {
  if (x.index() != y.index()) {
    return false;
  }
  if (std::holds_alternative<double>(x)) {
    auto xv = std::get<double>(x);
    auto yv = std::get<double>(y);
    return xv == yv;
  }
  if (isUndefined(x) || isNull(x)) {
    return true;
  }
  if (std::holds_alternative<JSStringPtr>(x)) {
    auto &xv = *std::get<JSStringPtr>(x);
    auto &yv = *std::get<JSStringPtr>(y);
    return xv == yv;
  }
  if (std::holds_alternative<bool>(x)) {
    auto xv = std::get<bool>(x);
    auto yv = std::get<bool>(y);
    return xv == yv;
  }
  return x == y;
}

JSResult isInstanceOf(const std::shared_ptr<JSEnv> &env, unsigned int lineNum, const JSValue &value,
                      const JSValue &constructor) {
  if (!std::holds_alternative<JSFunctionPtr>(constructor)) {
    return throwError(env, builtin::TYPE_ERROR, lineNum,
                      u"Right-hand side of instanceof is not callable");
  }
  if (isUndefined(value) || isNull(value)) {
    return Ok(false);
  }

  const auto prototype = findProperty(env, lineNum, constructor, builtin::PROTOTYPE);
  if (!prototype || isUndefined(prototype.value) || isNull(prototype.value)) {
    return Ok(false);
  }
  for (auto target = value;;) {
    auto proto = findProperty(env, lineNum, target, builtin::PROTO);
    if (!proto || isUndefined(proto.value) || isNull(proto.value)) {
      return Ok(false);
    }
    if (strictlyEquals(proto.value, prototype.value)) {
      break;
    }
    target = std::move(proto.value);
  }
  return Ok(true);
}

const char16_t *typeOf(const JSValue &value) {
  if (isUndefined(value)) {
    return u"undefined";
  }
  if (isNull(value)) {
    return u"object";
  }
  if (std::holds_alternative<bool>(value)) {
    return u"boolean";
  }
  if (std::holds_alternative<double>(value)) {
    return u"number";
  }
  if (std::holds_alternative<JSStringPtr>(value)) {
    return u"string";
  }
  if (std::holds_alternative<JSFunctionPtr>(value)) {
    return u"function";
  }
  return u"object";
}

static double compare(const JSValue &left, const JSValue &right) {
  if (std::holds_alternative<JSStringPtr>(left) && std::holds_alternative<JSStringPtr>(right)) {
    auto &left0 = *std::get<JSStringPtr>(left);
    auto &right0 = *std::get<JSStringPtr>(right);
    return left0.compare(right0);
  }
  double left0 = toNumber(left);
  double right0 = toNumber(right);
  if (std::isnan(left0) || std::isnan(right0)) {
    return std::nan("");
  }
  if (left0 < right0) {
    return -1;
  }
  if (left0 > right0) {
    return 1;
  }
  return 0;
}

static double jsRemainder(const double left, const double right) {
  if (std::isnan(left) || std::isnan(right) || std::isinf(left) || right == 0.0) {
    return std::nan("");
  }
  if (std::isinf(right) || left == 0.0) {
    return left;
  }
  return std::fmod(left, right);
}

// for builtin
JSFunctionPtr createJSFunction(const std::shared_ptr<JSEnv> &env, const char *name,
                               std::vector<std::string> &&params, JSObjectPtr &&prototype,
                               JSFunction::Impl &&impl) {
  auto func = std::make_shared<JSFunction>();
  func->params = std::move(params);
  func->definedEnv = env;
  func->values["name"] = newJSStringPtr(name);
  if (prototype) {
    func->values[builtin::PROTOTYPE] = std::move(prototype);
  }
  func->impl = std::move(impl);
  return func;
}

static JSObjectPtr newObject(const JSFunctionPtr &func) {
  auto obj = std::make_shared<JSObject>();
  if (auto prototype = getOwnProperty(*func, builtin::PROTOTYPE); !isUndefined(prototype)) {
    obj->values[builtin::PROTO] = std::move(prototype);
  }
  return obj;
}

static JSResult errorConstructorImpl(const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) {
  JSObjectPtr obj;
  if (auto v = env->findOrUndef(builtin::THIS); std::holds_alternative<JSObjectPtr>(v)) {
    obj = std::get<JSObjectPtr>(v);
  } else {
    obj = newObject(func);
  }
  env->assign(builtin::THIS, obj);
  assert(func->params.size() == 3);
  // message
  auto v = env->findOrUndef(func->params[0]);
  if (isUndefined(v)) {
    v = newJSStringPtr("");
  }
  obj->values[func->params[0]] = v;

  // fileName
  v = env->findOrUndef(func->params[1]);
  if (isUndefined(v)) {
    v = env->findOrUndef(JSEnv::CALLER_FILENAME);
  }
  obj->values[func->params[1]] = v;

  // lineNumber
  v = env->findOrUndef(func->params[2]);
  if (isUndefined(v)) {
    v = env->findOrUndef(JSEnv::CALLER_LINENO);
  }
  obj->values[func->params[2]] = v;
  return Ok(obj);
}

static void defineError(const std::shared_ptr<JSEnv> &global) {
  auto prototype = std::make_shared<JSObject>();
  prototype->values["name"] = newJSStringPtr(builtin::ERROR);
  auto func = createJSFunction(global, builtin::ERROR, {"message", "fileName", "lineNumber"},
                               std::move(prototype), errorConstructorImpl);
  global->define(builtin::ERROR, std::move(func));
}

void defineDerivedError(const std::shared_ptr<JSEnv> &global, const char *name) {
  auto errorConstructor = global->findOrUndef(builtin::ERROR);
  assert(std::holds_alternative<JSFunctionPtr>(errorConstructor));
  auto errorPrototype =
      getOwnProperty(*std::get<JSFunctionPtr>(errorConstructor), builtin::PROTOTYPE);
  auto prototype = std::make_shared<JSObject>();
  prototype->values["name"] = newJSStringPtr(name);
  prototype->values[builtin::PROTO] = errorPrototype;
  auto func = createJSFunction(global, name, {"message", "fileName", "lineNumber"},
                               std::move(prototype), errorConstructorImpl);
  global->define(name, std::move(func));
}

static void defineConsole(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto args = env->findOrUndef(builtin::ARGS);
    assert(std::holds_alternative<JSArrayPtr>(args));
    unsigned int count = 0;
    for (auto &arg : std::get<JSArrayPtr>(args)->array) {
      if (count++ > 0) {
        fputc(' ', stdout);
      }
      std::string out = toWTF8(toPrettyString(arg));
      fwrite(out.data(), sizeof(char), out.size(), stdout);
    }
    fputc('\n', stdout);
    fflush(stdout);
    return Ok(JSValue());
  };
  auto obj = std::make_shared<JSObject>();
  obj->values["log"] = createJSFunction(global, "log", {"message"}, nullptr, std::move(impl));
  global->define("console", std::move(obj));
}

static JSFunctionPtr createStringMatch(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    JSRegexPtr regex;
    if (auto arg = env->findOrUndef(func->params[0]); std::holds_alternative<JSRegexPtr>(arg)) {
      regex = std::get<JSRegexPtr>(arg);
    } else {
      auto regexConstructor = env->findGlobalEnv()->findOrUndef(builtin::REGEXP);
      auto ret = TRY(callJSFunction(env, env->callerLineNum(),
                                    std::get<JSFunctionPtr>(regexConstructor), nullptr, {arg}));
      regex = std::get<JSRegexPtr>(ret);
    }
    auto matchFunc = TRY(findProperty(env, regex, builtin::SYMBOL_MATCH));
    return callJSFunction(env, env->callerLineNum(), std::get<JSFunctionPtr>(matchFunc), regex,
                          {env->findOrUndef(builtin::THIS)});
  };
  return createJSFunction(global, "match", {"regexp"}, nullptr, std::move(impl));
}

static JSFunctionPtr createStringSlice(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto &thisStr = *std::get<JSStringPtr>(env->findOrUndef(builtin::THIS));
    size_t startIndex = 0;
    if (auto v = env->findOrUndef(func->params[0]); !isUndefined(v)) {
      const auto num = toIntegerOrInf(v);
      int64_t index = 0;
      if (std::isinf(num)) {
        index = num < 0 ? 0 : std::numeric_limits<int64_t>::max();
      } else {
        index = static_cast<int64_t>(num);
      }
      if (index < 0) {
        startIndex = std::max<int64_t>(index + static_cast<int64_t>(thisStr.size()), 0);
      } else {
        startIndex = std::min<uint64_t>(static_cast<uint64_t>(index), thisStr.size());
      }
    }
    size_t endIndex = thisStr.size();
    if (auto v = env->findOrUndef(func->params[1]); !isUndefined(v)) {
      const auto num = toIntegerOrInf(v);
      int64_t index = 0;
      if (std::isinf(num)) {
        index = num < 0 ? 0 : std::numeric_limits<int64_t>::max();
      } else {
        index = static_cast<int64_t>(num);
      }
      if (index < 0) {
        endIndex = std::max<int64_t>(index + static_cast<int64_t>(thisStr.size()), 0);
      } else {
        endIndex = std::min<uint64_t>(static_cast<uint64_t>(index), thisStr.size());
      }
    }
    JSString newStr;
    for (; startIndex < endIndex; startIndex++) {
      newStr += thisStr[startIndex];
    }
    return Ok(std::make_shared<JSString>(std::move(newStr)));
  };
  return createJSFunction(global, "slice", {"indexStart", "indexEnd"}, nullptr, std::move(impl));
}

static JSFunctionPtr createStringFromCharCode(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto args = env->findOrUndef(builtin::ARGS);
    assert(std::holds_alternative<JSArrayPtr>(args));
    JSString str;
    for (auto &e : std::get<JSArrayPtr>(args)->array) {
      char16_t v = toFixedSizeInteger<uint16_t>(e);
      str += v;
    }
    return Ok(std::make_shared<JSString>(std::move(str)));
  };
  return createJSFunction(global, "fromCharCode", {"num1"}, nullptr, std::move(impl));
}

static JSFunctionPtr createStringFromCodePoint(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto args = env->findOrUndef(builtin::ARGS);
    assert(std::holds_alternative<JSArrayPtr>(args));
    JSString str;
    for (auto &e : std::get<JSArrayPtr>(args)->array) {
      if (auto d = toNumber(e); isInteger(d)) {
        const auto v = static_cast<int64_t>(d);
        if (v >= 0 && v <= UnicodeUtil::CODE_POINT_MAX) {
          auto [high, low] = UnicodeUtil::codePointToUtf16(static_cast<int>(v));
          str += high;
          if (high != low) {
            str += low;
          }
          continue;
        }
      }
      JSString err = u"out of range code point: ";
      toPrettyString(e, err);
      return throwError(env, builtin::RANGE_ERROR, std::move(err));
    }
    return Ok(std::make_shared<JSString>(std::move(str)));
  };
  return createJSFunction(global, "fromCodePoint", {"num1"}, nullptr, std::move(impl));
}

static JSFunctionPtr createStringCharAt(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto &thisStr = *std::get<JSStringPtr>(env->findOrUndef(builtin::THIS));
    JSString str;
    if (double v = toIntegerOrInf(env->findOrUndef(func->params[0]));
        v >= 0 && static_cast<uint64_t>(v) < thisStr.size()) {
      str += thisStr[static_cast<uint64_t>(v)];
    }
    return Ok(std::make_shared<JSString>(std::move(str)));
  };
  return createJSFunction(global, "charAt", {"index"}, nullptr, std::move(impl));
}

static JSFunctionPtr createStringCharCodeAt(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto &thisStr = *std::get<JSStringPtr>(env->findOrUndef(builtin::THIS));
    if (double v = toIntegerOrInf(env->findOrUndef(func->params[0]));
        v >= 0 && static_cast<uint64_t>(v) < thisStr.size()) {
      return Ok(static_cast<double>(thisStr[static_cast<uint64_t>(v)]));
    }
    return Ok(std::nan(""));
  };
  return createJSFunction(global, "charCodeAt", {"index"}, nullptr, std::move(impl));
}

static JSFunctionPtr createStringCodePointAt(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto &thisStr = *std::get<JSStringPtr>(env->findOrUndef(builtin::THIS));
    if (double v = toIntegerOrInf(env->findOrUndef(func->params[0]));
        v >= 0 && static_cast<uint64_t>(v) < thisStr.size()) {
      const auto index = static_cast<uint64_t>(v);
      int codePoint = thisStr[index];
      if (UnicodeUtil::isHighSurrogate(codePoint) && index + 1 < thisStr.size() &&
          UnicodeUtil::isLowSurrogate(thisStr[index + 1])) {
        codePoint = UnicodeUtil::utf16ToCodePoint(thisStr[index], thisStr[index + 1]);
      }
      return Ok(static_cast<double>(codePoint));
    }
    return Ok(JSValue());
  };
  return createJSFunction(global, "codePointAt", {"index"}, nullptr, std::move(impl));
}

static void defineString(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto thing = env->findOrUndef(func->params[0]); // TODO: new String
    return Ok(std::make_shared<JSString>(toString(thing)));
  };
  auto prototype = std::make_shared<JSObject>();
  prototype->values["match"] = createStringMatch(global);
  prototype->values["slice"] = createStringSlice(global);
  prototype->values["charAt"] = createStringCharAt(global);
  prototype->values["charCodeAt"] = createStringCharCodeAt(global);
  prototype->values["codePointAt"] = createStringCodePointAt(global);
  auto func =
      createJSFunction(global, builtin::STRING, {"thing"}, std::move(prototype), std::move(impl));
  func->values["fromCharCode"] = createStringFromCharCode(global);
  func->values["fromCodePoint"] = createStringFromCodePoint(global);
  global->define(builtin::STRING, std::move(func));
}

static JSFunctionPtr createNumberToString(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    unsigned char radix = 10;
    if (auto v = env->findOrUndef(func->params[0]); !isUndefined(v)) {
      double num = toNumber(v);
      if (!isSafeInteger(num) || num < 2 || num > 36) {
        return throwError(env, builtin::RANGE_ERROR, u"toString() radix argument must be 2~36");
      }
      radix = static_cast<unsigned char>(num);
    }
    double value = std::get<double>(env->findOrUndef(builtin::THIS));
    if (!isSafeInteger(value) && radix != 10) { // TODO: radix for float
      return throwError(env, builtin::RANGE_ERROR,
                        u"float value toString() radix argument must be 10");
    }
    JSString out;
    toPrettyString(value, out, {.escape = false, .radix = radix});
    return Ok(std::make_shared<JSString>(std::move(out)));
  };
  return createJSFunction(global, "toString", {"radix"}, nullptr, std::move(impl));
}

static void defineNumber(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    if (auto *v = env->find(func->params[0])) { // TODO: new Number
      return Ok(toNumber(*v));
    }
    return Ok(0.0);
  };
  auto prototype = std::make_shared<JSObject>();
  prototype->values["toString"] = createNumberToString(global);
  auto func =
      createJSFunction(global, builtin::NUMBER, {"value"}, std::move(prototype), std::move(impl));
  func->values["EPSILON"] = std::numeric_limits<double>::epsilon();
  func->values["MAX_SAFE_INTEGER"] = MAX_SAFE_INTEGER;
  func->values["MIN_SAFE_INTEGER"] = MIN_SAFE_INTEGER;
  func->values["MAX_VALUE"] = std::numeric_limits<double>::max();
  func->values["MIN_VALUE"] = std::numeric_limits<double>::min();
  func->values["NaN"] = std::nan("");
  func->values["NEGATIVE_INFINITY"] = -INFINITY;
  func->values["POSITIVE_INFINITY"] = INFINITY;
  global->define(builtin::NUMBER, std::move(func));
}

JSArrayPtr createJSArray(const std::shared_ptr<JSEnv> &env) {
  auto constructor = env->findGlobalEnv()->findOrUndef(builtin::ARRAY);
  auto prototype = getOwnProperty(*std::get<JSFunctionPtr>(constructor), builtin::PROTOTYPE);
  auto array = std::make_shared<JSArray>();
  array->values[builtin::PROTO] = prototype;
  return array;
}

static JSFunctionPtr createArrayPush(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto args = env->findOrUndef(builtin::ARGS);
    assert(std::holds_alternative<JSArrayPtr>(args));
    auto array = std::get<JSArrayPtr>(env->findOrUndef(builtin::THIS));
    for (auto &arg : std::get<JSArrayPtr>(args)->array) {
      array->array.push_back(arg);
    }
    return Ok(static_cast<double>(array->array.size()));
  };
  return createJSFunction(global, "push", {"element"}, nullptr, std::move(impl));
}

static JSFunctionPtr createArrayJoin(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto array = std::get<JSArrayPtr>(env->findOrUndef(builtin::THIS));
    auto sep = env->findOrUndef(func->params[0]);
    JSString str;
    unsigned int c = 0;
    for (auto &e : array->array) {
      if (c++ > 0) {
        if (isUndefined(sep)) {
          str += u',';
        } else {
          toString(sep, str);
        }
      }
      if (!isUndefined(e) && !isNull(e)) {
        toString(e, str);
      }
    }
    return Ok(std::make_shared<JSString>(std::move(str)));
  };
  return createJSFunction(global, "join", {"separator"}, nullptr, std::move(impl));
}

static void defineArray(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto args = env->findOrUndef(builtin::ARGS);
    assert(std::holds_alternative<JSArrayPtr>(args));
    auto array = createJSArray(env);
    array->array.reserve(std::get<JSArrayPtr>(args)->array.size());
    for (auto &e : std::get<JSArrayPtr>(args)->array) { // TODO: arrayLength
      array->array.push_back(e);
    }
    return Ok(std::move(array));
  };
  auto prototype = std::make_shared<JSObject>();
  prototype->values["push"] = createArrayPush(global);
  prototype->values["join"] = createArrayJoin(global);
  auto func =
      createJSFunction(global, builtin::ARRAY, {"element"}, std::move(prototype), std::move(impl));
  global->define(builtin::ARRAY, std::move(func));
}

std::shared_ptr<JSEnv> initJSEnv() {
  auto global = JSEnv::createGlobal();
  global->define("undefined", JSValue());
  global->define("NaN", std::nan(""));
  global->define("Infinity", INFINITY);
  defineError(global);
  defineDerivedError(global, builtin::SYNTAX_ERROR);
  defineDerivedError(global, builtin::TYPE_ERROR);
  defineDerivedError(global, builtin::REF_ERROR);
  defineDerivedError(global, builtin::RANGE_ERROR);
  defineString(global);
  defineNumber(global);
  defineArray(global);
  defineJSRegex(global);
  defineConsole(global);
  return global;
}

// for node definition

struct Node;

struct NullLiteral {};

struct BoolLiteral {
  bool value;
};

struct NumberLiteral {
  double value;
};

struct StringLiteral {
  JSStringPtr value;
};

struct RegexLiteral {
  JSRegexPtr value;
};

struct ArrayLiteral {
  std::vector<std::unique_ptr<Node>> values;
};

struct ObjectLiteral {
  std::vector<std::pair<std::string, std::unique_ptr<Node>>> values;
};

struct FuncLiteral {
  std::string name;
  std::vector<std::string> params;
  std::shared_ptr<std::vector<std::unique_ptr<Node>>> nodes;
};

struct NameExpr {
  std::string name;
};

struct AccessExpr {
  std::unique_ptr<Node> recv;
  std::string name;
};

struct IndexExpr {
  std::unique_ptr<Node> recv;
  std::unique_ptr<Node> index;
};

struct CallExpr {
  std::unique_ptr<Node> func;
  std::vector<std::unique_ptr<Node>> args;
  bool newExpr{false};
};

struct UnaryExpr {
  JSTokenKind op;
  std::unique_ptr<Node> expr;
};

struct BinaryExpr {
  std::unique_ptr<Node> left;
  JSTokenKind op;
  std::unique_ptr<Node> right;
};

struct AssignExpr {
  std::unique_ptr<Node> left;  // maybe null if prefix ++, --
  JSTokenKind op;              // in addition to assign op, maybe ++, --
  std::unique_ptr<Node> right; // maybe null if suffix ++, --
};

struct VarDecl { // currently only support `const`
  enum class Kind : unsigned char {
    CONST,
    LET,
    VAR,
  } kind;

  std::string name;
  std::unique_ptr<Node> expr;
};

struct JumpStmt {
  JSResult::Status status;
  std::unique_ptr<Node> expr; // maybe null
};

struct BlockStmt {
  std::vector<std::unique_ptr<Node>> nodes;
};

struct TryStmt {
  std::unique_ptr<Node> tryBlock;     // must be BlockStmt
  std::string except;                 // for caught exception (maybe empty)
  std::unique_ptr<Node> catchBlock;   // must be BlockStmt. maybe null
  std::unique_ptr<Node> finallyBlock; // must be BlockStmt. maybe null
};

struct IfStmt {
  std::unique_ptr<Node> cond;
  std::unique_ptr<Node> thenStmt;
  std::unique_ptr<Node> elseStmt; // maybe null
};

struct ForStmt {
  std::unique_ptr<Node> init;  // maybe null
  std::unique_ptr<Node> cond;  // maybe null
  std::unique_ptr<Node> after; // maybe null
  std::unique_ptr<Node> body;
};

struct ForOfStmt {
  std::unique_ptr<Node> iter; // must be VarDecl
  std::unique_ptr<Node> body;
};

struct Node {
  unsigned int lineNum;

  using Underlying =
      std::variant<NullLiteral, BoolLiteral, NumberLiteral, StringLiteral, RegexLiteral,
                   ArrayLiteral, ObjectLiteral, FuncLiteral, NameExpr, AccessExpr, IndexExpr,
                   CallExpr, UnaryExpr, BinaryExpr, AssignExpr, VarDecl, JumpStmt, BlockStmt,
                   TryStmt, IfStmt, ForStmt, ForOfStmt>;
  Underlying value;

  Node(unsigned int lineNum, Underlying v) : lineNum(lineNum), value(std::move(v)) {}
};

// ######################
// ##     JSParser     ##
// ######################

#define EACH_LA_JS_PRIMARY(OP)                                                                     \
  OP(NIL)                                                                                          \
  OP(TRUE)                                                                                         \
  OP(FALSE)                                                                                        \
  OP(NUMBER)                                                                                       \
  OP(STRING)                                                                                       \
  OP(REGEX)                                                                                        \
  OP(IDENTIFIER)                                                                                   \
  OP(FUNCTION)                                                                                     \
  OP(LB)                                                                                           \
  OP(LBC)                                                                                          \
  OP(LP)

#define EACH_LA_JS_EXPRESSION(OP)                                                                  \
  OP(NOT)                                                                                          \
  OP(ADD)                                                                                          \
  OP(SUB)                                                                                          \
  OP(NEW)                                                                                          \
  OP(VOID)                                                                                         \
  OP(TYPEOF)                                                                                       \
  OP(INC)                                                                                          \
  OP(DEC)                                                                                          \
  EACH_LA_JS_PRIMARY(OP)

#define EACH_LA_JS_VAR_DECL(OP)                                                                    \
  OP(CONST)                                                                                        \
  OP(LET)                                                                                          \
  OP(VAR)

#define EACH_LA_JS_STATEMENT(OP)                                                                   \
  EACH_LA_JS_VAR_DECL(OP)                                                                          \
  OP(RETURN)                                                                                       \
  OP(THROW)                                                                                        \
  OP(TRY)                                                                                          \
  OP(IF)                                                                                           \
  OP(FOR)                                                                                          \
  OP(WHILE)                                                                                        \
  OP(BREAK)                                                                                        \
  OP(CONTINUE)                                                                                     \
  EACH_LA_JS_EXPRESSION(OP)

#define GEN_LA_CASE(CASE) case JSTokenKind::CASE:
#define GEN_LA_ALTER(CASE) JSTokenKind::CASE,

#define E_ALTER(...)                                                                               \
  do {                                                                                             \
    this->reportNoViableAlterError((JSTokenKind[]){__VA_ARGS__});                                  \
    return nullptr;                                                                                \
  } while (false)

#undef TRY
#define TRY(expr)                                                                                  \
  ({                                                                                               \
    auto v = expr;                                                                                 \
    if (unlikely(this->hasError())) {                                                              \
      return nullptr;                                                                              \
    }                                                                                              \
    std::forward<decltype(v)>(v);                                                                  \
  })

class JSParser : public ParserBase<JSTokenKind, JSLexer> {
private:
  std::shared_ptr<JSEnv> global;

public:
  struct Error {
    std::string sourceName;
    unsigned int lineNum;
    std::string message;
    std::string detail;
  };

  JSParser(const std::shared_ptr<JSEnv> &global, JSLexer &lex) : global(global) {
    this->lexer = &lex;
    this->fetchNext();
  }

  std::unique_ptr<Node> operator()() { return this->parseStatement(); }

  explicit operator bool() const { return !isEOSToken(this->curKind); }

  std::optional<Error> formatError() const;

private:
  Token expectVarDeclIdentifier();

  std::unique_ptr<Node> parseStatement();

  std::unique_ptr<Node> parseBlock();

  std::unique_ptr<Node> parseTryStatement();

  std::unique_ptr<Node> parseIfStatement();

  std::unique_ptr<Node> parseForStatement();

  std::unique_ptr<Node> parseWhileStatement();

  std::unique_ptr<Node> parseExpression() {
    return this->parseExpression(getOperatorInfo(JSTokenKind::ASSIGN).precedence);
  }

  std::unique_ptr<Node> parseExpression(JSOperatorPrecedence base);

  std::unique_ptr<Node> parseUnaryExpression();

  std::unique_ptr<Node> parseCallExpression();

  std::unique_ptr<Node> parseMemberExpression();

  std::unique_ptr<Node> parseMemberAccess(std::unique_ptr<Node> &&node);

  std::unique_ptr<Node> parseWithArguments(std::unique_ptr<Node> &&node, bool isNew = false);

  std::unique_ptr<Node> parsePrimary();

  std::unique_ptr<Node> parseNumber();

  std::unique_ptr<Node> parseObject();

  std::unique_ptr<Node> parseArray();

  std::unique_ptr<Node> parseFunction();
};

Token JSParser::expectVarDeclIdentifier() {
  auto token = this->expect(JSTokenKind::IDENTIFIER);
  if (!this->hasError()) {
    if (this->lexer->toStrRef(token) == "arguments") {
      this->reportTokenFormatError(JSTokenKind::IDENTIFIER, token, "unexpected `arguments'");
    }
  }
  return token;
}

std::optional<JSParser::Error> JSParser::formatError() const {
  if (!this->hasError()) {
    return {};
  }

  auto errorToken = this->lexer->shiftEOS(this->getError().getErrorToken());
  const unsigned int lineNum = this->lexer->getLineNumByPos(errorToken.pos);
  std::string str;
  str += this->lexer->getSourceName();
  str += ':';
  str += std::to_string(lineNum);
  str += " [error] ";
  str += this->getError().getMessage();
  str += '\n';

  auto lineToken = this->lexer->getLineToken(errorToken);

  str += this->lexer->formatTokenText(lineToken);
  str += this->lexer->formatLineMarker(lineToken, errorToken);
  str += '\n';

  Error err = {
      .sourceName = this->lexer->getSourceName(),
      .lineNum = lineNum,
      .message = this->getError().getMessage(),
      .detail = std::move(str),
  };
  return err;
}

static VarDecl::Kind toVarKind(JSTokenKind kind) {
  switch (kind) {
  case JSTokenKind::CONST:
    return VarDecl::Kind::CONST;
  case JSTokenKind::LET:
    return VarDecl::Kind::LET;
  default:
    break;
  }
  return VarDecl::Kind::VAR;
}

std::unique_ptr<Node> JSParser::parseStatement() {
  switch (this->curKind) {
    EACH_LA_JS_VAR_DECL(GEN_LA_CASE) {
      const auto kind = toVarKind(this->curKind);
      this->consume();
      Token token = TRY(this->expectVarDeclIdentifier());
      std::unique_ptr<Node> expr;
      if (this->curKind == JSTokenKind::ASSIGN) {
        TRY(this->expect(JSTokenKind::ASSIGN));
        expr = TRY(this->parseExpression());
      }
      TRY(this->expect(JSTokenKind::LINE_END));
      return std::make_unique<Node>(
          this->lexer->getLineNumByPos(token.pos),
          VarDecl{kind, this->lexer->toTokenText(token), std::move(expr)});
    }
  case JSTokenKind::RETURN: {
    Token token = TRY(this->expect(JSTokenKind::RETURN));
    std::unique_ptr<Node> node;
    if (this->curKind != JSTokenKind::LINE_END) {
      node = TRY(this->parseExpression());
    }
    TRY(this->expect(JSTokenKind::LINE_END));
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  JumpStmt{JSResult::Status::RETURN, std::move(node)});
  }
  case JSTokenKind::THROW: {
    Token token = TRY(this->expect(JSTokenKind::THROW));
    auto node = TRY(this->parseExpression());
    TRY(this->expect(JSTokenKind::LINE_END));
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  JumpStmt{JSResult::Status::ERR, std::move(node)});
  }
  case JSTokenKind::TRY:
    return this->parseTryStatement();
  case JSTokenKind::IF:
    return this->parseIfStatement();
  case JSTokenKind::BREAK: {
    Token token = TRY(this->expect(JSTokenKind::BREAK));
    TRY(this->expect(JSTokenKind::LINE_END));
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  JumpStmt{JSResult::Status::BREAK, nullptr});
  }
  case JSTokenKind::CONTINUE: {
    Token token = TRY(this->expect(JSTokenKind::CONTINUE));
    TRY(this->expect(JSTokenKind::LINE_END));
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  JumpStmt{JSResult::Status::CONTINUE, nullptr});
  }
  case JSTokenKind::WHILE:
    return this->parseWhileStatement();
  case JSTokenKind::FOR:
    return this->parseForStatement();
    // clang-format off
  EACH_LA_JS_EXPRESSION(GEN_LA_CASE) {
    auto expr = TRY(this->parseExpression());
    TRY(this->expect(JSTokenKind::LINE_END));
    return expr;
  }
    // clang-format on
  default:
    E_ALTER(EACH_LA_JS_STATEMENT(GEN_LA_ALTER));
  }
}

std::unique_ptr<Node> JSParser::parseBlock() {
  std::vector<std::unique_ptr<Node>> nodes;
  auto token = TRY(this->expect(JSTokenKind::LBC));
  while (this->curKind != JSTokenKind::RBC) {
    auto node = TRY(this->parseStatement());
    nodes.push_back(std::move(node));
  }
  TRY(this->expect(JSTokenKind::RBC));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                BlockStmt{std::move(nodes)});
}

std::unique_ptr<Node> JSParser::parseIfStatement() {
  auto token = TRY(this->expect(JSTokenKind::IF));
  TRY(this->expect(JSTokenKind::LP));
  auto cond = TRY(this->parseExpression());
  TRY(this->expect(JSTokenKind::RP));
  std::unique_ptr<Node> thenStmt;
  if (this->curKind == JSTokenKind::LBC) {
    thenStmt = TRY(this->parseBlock());
  } else {
    thenStmt = TRY(this->parseStatement());
  }
  std::unique_ptr<Node> elseStmt;
  if (this->curKind == JSTokenKind::ELSE) {
    this->consume();
    if (this->curKind == JSTokenKind::LBC) {
      elseStmt = TRY(this->parseBlock());
    } else {
      elseStmt = TRY(this->parseStatement());
    }
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                IfStmt{
                                    .cond = std::move(cond),
                                    .thenStmt = std::move(thenStmt),
                                    .elseStmt = std::move(elseStmt),
                                });
}

std::unique_ptr<Node> JSParser::parseForStatement() {
  Token start = TRY(this->expect(JSTokenKind::FOR));
  bool forOf = false;
  std::unique_ptr<Node> init;
  TRY(this->expect(JSTokenKind::LP));
  switch (this->curKind) {
    EACH_LA_JS_VAR_DECL(GEN_LA_CASE) {
      const auto kind = toVarKind(this->curKind);
      this->consume();
      Token token = TRY(this->expectVarDeclIdentifier());
      std::unique_ptr<Node> expr;
      if (this->curKind == JSTokenKind::OF) {
        forOf = true;
        this->consume();
        expr = TRY(this->parseExpression());
      } else if (this->curKind == JSTokenKind::ASSIGN) {
        this->consume();
        expr = TRY(this->parseExpression());
      }
      if (!forOf) {
        TRY(this->expect(JSTokenKind::LINE_END));
      }
      init =
          std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                 VarDecl{kind, this->lexer->toTokenText(token), std::move(expr)});
      break;
    }
  default:
    if (this->curKind != JSTokenKind::LINE_END) {
      init = TRY(this->parseExpression());
    }
    TRY(this->expect(JSTokenKind::LINE_END));
    break;
  }
  std::unique_ptr<Node> cond;
  std::unique_ptr<Node> after;
  if (!forOf) {
    cond = TRY(this->parseExpression());
    TRY(this->expect(JSTokenKind::LINE_END));
    if (this->curKind != JSTokenKind::RP) {
      after = TRY(this->parseExpression());
    }
  }
  TRY(this->expect(JSTokenKind::RP));
  std::unique_ptr<Node> body;
  if (this->curKind == JSTokenKind::LBC) {
    body = TRY(this->parseBlock());
  } else {
    body = TRY(this->parseStatement());
  }
  if (forOf) {
    return std::make_unique<Node>(this->lexer->getLineNumByPos(start.pos),
                                  ForOfStmt{
                                      .iter = std::move(init),
                                      .body = std::move(body),
                                  });
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(start.pos),
                                ForStmt{
                                    .init = std::move(init),
                                    .cond = std::move(cond),
                                    .after = std::move(after),
                                    .body = std::move(body),
                                });
}

std::unique_ptr<Node> JSParser::parseWhileStatement() {
  Token token = TRY(this->expect(JSTokenKind::WHILE));
  TRY(this->expect(JSTokenKind::LP));
  auto cond = TRY(this->parseExpression());
  TRY(this->expect(JSTokenKind::RP));
  std::unique_ptr<Node> body;
  if (this->curKind == JSTokenKind::LBC) {
    body = TRY(this->parseBlock());
  } else {
    body = TRY(this->parseStatement());
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                ForStmt{
                                    .init = nullptr,
                                    .cond = std::move(cond),
                                    .after = nullptr,
                                    .body = std::move(body),
                                });
}

std::unique_ptr<Node> JSParser::parseTryStatement() {
  Token token = TRY(this->expect(JSTokenKind::TRY));
  auto tryBlock = TRY(this->parseBlock());
  std::string except;
  std::unique_ptr<Node> catchBlock;
  if (this->curKind == JSTokenKind::CATCH) {
    TRY(this->expect(JSTokenKind::CATCH));
    if (this->curKind == JSTokenKind::LP) {
      TRY(this->expect(JSTokenKind::LP));
      except = this->lexer->toTokenText(TRY(this->expectVarDeclIdentifier()));
      TRY(this->expect(JSTokenKind::RP));
    }
    catchBlock = TRY(this->parseBlock());
  }
  std::unique_ptr<Node> finallyBlock;
  if (this->curKind == JSTokenKind::FINALLY) {
    TRY(this->expect(JSTokenKind::FINALLY));
    finallyBlock = TRY(this->parseBlock());
  } else if (!catchBlock) {
    E_ALTER(JSTokenKind::CATCH, JSTokenKind::FINALLY);
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                TryStmt{
                                    .tryBlock = std::move(tryBlock),
                                    .except = std::move(except),
                                    .catchBlock = std::move(catchBlock),
                                    .finallyBlock = std::move(finallyBlock),
                                });
}

static bool isAssignable(const Node &node) {
  return std::holds_alternative<NameExpr>(node.value) ||
         std::holds_alternative<AccessExpr>(node.value) ||
         std::holds_alternative<IndexExpr>(node.value);
}

std::unique_ptr<Node> JSParser::parseExpression(JSOperatorPrecedence base) {
  auto node = TRY(this->parseUnaryExpression());
  while (isOperator(this->curKind)) {
    const auto info = getOperatorInfo(this->curKind);
    if (!hasFlag(info.attr, JSOperatorAttr::INFIX) || info.precedence < base) {
      break;
    }
    Token token = this->curToken;
    JSTokenKind kind = this->scan();
    const auto next =
        hasFlag(info.attr, JSOperatorAttr::RASSOC) ? info.precedence : advance(info.precedence);
    auto rightNode = this->parseExpression(next);
    unsigned int lineNum = node->lineNum;
    if (isAssignOp(kind)) {
      if (!isAssignable(*node)) {
        this->reportTokenFormatError(kind, token, "invalid left-hand side of assignment");
        return nullptr;
      }
      node =
          std::make_unique<Node>(lineNum, AssignExpr{std::move(node), kind, std::move(rightNode)});
    } else {
      node =
          std::make_unique<Node>(lineNum, BinaryExpr{std::move(node), kind, std::move(rightNode)});
    }
  }
  return node;
}

std::unique_ptr<Node> JSParser::parseUnaryExpression() {
  switch (this->curKind) {
  case JSTokenKind::NOT:
  case JSTokenKind::ADD:
  case JSTokenKind::SUB:
  case JSTokenKind::VOID:
  case JSTokenKind::TYPEOF: {
    Token token = this->curToken;
    JSTokenKind kind = this->scan();
    auto expr = TRY(this->parseUnaryExpression());
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  UnaryExpr{kind, std::move(expr)});
  }
  case JSTokenKind::INC:
  case JSTokenKind::DEC: {
    Token token = this->curToken;
    JSTokenKind kind = this->scan();
    auto expr = TRY(this->parseUnaryExpression());
    if (!isAssignable(*expr)) {
      this->reportTokenFormatError(kind, token,
                                   "invalid left-hand side expression in prefix operation");
      return nullptr;
    }
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  AssignExpr{nullptr, kind, std::move(expr)});
  }
  default:
    return this->parseCallExpression();
  }
}

std::unique_ptr<Node> JSParser::parseCallExpression() {
  auto node = TRY(this->parseMemberExpression());
  while (true) {
    switch (this->curKind) {
    case JSTokenKind::DOT:
    case JSTokenKind::LB:
      node = TRY(this->parseMemberAccess(std::move(node)));
      continue;
    case JSTokenKind::LP:
      node = TRY(this->parseWithArguments(std::move(node)));
      continue;
    default:
      break;
    }
    break;
  }

  // suffix op
  switch (this->curKind) {
  case JSTokenKind::INC:
  case JSTokenKind::DEC: {
    Token token = this->curToken;
    JSTokenKind kind = this->scan();
    if (!isAssignable(*node)) {
      this->reportTokenFormatError(kind, token,
                                   "invalid left-hand side expression in postfix operation");
      return nullptr;
    }
    unsigned int lineNum = node->lineNum;
    node = std::make_unique<Node>(lineNum, AssignExpr{std::move(node), kind, nullptr});
    break;
  }
  default:
    break;
  }
  return node;
}

std::unique_ptr<Node> JSParser::parseMemberExpression() {
  std::unique_ptr<Node> node;
  if (this->curKind == JSTokenKind::NEW) {
    this->consume();
    auto constructor = TRY(this->parseMemberExpression());
    if (this->curKind == JSTokenKind::LP) {
      node = TRY(this->parseWithArguments(std::move(constructor), true));
    } else {
      CallExpr call;
      unsigned int lineNum = constructor->lineNum;
      call.func = std::move(constructor);
      call.newExpr = true;
      node = std::make_unique<Node>(lineNum, std::move(call));
    }
  } else {
    node = TRY(this->parsePrimary());
  }
  return this->parseMemberAccess(std::move(node));
}

std::unique_ptr<Node> JSParser::parseMemberAccess(std::unique_ptr<Node> &&node) {
  while (true) {
    switch (this->curKind) {
    case JSTokenKind::DOT: {
      this->consume();
      Token token = TRY(this->expect(JSTokenKind::IDENTIFIER));
      unsigned int lineNum = node->lineNum;
      node = std::make_unique<Node>(lineNum,
                                    AccessExpr{std::move(node), this->lexer->toTokenText(token)});
      continue;
    }
    case JSTokenKind::LB: {
      this->consume();
      unsigned int lineNum = node->lineNum;
      auto expr = TRY(this->parseExpression());
      TRY(this->expect(JSTokenKind::RB));
      node = std::make_unique<Node>(lineNum, IndexExpr{std::move(node), std::move(expr)});
      continue;
    }
    default:
      return std::move(node);
    }
  }
}

std::unique_ptr<Node> JSParser::parseWithArguments(std::unique_ptr<Node> &&node, const bool isNew) {
  TRY(this->expect(JSTokenKind::LP));
  CallExpr call;
  unsigned int lineNum = node->lineNum;
  call.func = std::move(node);
  call.newExpr = isNew;
  while (this->curKind != JSTokenKind::RP) {
    call.args.push_back(TRY(this->parseExpression()));
    if (this->curKind == JSTokenKind::COMMA) {
      this->consume();
    } else if (this->curKind != JSTokenKind::RP) {
      E_ALTER(JSTokenKind::COMMA, JSTokenKind::RP);
    }
  }
  TRY(this->expect(JSTokenKind::RP));
  return std::make_unique<Node>(lineNum, std::move(call));
}

std::unique_ptr<Node> JSParser::parsePrimary() {
  switch (this->curKind) {
  case JSTokenKind::NIL: {
    Token token = this->expect(JSTokenKind::NIL);
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), NullLiteral{});
  }
  case JSTokenKind::TRUE: {
    Token token = this->expect(JSTokenKind::TRUE);
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), BoolLiteral{true});
  }
  case JSTokenKind::FALSE: {
    Token token = this->expect(JSTokenKind::FALSE);
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), BoolLiteral{false});
  }
  case JSTokenKind::NUMBER:
    return this->parseNumber();
  case JSTokenKind::STRING: {
    auto token = this->expect(JSTokenKind::STRING);
    std::string err;
    if (auto str = this->lexer->toString(token, &err); str.has_value()) {
      return std::make_unique<Node>(
          this->lexer->getLineNumByPos(token.pos),
          StringLiteral{std::make_shared<std::u16string>(std::move(str.value()))});
    }
    this->reportTokenFormatError(JSTokenKind::STRING, token, "out of range");
    return nullptr;
  }
  case JSTokenKind::REGEX: {
    auto token = this->expect(JSTokenKind::REGEX);
    unsigned int lineNum = this->lexer->getLineNumByPos(token.pos);
    std::string err;
    auto prototype = findProperty(this->global, lineNum, this->global->findOrUndef(builtin::REGEXP),
                                  builtin::PROTOTYPE);
    assert(prototype);
    assert(std::holds_alternative<JSObjectPtr>(prototype.value));
    if (auto ret = createJSRegexFromLiteral(std::get<JSObjectPtr>(prototype.value),
                                            this->lexer->toStrRef(token), &err)) {
      return std::make_unique<Node>(lineNum, RegexLiteral{std::move(ret)});
    }
    this->reportTokenFormatError(JSTokenKind::REGEX, token, std::move(err));
    return nullptr;
  }
  case JSTokenKind::IDENTIFIER: {
    auto token = this->expect(JSTokenKind::IDENTIFIER);
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  NameExpr{this->lexer->toTokenText(token)});
  }
  case JSTokenKind::FUNCTION:
    return this->parseFunction();
  case JSTokenKind::LB:
    return this->parseArray();
  case JSTokenKind::LBC:
    return this->parseObject();
  case JSTokenKind::LP: {
    this->consume();
    auto node = this->parseExpression();
    TRY(this->expect(JSTokenKind::RP));
    return node;
  }
  default:
    E_ALTER(EACH_LA_JS_PRIMARY(GEN_LA_ALTER));
  }
}

std::unique_ptr<Node> JSParser::parseNumber() {
  Token token = TRY(this->expect(JSTokenKind::NUMBER));
  std::string data;
  data.reserve(token.size);
  for (char ch : this->lexer->toStrRef(token)) {
    if (ch == '_') {
      continue;
    }
    data += ch;
  }
  if (auto ret = convertToDouble(data.c_str())) {
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  NumberLiteral{ret.value});
  }
  this->reportTokenFormatError(JSTokenKind::NUMBER, token, "out of range");
  return nullptr;
}

std::unique_ptr<Node> JSParser::parseObject() {
  Token start = TRY(this->expect(JSTokenKind::LBC));
  ObjectLiteral object;
  while (this->curKind != JSTokenKind::RBC) {
    Token token = TRY(this->expect(JSTokenKind::IDENTIFIER));
    TRY(this->expect(JSTokenKind::COLON));
    auto expr = TRY(this->parseExpression());
    object.values.emplace_back(this->lexer->toTokenText(token), std::move(expr));
    if (this->curKind == JSTokenKind::COMMA) {
      this->consume();
    } else if (this->curKind != JSTokenKind::RBC) {
      E_ALTER(JSTokenKind::COMMA, JSTokenKind::RBC);
    }
  }
  TRY(this->expect(JSTokenKind::RBC));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(start.pos), std::move(object));
}

std::unique_ptr<Node> JSParser::parseArray() {
  Token token = TRY(this->expect(JSTokenKind::LB));
  ArrayLiteral array;
  while (this->curKind != JSTokenKind::RB) {
    auto node = TRY(this->parseExpression());
    array.values.push_back(std::move(node));
    if (this->curKind == JSTokenKind::COMMA) {
      this->consume();
    } else if (this->curKind != JSTokenKind::RB) {
      E_ALTER(JSTokenKind::COMMA, JSTokenKind::RB);
    }
  }
  TRY(this->expect(JSTokenKind::RB));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), std::move(array));
}

std::unique_ptr<Node> JSParser::parseFunction() {
  Token token = TRY(this->expect(JSTokenKind::FUNCTION));
  FuncLiteral func;
  func.nodes = std::make_shared<std::vector<std::unique_ptr<Node>>>();
  TRY(this->expect(JSTokenKind::LP));
  while (this->curKind != JSTokenKind::RP) {
    Token nameToken = TRY(this->expectVarDeclIdentifier());
    func.params.push_back(this->lexer->toTokenText(nameToken));
    if (this->curKind == JSTokenKind::COMMA) {
      this->consume();
    } else if (this->curKind != JSTokenKind::RP) {
      E_ALTER(JSTokenKind::COMMA, JSTokenKind::RP);
    }
  }
  TRY(this->expect(JSTokenKind::RP));
  TRY(this->expect(JSTokenKind::LBC));
  while (this->curKind != JSTokenKind::RBC) {
    func.nodes->push_back(TRY(this->parseStatement()));
  }
  TRY(this->expect(JSTokenKind::RBC));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), std::move(func));
}

#undef TRY
#define TRY(...)                                                                                   \
  ({                                                                                               \
    auto v__ = (__VA_ARGS__);                                                                      \
    if (!v__) {                                                                                    \
      return v__;                                                                                  \
    }                                                                                              \
    std::move(v__.value);                                                                          \
  })

static JSResult evaluate(const Node &node, const std::shared_ptr<JSEnv> &env);

static JSResult evalArray(const ArrayLiteral &literal, const std::shared_ptr<JSEnv> &env) {
  auto array = createJSArray(env);
  array->array.reserve(literal.values.size());
  for (auto &e : literal.values) {
    auto ret = TRY(evaluate(*e, env));
    array->array.push_back(std::move(ret));
  }
  return Ok(std::move(array));
}

static JSResult evalObject(const ObjectLiteral &literal, const std::shared_ptr<JSEnv> &env) {
  JSObjectPtr object = std::make_shared<JSObject>();
  for (auto &[k, v] : literal.values) {
    auto value = TRY(evaluate(*v, env));
    object->values[k] = std::move(value);
  }
  return Ok(std::move(object));
}

static JSResult evalCallExpr(const CallExpr &callExpr, const unsigned int lineNum,
                             const std::shared_ptr<JSEnv> &env) {
  JSValue callee;
  JSValue recv;
  if (callExpr.newExpr) {
    callee = TRY(evaluate(*callExpr.func, env));
  } else if (std::holds_alternative<AccessExpr>(callExpr.func->value)) {
    auto &access = std::get<AccessExpr>(callExpr.func->value);
    recv = TRY(evaluate(*access.recv, env));
    callee = TRY(findProperty(env, lineNum, recv, access.name));
  } else {
    callee = TRY(evaluate(*callExpr.func, env));
  }
  JSFunctionPtr func;
  if (std::holds_alternative<JSFunctionPtr>(callee)) {
    func = std::get<JSFunctionPtr>(callee);
    if (callExpr.newExpr) {
      recv = newObject(func);
    }
  } else {
    return throwError(env, builtin::TYPE_ERROR, lineNum, u"not a function");
  }
  std::vector<JSValue> args;
  args.reserve(callExpr.args.size());
  for (auto &e : callExpr.args) {
    args.push_back(TRY(evaluate(*e, env)));
  }
  return callJSFunction(env, lineNum, func, std::move(recv), std::move(args));
}

static JSResult evalFunc(const FuncLiteral &literal, const std::shared_ptr<JSEnv> &env) {
  assert(literal.name.empty());
  auto impl = [nodes = literal.nodes](const JSFunctionPtr &,
                                      const std::shared_ptr<JSEnv> &env) -> JSResult {
    for (auto &node : *nodes) {
      switch (auto [status, value] = evaluate(*node, env); status) {
      case JSResult::Status::OK:
      case JSResult::Status::BREAK:    // unreachable
      case JSResult::Status::CONTINUE: // unreachable
        continue;
      case JSResult::Status::ERR:
        return Err(std::move(value));
      case JSResult::Status::RETURN:
        return Ok(std::move(value));
      }
    }
    return Ok(JSValue());
  };
  return Ok(createJSFunction(env, "", std::vector(literal.params), nullptr, std::move(impl)));
}

static JSResult evalUnary(const UnaryExpr &unary, const std::shared_ptr<JSEnv> &env) {
  auto value = TRY(evaluate(*unary.expr, env));
  switch (unary.op) {
  case JSTokenKind::NOT:
    return Ok(!toBool(value));
  case JSTokenKind::ADD:
    return Ok(toNumber(value));
  case JSTokenKind::SUB:
    return Ok(-toNumber(value));
  case JSTokenKind::VOID:
    return Ok(JSValue()); // always `undefined`
  case JSTokenKind::TYPEOF:
    return Ok(std::make_shared<JSString>(typeOf(value)));
  default:
    fatal("unreachable: %s\n", toString(unary.op));
  }
}

static JSResult evalBinary(const BinaryExpr &binary, const std::shared_ptr<JSEnv> &env) {
  if (binary.op == JSTokenKind::COND_AND) {
    if (auto left = TRY(evaluate(*binary.left, env)); !toBool(left)) {
      return Ok(std::move(left));
    }
    return evaluate(*binary.right, env);
  }
  if (binary.op == JSTokenKind::COND_OR) {
    if (auto left = TRY(evaluate(*binary.left, env)); toBool(left)) {
      return Ok(std::move(left));
    }
    return evaluate(*binary.right, env);
  }
  auto left = TRY(evaluate(*binary.left, env));
  auto right = TRY(evaluate(*binary.right, env));
  switch (binary.op) {
  case JSTokenKind::ADD:
    if (std::holds_alternative<JSStringPtr>(left) || std::holds_alternative<JSStringPtr>(right)) {
      JSString str;
      toString(left, str);
      toString(right, str);
      return Ok(std::make_shared<JSString>(std::move(str)));
    }
    return Ok(toNumber(left) + toNumber(right));
  case JSTokenKind::SUB:
    return Ok(toNumber(left) - toNumber(right));
  case JSTokenKind::MOD:
    return Ok(jsRemainder(toNumber(left), toNumber(right)));
  case JSTokenKind::LT:
    return Ok(compare(left, right) < 0);
  case JSTokenKind::LE:
    return Ok(compare(left, right) <= 0);
  case JSTokenKind::GT:
    return Ok(compare(left, right) > 0);
  case JSTokenKind::GE:
    return Ok(compare(left, right) >= 0);
  case JSTokenKind::INSTANCEOF:
    return isInstanceOf(env, env->callerLineNum(), left, right);
  case JSTokenKind::EQ2:
    return Ok(strictlyEquals(left, right));
  case JSTokenKind::NE2:
    return Ok(!strictlyEquals(left, right));
  default:
    fatal("unreachable: %s\n", toString(binary.op));
  }
  return Ok(JSValue());
}

static std::optional<unsigned int> toArrayIndex(const JSValue &value) {
  if (std::holds_alternative<double>(value)) {
    if (auto d = std::get<double>(value);
        isSafeInteger(d) && d > -1 && static_cast<uint64_t>(d) <= UINT32_MAX) {
      return static_cast<unsigned int>(d);
    }
  } else if (std::holds_alternative<JSStringPtr>(value)) {
    const auto &str = *std::get<JSStringPtr>(value);
    if (const auto index = toFixedSizeInteger<unsigned int>(value);
        str == toString(static_cast<double>(index))) {
      return index;
    }
  }
  return {};
}

static JSResult evalIndex(const IndexExpr &expr, const std::shared_ptr<JSEnv> &env) {
  auto recv = TRY(evaluate(*expr.recv, env));
  auto index = TRY(evaluate(*expr.index, env));
  if (auto arrayIndex = toArrayIndex(index)) {
    if (std::holds_alternative<JSStringPtr>(recv)) {
      if (auto &str = *std::get<JSStringPtr>(recv); arrayIndex.value() < str.size()) {
        JSString ret;
        ret += str[arrayIndex.value()];
        return Ok(std::make_shared<JSString>(std::move(ret)));
      }
      return Ok(JSValue());
    }
    if (std::holds_alternative<JSArrayPtr>(recv)) {
      if (auto &array = std::get<JSArrayPtr>(recv)->array; arrayIndex.value() < array.size()) {
        auto v = array[arrayIndex.value()];
        return Ok(std::move(v));
      }
      return Ok(JSValue());
    }
  }
  auto key = toWTF8(toString(index));
  return findProperty(env, recv, key);
}

static JSResult assignImpl(const Node &left, JSValue &&right, const std::shared_ptr<JSEnv> &env) {
  if (std::holds_alternative<NameExpr>(left.value)) {
    auto &nameExpr = std::get<NameExpr>(left.value);
    if (!env->assign(nameExpr.name, right)) {
      JSString str;
      toUTF16(nameExpr.name, str);
      str += u" is not defined";
      return throwError(env, builtin::REF_ERROR, std::move(str));
    }
    return Ok(std::move(right));
  }
  if (std::holds_alternative<AccessExpr>(left.value)) {
    auto &accessExpr = std::get<AccessExpr>(left.value);
    auto recv = TRY(evaluate(*accessExpr.recv, env));
    return assignProperty(env, recv, accessExpr.name, std::move(right));
  }

  // recv[index] = right
  auto &indexExpr = std::get<IndexExpr>(left.value);
  auto recv = TRY(evaluate(*indexExpr.recv, env));
  auto index = TRY(evaluate(*indexExpr.index, env));
  if (auto arrayIndex = toArrayIndex(index);
      arrayIndex && std::holds_alternative<JSArrayPtr>(recv)) {
    auto &array = std::get<JSArrayPtr>(recv)->array;
    if (arrayIndex.value() >= array.size()) {
      array.resize(arrayIndex.value() + 1, JSValue());
    }
    array[arrayIndex.value()] = right;
    return Ok(std::move(right));
  }
  auto key = toWTF8(toString(index));
  return assignProperty(env, recv, key, std::move(right));
}

static JSResult evalAssign(const AssignExpr &assign, const std::shared_ptr<JSEnv> &env) {
  switch (assign.op) {
  case JSTokenKind::ASSIGN: {
    auto right = TRY(evaluate(*assign.right, env));
    return assignImpl(*assign.left, std::move(right), env);
  }
  case JSTokenKind::INC:
  case JSTokenKind::DEC: {
    double delta = assign.op == JSTokenKind::INC ? 1 : -1;
    if (assign.left) { // left++, left--
      assert(!assign.right);
      auto left = TRY(evaluate(*assign.left, env));
      const auto oldValue = toNumber(left);
      TRY(assignImpl(*assign.left, oldValue + delta, env));
      return Ok(oldValue);
    }
    // ++right, --right
    assert(assign.right);
    auto left = TRY(evaluate(*assign.right, env));
    const auto newValue = toNumber(left) + delta;
    TRY(assignImpl(*assign.right, newValue, env));
    return Ok(newValue);
  }
  default:
    break;
  }
  auto left = TRY(evaluate(*assign.left, env));
  auto right = TRY(evaluate(*assign.right, env));
  switch (assign.op) {
  case JSTokenKind::ADD_ASSIGN:
    if (std::holds_alternative<JSStringPtr>(left) || std::holds_alternative<JSStringPtr>(right)) {
      JSString str;
      toString(left, str);
      toString(right, str);
      right = std::make_shared<JSString>(std::move(str));
    } else {
      right = toNumber(left) + toNumber(right);
    }
    break;
  case JSTokenKind::SUB_ASSIGN:
    right = toNumber(left) - toNumber(right);
    break;
  case JSTokenKind::MOD_ASSIGN:
    right = jsRemainder(toNumber(left), toNumber(right));
    break;
  default:
    fatal("unsupported assign: %s\n", toString(assign.op));
  }
  return assignImpl(*assign.left, std::move(right), env);
}

static JSResult defineVar(VarDecl::Kind kind, const std::string &name, JSValue &&value,
                          unsigned int lineNum, const std::shared_ptr<JSEnv> &env) {
  auto targetEnv = env;
  if (kind == VarDecl::Kind::VAR) {
    targetEnv = targetEnv->findGlobalOrFuncEnv();
  }
  if (!targetEnv->define(name, value)) {
    if (kind == VarDecl::Kind::VAR) {
      targetEnv->assign(name, std::move(value));
    } else { // TODO: should be syntax error
      JSString message = u"'";
      toUTF16(name, message);
      message += u"' is already defined";
      return throwError(env, builtin::TYPE_ERROR, lineNum, std::move(message));
    }
  }
  return Ok(JSValue());
}

static JSResult evalBlockWithCurrentEnv(const BlockStmt &block, const std::shared_ptr<JSEnv> &env) {
  for (auto &node : block.nodes) {
    TRY(evaluate(*node, env));
  }
  return Ok(JSValue());
}

static JSResult evalBlock(const BlockStmt &block, const std::shared_ptr<JSEnv> &env) {
  return evalBlockWithCurrentEnv(block, env->createChild());
}

static JSResult evalIf(const IfStmt &ifStmt, const std::shared_ptr<JSEnv> &env) {
  if (auto cond = TRY(evaluate(*ifStmt.cond, env)); toBool(cond)) {
    TRY(evaluate(*ifStmt.thenStmt, env));
  } else if (ifStmt.elseStmt) {
    TRY(evaluate(*ifStmt.elseStmt, env));
  }
  return Ok(JSValue());
}

static JSResult evalFor(const ForStmt &forStmt, const std::shared_ptr<JSEnv> &env) {
  auto loopInitEnv = env->createChild();
  if (forStmt.init) {
    TRY(evaluate(*forStmt.init, loopInitEnv));
  }
  while (!forStmt.cond || toBool(TRY(evaluate(*forStmt.cond, loopInitEnv)))) {
    if (forStmt.body) {
      auto loopEnv = loopInitEnv->createChild();
      JSResult ret;
      if (auto &e = forStmt.body->value; std::holds_alternative<BlockStmt>(e)) {
        ret = evalBlockWithCurrentEnv(std::get<BlockStmt>(e), loopEnv);
      } else {
        ret = evaluate(*forStmt.body, loopEnv);
      }
      switch (ret.status) {
      case JSResult::Status::OK:
        break;
      case JSResult::Status::ERR:
      case JSResult::Status::RETURN:
        return ret;
      case JSResult::Status::BREAK:
        goto BREAK;
      case JSResult::Status::CONTINUE:
        break;
      }
    }
    if (forStmt.after) {
      TRY(evaluate(*forStmt.after, loopInitEnv));
    }
  }
BREAK:
  return Ok(JSValue());
}

static std::function<std::optional<JSValue>()> toIter(const JSValue &value) {
  if (std::holds_alternative<JSStringPtr>(value)) {
    return [index = static_cast<size_t>(0),
            str = std::get<JSStringPtr>(value)]() mutable -> std::optional<JSValue> {
      if (index < str->size()) {
        JSString sub;
        auto ch = (*str)[index++]; // NOLINT
        sub += ch;
        if (UnicodeUtil::isHighSurrogate(ch) && index < str->size()) {
          sub += (*str)[index++]; // NOLINT
        }
        return std::make_shared<JSString>(std::move(sub));
      }
      return {};
    };
  }
  return [index = static_cast<size_t>(0),
          array = std::get<JSArrayPtr>(value)]() mutable -> std::optional<JSValue> {
    if (index < array->array.size()) {
      auto v = array->array[index++];
      return v;
    }
    return {};
  };
}

static JSResult evalForOf(const ForOfStmt &forOfStmt, unsigned int lineNum,
                          const std::shared_ptr<JSEnv> &env) {
  auto loopInitEnv = env->createChild();
  auto &decl = std::get<VarDecl>(forOfStmt.iter->value);
  auto iterable = TRY(evaluate(*decl.expr, loopInitEnv));
  if (!std::holds_alternative<JSStringPtr>(iterable) &&
      !std::holds_alternative<JSArrayPtr>(iterable)) {
    JSString str;
    toPrettyString(iterable, str);
    str += u" is not iterable";
    return throwError(loopInitEnv, builtin::TYPE_ERROR, std::move(str));
  }
  for (auto iter = toIter(iterable);;) {
    auto next = iter();
    if (!next) {
      break;
    }
    auto loopEnv = loopInitEnv->createChild();
    TRY(defineVar(decl.kind, decl.name, std::move(next.value()), lineNum, loopEnv));
    if (forOfStmt.body) {
      JSResult ret;
      if (auto &e = forOfStmt.body->value; std::holds_alternative<BlockStmt>(e)) {
        ret = evalBlockWithCurrentEnv(std::get<BlockStmt>(e), loopEnv);
      } else {
        ret = evaluate(*forOfStmt.body, loopEnv);
      }
      switch (ret.status) {
      case JSResult::Status::OK:
        break;
      case JSResult::Status::ERR:
      case JSResult::Status::RETURN:
        return ret;
      case JSResult::Status::BREAK:
        goto BREAK;
      case JSResult::Status::CONTINUE:
        break;
      }
    }
  }
BREAK:
  return Ok(JSValue());
}

static JSResult evalTry(const TryStmt &tryStmt, const std::shared_ptr<JSEnv> &env) {
  auto ret = evaluate(*tryStmt.tryBlock, env);
  if (ret.status == JSResult::Status::ERR && tryStmt.catchBlock) {
    auto catchEnv = env->createChild();
    if (!tryStmt.except.empty()) {
      catchEnv->define(tryStmt.except, ret.value);
    }
    ret = evalBlockWithCurrentEnv(std::get<BlockStmt>(tryStmt.catchBlock->value), catchEnv);
  }
  if (tryStmt.finallyBlock) {
    TRY(evaluate(*tryStmt.finallyBlock, env));
  }
  return ret;
}

static JSResult evaluate(const Node &node, const std::shared_ptr<JSEnv> &env) {
  return std::visit(
      [env, lineNum = node.lineNum](auto &&element) -> JSResult {
        using T = std::decay_t<decltype(element)>;
        if constexpr (std::is_same_v<T, NullLiteral>) {
          return Ok(nullptr);
        } else if constexpr (std::is_same_v<T, BoolLiteral> || std::is_same_v<T, NumberLiteral> ||
                             std::is_same_v<T, StringLiteral> || std::is_same_v<T, RegexLiteral>) {
          return Ok(element.value);
        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
          return evalArray(element, env);
        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
          return evalObject(element, env);
        } else if constexpr (std::is_same_v<T, FuncLiteral>) {
          return evalFunc(element, env);
        } else if constexpr (std::is_same_v<T, NameExpr>) {
          if (auto *v = env->find(element.name)) {
            return Ok(JSValue(*v));
          }
          JSString message;
          toUTF16(element.name, message);
          message += u" is not defined";
          return throwError(env, builtin::REF_ERROR, lineNum, std::move(message));
        } else if constexpr (std::is_same_v<T, AccessExpr>) {
          auto recv = TRY(evaluate(*element.recv, env));
          return findProperty(env, lineNum, recv, element.name);
        } else if constexpr (std::is_same_v<T, IndexExpr>) {
          return evalIndex(element, env);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          return evalCallExpr(element, lineNum, env);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          return evalUnary(element, env);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          return evalBinary(element, env);
        } else if constexpr (std::is_same_v<T, AssignExpr>) {
          return evalAssign(element, env);
        } else if constexpr (std::is_same_v<T, VarDecl>) {
          JSValue value;
          if (element.expr) {
            value = TRY(evaluate(*element.expr, env));
          }
          return defineVar(element.kind, element.name, std::move(value), lineNum, env);
        } else if constexpr (std::is_same_v<T, JumpStmt>) {
          JSValue ret;
          if (auto &n = element.expr) {
            ret = TRY(evaluate(*n, env));
          }
          return JSResult{element.status, std::move(ret)};
        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          return evalBlock(element, env);
        } else if constexpr (std::is_same_v<T, TryStmt>) {
          return evalTry(element, env);
        } else if constexpr (std::is_same_v<T, IfStmt>) {
          return evalIf(element, env);
        } else if constexpr (std::is_same_v<T, ForStmt>) {
          return evalFor(element, env);
        } else if constexpr (std::is_same_v<T, ForOfStmt>) {
          return evalForOf(element, lineNum, env);
        } else {
          fatal("unreachable");
        }
      },
      node.value);
}

JSResult jsEval(const char *sourceName, StringRef source, std::shared_ptr<JSEnv> global,
                const bool debug, std::string *syntaxErr) {
  if (!global) {
    global = initJSEnv();
  }
  std::vector<std::unique_ptr<Node>> nodes;
  {
    auto fileName = newJSStringPtr(sourceName);
    if (!global->define(JSEnv::DEFINED_FILENAME, fileName)) {
      global->assign(JSEnv::DEFINED_FILENAME, fileName);
    }
    JSLexer lexer(sourceName, source);
    lexer.setVerbose(debug);
    JSParser parser(global, lexer);
    while (parser) {
      if (auto node = parser()) {
        nodes.push_back(std::move(node));
      } else if (auto error = parser.formatError(); error.has_value()) {
        if (syntaxErr) {
          *syntaxErr = std::move(error.value().detail);
        }
        JSString message;
        toUTF16(error.value().message, message);
        return throwError(global, builtin::SYNTAX_ERROR, error.value().lineNum, std::move(message));
      }
    }
  }
  JSValue last;
  for (auto &node : nodes) {
    last = TRY(evaluate(*node, global));
  }
  return Ok(std::move(last));
}

std::string formatEvalResult(const std::shared_ptr<JSEnv> &env, const JSResult &result) {
  JSString out;
  auto &v = result.value;
  if (!result) {
    out += u"[uncaught]\n";
  }
  if (auto ret = isInstanceOf(env, 0, v, env->findGlobalEnv()->findOrUndef(builtin::ERROR));
      ret && std::get<bool>(ret.value)) {
    if (auto r = findProperty(env, 1, v, "name")) {
      toPrettyString(r.value, out);
    }
    if (auto r = findProperty(env, 1, v, "message")) {
      out += u": ";
      toPrettyString(r.value, out);
    }
    if (auto r = findProperty(env, 1, v, "fileName")) {
      out += u"\n    at ";
      toPrettyString(r.value, out);
      out += u':';
      r = findProperty(env, 1, v, "lineNumber");
      if (r) {
        toPrettyString(r.value, out);
      }
    }
  } else {
    toPrettyString(v, out);
  }
  return toWTF8(out);
}

} // namespace arsh::re262