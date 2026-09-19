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
#include "js_regex.h"

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

static JSProperty getOwnProperty(const JSArray &recv, const std::string &name) {
  if (name == "length") {
    return {JSPropertyAttr::WRITABLE, static_cast<double>(recv.array.size())};
  }
  return recv.getProperty(name);
}

static JSProperty getOwnProperty(const JSString &recv, const std::string &name) {
  if (name == "length") {
    return {JSPropertyAttr::NONE, static_cast<double>(recv.size())};
  }
  return {};
}

JSProperty findOwnProperty(const JSValue &recv, const std::string &name) {
  return std::visit(
      [name](auto &&element) -> JSProperty {
        using T = std::decay_t<decltype(element)>;
        if constexpr (std::is_same_v<T, JSFunctionPtr> || std::is_same_v<T, JSObjectPtr>) {
          return element->getProperty(name);
        } else if constexpr (std::is_same_v<T, JSRegexPtr> || std::is_same_v<T, JSArrayPtr> ||
                             std::is_same_v<T, JSStringPtr>) {
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
    actualRecv =
        std::move(std::get<JSFunctionPtr>(actualRecv)->getProperty(builtin::PROTOTYPE).value);
  } else if (std::holds_alternative<double>(recv)) {
    actualRecv = env->findGlobalEnv()->findOrUndef(builtin::NUMBER);
    actualRecv =
        std::move(std::get<JSFunctionPtr>(actualRecv)->getProperty(builtin::PROTOTYPE).value);
  }
  JSValue ret;
  const bool proto = name == builtin::PROTO;
  while (!isUndefined(actualRecv)) {
    auto p = findOwnProperty(actualRecv, name);
    if (!isUndefined(p.value) || proto) {
      ret = std::move(p.value);
      break;
    }
    actualRecv = std::move(findOwnProperty(actualRecv, builtin::PROTO).value);
  }
  return Ok(std::move(ret));
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

JSProperty findOwnPropertyByIndex(const JSValue &recv, const JSValue &index) {
  if (auto arrayIndex = toArrayIndex(index)) {
    if (std::holds_alternative<JSStringPtr>(recv)) {
      if (auto &str = *std::get<JSStringPtr>(recv); arrayIndex.value() < str.size()) {
        JSString ret;
        ret += str[arrayIndex.value()];
        return {JSPropertyAttr::ENUMERABLE, std::make_shared<JSString>(std::move(ret))};
      }
      return {};
    }
    if (std::holds_alternative<JSArrayPtr>(recv)) {
      if (auto &array = std::get<JSArrayPtr>(recv)->array; arrayIndex.value() < array.size()) {
        auto v = array[arrayIndex.value()];
        return JSProperty::withDefault(std::move(v));
      }
      return {};
    }
  }
  auto key = toWTF8(toString(index));
  return findOwnProperty(recv, key);
}

JSResult findPropertyByIndex(const std::shared_ptr<JSEnv> &env, const JSValue &recv,
                             const JSValue &index) {
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

JSResult assignProperty(const std::shared_ptr<JSEnv> &env, unsigned int callerLineNum,
                        const JSValue &recv, const std::string &name, JSValue &&value) {
  return std::visit(
      [&](auto &&element) -> JSResult {
        using T = std::decay_t<decltype(element)>;
        if constexpr (std::is_same_v<T, JSFunctionPtr> || std::is_same_v<T, JSObjectPtr> ||
                      std::is_same_v<T, JSArrayPtr>) {
          element->setProperty(name, JSValue(value));
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

JSResult assignPropertyByIndex(const std::shared_ptr<JSEnv> &env, const JSValue &recv,
                               const JSValue &index, JSValue &&value) {
  if (auto arrayIndex = toArrayIndex(index);
      arrayIndex && std::holds_alternative<JSArrayPtr>(recv)) {
    auto &array = std::get<JSArrayPtr>(recv)->array;
    if (arrayIndex.value() >= array.size()) {
      array.resize(arrayIndex.value() + 1, JSValue());
    }
    array[arrayIndex.value()] = value;
    return Ok(std::move(value));
  }
  auto key = toWTF8(toString(index));
  return assignProperty(env, recv, key, std::move(value));
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
    out += *std::get<JSStringPtr>(std::get<JSFunctionPtr>(value)->values.at("name").value);
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
      toPrettyString(v.value, out, op);
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
      toPrettyString(v.value, out, op);
    }
    out += u" }";
  } else {
    out += u"{}";
  }
}

void toString(const JSValue &value, std::u16string &out) {
  if (std::holds_alternative<JSFunctionPtr>(value)) {
    out += u"function ";
    out += *std::get<JSStringPtr>(std::get<JSFunctionPtr>(value)->values.at("name").value);
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

bool toBool(const JSValue &value) {
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

// for builtin
JSFunctionPtr createJSFunction(const std::shared_ptr<JSEnv> &env, const char *name,
                               std::vector<std::string> &&params, JSObjectPtr &&prototype,
                               JSFunction::Impl &&impl) {
  auto func = std::make_shared<JSFunction>();
  func->params = std::move(params);
  func->definedEnv = env;
  func->setProperty("name", JSPropertyAttr::CONFIGURABLE, newJSStringPtr(name));
  func->setProperty("length", JSPropertyAttr::CONFIGURABLE,
                    static_cast<double>(func->params.size()));
  if (prototype) {
    func->setBuiltinProperty(builtin::PROTOTYPE, std::move(prototype));
  }
  func->impl = std::move(impl);
  return func;
}

JSObjectPtr newObject(const JSFunctionPtr &func) {
  auto obj = std::make_shared<JSObject>();
  if (auto prototype = func->getProperty(builtin::PROTOTYPE);
      prototype && !isUndefined(prototype.value)) {
    obj->setBuiltinProperty(builtin::PROTO, std::move(std::move(prototype.value)));
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
  obj->setBuiltinProperty(func->params[0], std::move(v));

  // fileName
  v = env->findOrUndef(func->params[1]);
  if (isUndefined(v)) {
    v = env->findOrUndef(JSEnv::CALLER_FILENAME);
  }
  obj->setBuiltinProperty(func->params[1], std::move(v));

  // lineNumber
  v = env->findOrUndef(func->params[2]);
  if (isUndefined(v)) {
    v = env->findOrUndef(JSEnv::CALLER_LINENO);
  }
  obj->setBuiltinProperty(func->params[2], std::move(v));
  return Ok(obj);
}

static void defineError(const std::shared_ptr<JSEnv> &global) {
  auto prototype = std::make_shared<JSObject>();
  prototype->setBuiltinProperty("name", newJSStringPtr(builtin::ERROR));
  auto func = createJSFunction(global, builtin::ERROR, {"message", "fileName", "lineNumber"},
                               std::move(prototype), errorConstructorImpl);
  global->define(builtin::ERROR, std::move(func));
}

void defineDerivedError(const std::shared_ptr<JSEnv> &global, const char *name) {
  auto errorConstructor = global->findOrUndef(builtin::ERROR);
  assert(std::holds_alternative<JSFunctionPtr>(errorConstructor));
  auto errorPrototype =
      std::get<JSFunctionPtr>(errorConstructor)->getProperty(builtin::PROTOTYPE).value;
  auto prototype = std::make_shared<JSObject>();
  prototype->setBuiltinProperty("name", newJSStringPtr(name));
  prototype->setBuiltinProperty(builtin::PROTO, std::move(errorPrototype));
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
  obj->setBuiltinProperty("log",
                          createJSFunction(global, "log", {"message"}, nullptr, std::move(impl)));
  global->define("console", std::move(obj));
}

static JSFunction::Impl regexpSymbolOp(const char *op) {
  return [symbol = std::string(op)](const JSFunctionPtr &func,
                                    const std::shared_ptr<JSEnv> &env) -> JSResult {
    JSRegexPtr regex;
    if (auto arg = env->findOrUndef(func->params[0]); std::holds_alternative<JSRegexPtr>(arg)) {
      regex = std::get<JSRegexPtr>(arg);
    } else {
      auto regexConstructor = env->findGlobalEnv()->findOrUndef(builtin::REGEXP);
      auto ret = TRY(callJSFunction(env, env->callerLineNum(),
                                    std::get<JSFunctionPtr>(regexConstructor), nullptr, {arg}));
      regex = std::get<JSRegexPtr>(ret);
    }
    std::vector args = {env->findOrUndef(builtin::THIS)};
    for (unsigned int i = 1; i < func->params.size(); i++) {
      args.push_back(env->findOrUndef(func->params[i]));
    }
    auto matchFunc = TRY(findProperty(env, regex, symbol));
    return callJSFunction(env, env->callerLineNum(), std::get<JSFunctionPtr>(matchFunc), regex,
                          std::move(args));
  };
}

static JSFunctionPtr createStringMatch(const std::shared_ptr<JSEnv> &global) {
  return createJSFunction(global, "match", {"regexp"}, nullptr,
                          regexpSymbolOp(builtin::SYMBOL_MATCH));
}

static JSFunctionPtr createStringSearch(const std::shared_ptr<JSEnv> &global) {
  return createJSFunction(global, "search", {"regexp"}, nullptr,
                          regexpSymbolOp(builtin::SYMBOL_SEARCH));
}

static JSFunctionPtr createStringReplace(const std::shared_ptr<JSEnv> &global) {
  return createJSFunction(global, "replace", {"pattern", "replacement"}, nullptr,
                          regexpSymbolOp(builtin::SYMBOL_REPLACE));
}

static JSFunctionPtr createStringSplit(const std::shared_ptr<JSEnv> &global) {
  return createJSFunction(global, "split", {"separator", "limit"}, nullptr,
                          regexpSymbolOp(builtin::SYMBOL_SPLIT));
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
  prototype->setBuiltinProperty("match", createStringMatch(global));
  prototype->setBuiltinProperty("search", createStringSearch(global));
  prototype->setBuiltinProperty("replace", createStringReplace(global));
  prototype->setBuiltinProperty("split", createStringSplit(global));
  prototype->setBuiltinProperty("slice", createStringSlice(global));
  prototype->setBuiltinProperty("charAt", createStringCharAt(global));
  prototype->setBuiltinProperty("charCodeAt", createStringCharCodeAt(global));
  prototype->setBuiltinProperty("codePointAt", createStringCodePointAt(global));
  auto func =
      createJSFunction(global, builtin::STRING, {"thing"}, std::move(prototype), std::move(impl));
  func->setBuiltinProperty("fromCharCode", createStringFromCharCode(global));
  func->setBuiltinProperty("fromCodePoint", createStringFromCodePoint(global));
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
  prototype->setBuiltinProperty("toString", createNumberToString(global));
  auto func =
      createJSFunction(global, builtin::NUMBER, {"value"}, std::move(prototype), std::move(impl));
  func->setProperty("EPSILON", JSPropertyAttr::NONE, std::numeric_limits<double>::epsilon());
  func->setProperty("MAX_SAFE_INTEGER", JSPropertyAttr::NONE, MAX_SAFE_INTEGER);
  func->setProperty("MIN_SAFE_INTEGER", JSPropertyAttr::NONE, MIN_SAFE_INTEGER);
  func->setProperty("MAX_VALUE", JSPropertyAttr::NONE, std::numeric_limits<double>::max());
  func->setProperty("MIN_VALUE", JSPropertyAttr::NONE, std::numeric_limits<double>::min());
  func->setProperty("NaN", JSPropertyAttr::NONE, std::nan(""));
  func->setProperty("NEGATIVE_INFINITY", JSPropertyAttr::NONE, -INFINITY);
  func->setProperty("POSITIVE_INFINITY", JSPropertyAttr::NONE, INFINITY);
  global->define(builtin::NUMBER, std::move(func));
}

JSArrayPtr createJSArray(const std::shared_ptr<JSEnv> &env) {
  auto constructor = env->findGlobalEnv()->findOrUndef(builtin::ARRAY);
  auto prototype = std::get<JSFunctionPtr>(constructor)->getProperty(builtin::PROTOTYPE).value;
  auto array = std::make_shared<JSArray>();
  array->setBuiltinProperty(builtin::PROTO, std::move(prototype));
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
  prototype->setBuiltinProperty("push", createArrayPush(global));
  prototype->setBuiltinProperty("join", createArrayJoin(global));
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

} // namespace arsh::re262