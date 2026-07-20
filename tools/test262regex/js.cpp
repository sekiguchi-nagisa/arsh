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

static bool isInteger(double d) { return d == std::floor(d); }

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
  while (v) {
    tmp += u"0123456789abcdefghijklmnopqrstuvwxyz"[v % radix];
    v /= radix;
  }
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
    } else if (isInteger(d)) {
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
    for (unsigned int i = 0; i < array->array.size(); i++) {
      if (i > 0) {
        out += u',';
      }
      out += u' ';
      toPrettyString(array->array[i], out, op);
    }
    if (array->values.size() && array->array.size()) {
      out += u',';
    }
    unsigned int count = 0;
    for (auto &[k, v] : array->values) {
      if (count++ > 0) {
        out += u',';
      }
      out += u' ';
      toUTF16(k, out);
      out += u": ";
      toPrettyString(v, out, op);
    }
    if (array->array.size() || array->values.size()) {
      out += u' ';
    }
    out += u']';
  } else if (std::holds_alternative<JSObjectPtr>(value) &&
             std::get<JSObjectPtr>(value)->values.size()) {
    auto &obj = std::get<JSObjectPtr>(value);
    out += u'{';
    unsigned int count = 0;
    for (auto &[k, v] : obj->values) {
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

JSResult callJSFunction(const std::shared_ptr<JSEnv> &caller, unsigned int callerLineNum,
                        const JSFunctionPtr &func, JSValue &&recv, std::vector<JSValue> &&args) {
  auto funcEnv = func->definedEnv.lock()->createChild();
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

JSString typeOf(const JSValue &value) {
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
  func->values["name"] = newJSString(name);
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
    v = newJSString("");
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
  prototype->values["name"] = newJSString(builtin::ERROR);
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
  prototype->values["name"] = newJSString(name);
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
    if (auto v = toNumber(env->findOrUndef(func->params[0])); !std::isnan(v)) {
      auto index = static_cast<ssize_t>(v);
      if (index < 0) {
        startIndex = std::max<ssize_t>(index + static_cast<ssize_t>(thisStr.size()), 0);
      } else {
        startIndex = std::min(static_cast<size_t>(index), thisStr.size());
      }
    }
    size_t endIndex = thisStr.size();
    if (auto v = toNumber(env->findOrUndef(func->params[1])); !std::isnan(v)) {
      auto index = static_cast<ssize_t>(v);
      if (index < 0) {
        endIndex = std::max<ssize_t>(index + static_cast<ssize_t>(thisStr.size()), 0);
      } else {
        endIndex = std::min(static_cast<size_t>(index), thisStr.size());
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

static void defineString(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    auto thing = env->findOrUndef(func->params[0]); // TODO: new String
    return Ok(std::make_shared<JSString>(toString(thing)));
  };
  auto prototype = std::make_shared<JSObject>();
  prototype->values["match"] = createStringMatch(global);
  prototype->values["slice"] = createStringSlice(global);
  auto func =
      createJSFunction(global, builtin::STRING, {"thing"}, std::move(prototype), std::move(impl));
  global->define(builtin::STRING, std::move(func));
}

static JSFunctionPtr createNumberToString(const std::shared_ptr<JSEnv> &global) {
  auto impl = [](const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    unsigned char radix = 10;
    if (auto v = env->findOrUndef(func->params[0]); !isUndefined(v)) {
      double num = toNumber(v);
      if (!isInteger(num) || num < 2 || num > 36) {
        return throwError(env, builtin::RANGE_ERROR, u"toString() radix argument must be 2~36");
      }
      radix = static_cast<unsigned char>(num);
    }
    double value = std::get<double>(env->findOrUndef(builtin::THIS));
    if (!isInteger(value) && radix != 10) { // TODO: radix for float
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
  global->define(builtin::NUMBER, std::move(func));

  // number constant
  global->define("NaN", std::nan(""));
  global->define("Infinity", INFINITY);
}

std::shared_ptr<JSEnv> initJSEnv() {
  auto global = JSEnv::createGlobal();
  global->define("undefined", JSValue());
  defineError(global);
  defineDerivedError(global, builtin::SYNTAX_ERROR);
  defineDerivedError(global, builtin::TYPE_ERROR);
  defineDerivedError(global, builtin::REF_ERROR);
  defineDerivedError(global, builtin::RANGE_ERROR);
  defineString(global);
  defineNumber(global);
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

struct CallExpr {
  std::unique_ptr<Node> func;
  std::vector<std::unique_ptr<Node>> args;
  bool newExpr{false};
};

struct UnaryExpr {
  std::string op;
  std::unique_ptr<Node> expr;
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

struct TryStmt {
  std::vector<std::unique_ptr<Node>> tryBlock;
  bool hasCatch;
  std::string except; // for caught exception (maybe empty)
  std::vector<std::unique_ptr<Node>> catchBlock;
  std::vector<std::unique_ptr<Node>> finallyBlock;
};

struct Node {
  unsigned int lineNum;

  using Underlying = std::variant<NullLiteral, BoolLiteral, NumberLiteral, StringLiteral,
                                  RegexLiteral, ArrayLiteral, ObjectLiteral, FuncLiteral, NameExpr,
                                  AccessExpr, CallExpr, UnaryExpr, VarDecl, JumpStmt, TryStmt>;
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
  EACH_LA_JS_PRIMARY(OP)

#define EACH_LA_JS_STATEMENT(OP)                                                                   \
  OP(CONST)                                                                                        \
  OP(LET)                                                                                          \
  OP(VAR)                                                                                          \
  OP(RETURN)                                                                                       \
  OP(THROW)                                                                                        \
  OP(TRY)                                                                                          \
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

  /**
   *
   * @param nodes
   * @return return always null
   */
  std::unique_ptr<Node> parseBlock(std::vector<std::unique_ptr<Node>> &nodes);

  std::unique_ptr<Node> parseTryStatement();

  std::unique_ptr<Node> parseExpression();

  std::unique_ptr<Node> parseUnaryExpression();

  std::unique_ptr<Node> parseCallExpression();

  std::unique_ptr<Node> parseMemberExpression();

  std::unique_ptr<Node> parseWithArguments(std::unique_ptr<Node> &&node);

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

  Error err = {.sourceName = this->lexer->getSourceName(),
               .lineNum = lineNum,
               .message = this->getError().getMessage(),
               .detail = std::move(str)};
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
  case JSTokenKind::CONST:
  case JSTokenKind::LET:
  case JSTokenKind::VAR: {
    const auto kind = toVarKind(this->curKind);
    this->consume();
    Token token = TRY(this->expectVarDeclIdentifier());
    TRY(this->expect(JSTokenKind::ASSIGN));
    auto expr = TRY(this->parseExpression());
    TRY(this->expect(JSTokenKind::LINE_END));
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
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

std::unique_ptr<Node> JSParser::parseBlock(std::vector<std::unique_ptr<Node>> &nodes) {
  TRY(this->expect(JSTokenKind::LBC));
  while (this->curKind != JSTokenKind::RBC) {
    auto node = TRY(this->parseStatement());
    nodes.push_back(std::move(node));
  }
  TRY(this->expect(JSTokenKind::RBC));
  return nullptr;
}

std::unique_ptr<Node> JSParser::parseTryStatement() {
  Token token = TRY(this->expect(JSTokenKind::TRY));
  std::vector<std::unique_ptr<Node>> tryBlock;
  TRY(this->parseBlock(tryBlock));
  bool foundCatch = false;
  std::string except;
  std::vector<std::unique_ptr<Node>> catchBlock;
  if (this->curKind == JSTokenKind::CATCH) {
    TRY(this->expect(JSTokenKind::CATCH));
    if (this->curKind == JSTokenKind::LP) {
      TRY(this->expect(JSTokenKind::LP));
      except = this->lexer->toTokenText(TRY(this->expectVarDeclIdentifier()));
      TRY(this->expect(JSTokenKind::RP));
    }
    TRY(this->parseBlock(catchBlock));
    foundCatch = true;
  }
  std::vector<std::unique_ptr<Node>> finallyBlock;
  if (this->curKind == JSTokenKind::FINALLY) {
    TRY(this->expect(JSTokenKind::FINALLY));
    TRY(this->parseBlock(finallyBlock));
  } else if (!foundCatch) {
    E_ALTER(JSTokenKind::CATCH, JSTokenKind::FINALLY);
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                TryStmt{.tryBlock = std::move(tryBlock),
                                        .hasCatch = foundCatch,
                                        .except = std::move(except),
                                        .catchBlock = std::move(catchBlock),
                                        .finallyBlock = std::move(finallyBlock)});
}

std::unique_ptr<Node> JSParser::parseExpression() { return this->parseUnaryExpression(); }

std::unique_ptr<Node> JSParser::parseUnaryExpression() {
  switch (this->curKind) {
  case JSTokenKind::NOT:
  case JSTokenKind::ADD:
  case JSTokenKind::SUB:
  case JSTokenKind::VOID:
  case JSTokenKind::TYPEOF: {
    Token token = this->curToken;
    this->consume();
    auto expr = TRY(this->parseUnaryExpression());
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  UnaryExpr{this->lexer->toTokenText(token), std::move(expr)});
  }
  case JSTokenKind::NEW:
    return this->parseMemberExpression();
  default:
    return this->parseCallExpression();
  }
}

std::unique_ptr<Node> JSParser::parseCallExpression() {
  auto node = TRY(this->parsePrimary());
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
    case JSTokenKind::LP:
      node = TRY(this->parseWithArguments(std::move(node)));
      continue;
    case JSTokenKind::LB: // TODO: index
    default:
      goto END;
    }
  }
END:
  return node;
}

std::unique_ptr<Node> JSParser::parseMemberExpression() {
  TRY(this->expect(JSTokenKind::NEW));
  std::unique_ptr<Node> node;
  if (this->curKind == JSTokenKind::NEW) {
    node = TRY(this->parseMemberExpression());
  } else {
    node = TRY(this->parsePrimary());
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
      case JSTokenKind::LB: // TODO: index
      default:
        goto END;
      }
    }
  }
END:
  node = TRY(this->parseWithArguments(std::move(node)));
  assert(std::holds_alternative<CallExpr>(node->value));
  std::get<CallExpr>(node->value).newExpr = true;
  return node;
}

std::unique_ptr<Node> JSParser::parseWithArguments(std::unique_ptr<Node> &&node) {
  TRY(this->expect(JSTokenKind::LP));
  CallExpr call;
  unsigned int lineNum = node->lineNum;
  call.func = std::move(node);
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
  JSArrayPtr array = std::make_shared<JSArray>();
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
  if (unary.op == "!") {
    value = !toBool(value);
  } else if (unary.op == "+") {
    value = toNumber(value);
  } else if (unary.op == "-") {
    value = -toNumber(value);
  } else if (unary.op == "void") {
    value = JSValue(); // always `undefined`
  } else if (unary.op == "typeof") {
    value = std::make_shared<JSString>(typeOf(value));
  }
  return Ok(std::move(value));
}

static JSResult evalBlock(const std::vector<std::unique_ptr<Node>> &nodes,
                          const std::shared_ptr<JSEnv> &env) {
  for (auto &node : nodes) {
    TRY(evaluate(*node, env));
  }
  return Ok(JSValue());
}

static JSResult evalBlockWithNewEnv(const std::vector<std::unique_ptr<Node>> &nodes,
                                    const std::shared_ptr<JSEnv> &env) {
  return evalBlock(nodes, env->createChild());
}

static JSResult evalTry(const TryStmt &tryStmt, const std::shared_ptr<JSEnv> &env) {
  auto ret = evalBlockWithNewEnv(tryStmt.tryBlock, env);
  if (ret.status == JSResult::Status::ERR && tryStmt.hasCatch) {
    auto catchEnv = env->createChild();
    if (!tryStmt.except.empty()) {
      catchEnv->define(tryStmt.except, ret.value);
    }
    ret = evalBlock(tryStmt.catchBlock, catchEnv);
  }
  if (!tryStmt.finallyBlock.empty()) {
    TRY(evalBlockWithNewEnv(tryStmt.finallyBlock, env));
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
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          return evalCallExpr(element, lineNum, env);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          return evalUnary(element, env);
        } else if constexpr (std::is_same_v<T, VarDecl>) {
          auto value = TRY(evaluate(*element.expr, env));
          if (!env->define(element.name, std::move(value))) { // TODO: should be syntax error
            JSString message = u"'";
            toUTF16(element.name, message);
            message += u"' is already defined";
            return throwError(env, builtin::TYPE_ERROR, lineNum, std::move(message));
          }
          return Ok(JSValue());
        } else if constexpr (std::is_same_v<T, JumpStmt>) {
          JSValue ret;
          if (auto &n = element.expr) {
            ret = TRY(evaluate(*n, env));
          }
          return JSResult{element.status, std::move(ret)};
        } else if constexpr (std::is_same_v<T, TryStmt>) {
          return evalTry(element, env);
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
    auto fileName = newJSString(sourceName);
    if (!global->define(JSEnv::DEFINED_FILENAME, fileName)) {
      global->assign(JSEnv::DEFINED_FILENAME, fileName);
    }
    JSLexer lexer(sourceName, source);
    lexer.setVerbose(debug);
    JSParser parser(global, lexer);
    while (parser) {
      if (auto node = parser()) {
        nodes.push_back(std::move(node));
      } else {
        auto error = parser.formatError();
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
      out += u":";
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