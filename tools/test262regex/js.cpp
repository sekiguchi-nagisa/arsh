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

static JSValue findOwnProperty(const JSValue &recv, const std::string &name) {
  return std::visit(
      [name](auto &&element) -> JSValue {
        using T = std::decay_t<decltype(element)>;
        if constexpr (std::is_same_v<T, JSRegexPtr> || std::is_same_v<T, JSFunctionPtr> ||
                      std::is_same_v<T, JSObjectPtr>) {
          return getOwnProperty(*element, name);
        } else {
          return {};
        }
      },
      recv);
}

static Result<JSValue, JSThrown> findProperty(const std::shared_ptr<JSEnv> &env,
                                              unsigned int callerLineNum, const JSValue &recv,
                                              const std::string &name) {
  if (isUndefined(recv) || isNull(recv)) {
    std::string message = "Cannot read properties of ";
    toPrettyString(recv, message);
    message += " (reading '";
    message += name;
    message += "')";
    return throwError(env, builtin::TYPE_ERROR, callerLineNum, message);
  }
  JSValue actualRecv = recv;
  JSValue ret;
  const bool proto = name == builtin::PROTO;
  while (!isUndefined(actualRecv)) {
    ret = findOwnProperty(actualRecv, name);
    if (!isUndefined(ret) || proto) {
      break;
    }
    actualRecv = findOwnProperty(actualRecv, builtin::PROTO);
  }
  return Ok(ret);
}

std::u16string toUTF16(StringRef ref) {
  std::u16string value;
  const char *end = ref.end();
  for (const char *iter = ref.begin(); iter != end;) {
    int codePoint;
    if (unsigned int len = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint); len) {
      iter += len;
    } else { // put dummy
      iter++;
      codePoint = UnicodeUtil::REPLACEMENT_CHAR_CODE;
    }
    auto [high, low] = UnicodeUtil::codePointToUtf16(codePoint);
    value += high;
    if (high != low) {
      value += low;
    }
  }
  return value;
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

void toPrettyString(const JSValue &value, std::string &out) {
  if (isUndefined(value)) {
    out += "undefined";
  } else if (isNull(value)) {
    out += "null";
  } else if (std::holds_alternative<bool>(value)) {
    out += std::get<bool>(value) ? "true" : "false";
  } else if (std::holds_alternative<double>(value)) {
    auto d = std::get<double>(value);
    if (d == 0.0) {
      out += '0';
    } else if (std::isnan(d)) {
      out += "NaN";
    } else if (std::isinf(d)) {
      out += std::signbit(d) ? "-Infinity" : "Infinity";
    } else if (isInteger(d)) {
      out += std::to_string(static_cast<int64_t>(d));
    } else {
      out += std::to_string(d);
    }
  } else if (std::holds_alternative<JSStringPtr>(value)) {
    toWTF8(*std::get<JSStringPtr>(value), out);
  } else if (std::holds_alternative<JSRegexPtr>(value)) {
    out += toString(*std::get<JSRegexPtr>(value));
  } else if (std::holds_alternative<JSFunctionPtr>(value)) {
    out += "[Function: ";
    toWTF8(*std::get<JSStringPtr>(std::get<JSFunctionPtr>(value)->values.at("name")), out);
    out += ']';
  } else if (std::holds_alternative<JSArrayPtr>(value)) {
    auto &array = std::get<JSArrayPtr>(value);
    out += '[';
    for (unsigned int i = 0; i < array->values.size(); i++) {
      if (i > 0) {
        out += ',';
      }
      out += ' ';
      toPrettyString(array->values[i], out);
    }
    if (array->values.size()) {
      out += ' ';
    }
    out += ']';
  } else if (std::holds_alternative<JSObjectPtr>(value) &&
             std::get<JSObjectPtr>(value)->values.size()) {
    auto &obj = std::get<JSObjectPtr>(value);
    out += '{';
    unsigned int count = 0;
    for (auto &[k, v] : obj->values) {
      if (count++ > 0) {
        out += ',';
      }
      out += ' ';
      out += k;
      out += ": ";
      toPrettyString(v, out);
    }
    out += " }";
  } else {
    out += "{}";
  }
}

void toString(const JSValue &value, std::string &out) {
  if (isNull(value) || isUndefined(value)) { // do nothing
  } else if (std::holds_alternative<JSFunctionPtr>(value)) {
    out += "function ";
    toWTF8(*std::get<JSStringPtr>(std::get<JSFunctionPtr>(value)->values.at("name")), out);
    out += "() { [native code] }";
  } else if (std::holds_alternative<JSArrayPtr>(value)) {
    auto &array = std::get<JSArrayPtr>(value);
    for (unsigned int i = 0; i < array->values.size(); i++) {
      if (i > 0) {
        out += ',';
      }
      toString(array->values[i], out);
    }
  } else if (std::holds_alternative<JSObjectPtr>(value)) {
    out += "[object Object]";
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
        return ret.value;
      }
    }
  }
  return std::nan("");
}

static auto callJSFunction(const std::shared_ptr<JSEnv> &caller, unsigned int callerLineNum,
                           const JSFunctionPtr &func, JSValue &&recv, std::vector<JSValue> &&args) {
  auto funcEnv = func->definedEnv.lock()->createChild();
  assert(funcEnv);
  funcEnv->define(builtin::THIS, std::move(recv));
  funcEnv->define(JSEnv::CALLER_FILENAME, caller->findOrUndef(JSEnv::DEFINED_FILENAME));
  funcEnv->define(JSEnv::CALLER_LINENO, static_cast<double>(callerLineNum));
  const size_t maxArgs = std::max(func->params.size(), args.size());
  for (size_t i = 0; i < maxArgs; i++) {
    if (i < func->params.size() && i < args.size()) {
      funcEnv->define(func->params[i], std::move(args[i]));
    }
  }
  return func->impl(func, funcEnv);
}

ErrHolder<JSThrown> throwError(const std::shared_ptr<JSEnv> &env, const char *name,
                               unsigned int lineNum, const std::string &message) {
  auto v = env->findGlobalEnv()->findOrUndef(name);
  assert(std::holds_alternative<JSFunctionPtr>(v));
  auto func = std::get<JSFunctionPtr>(v);
  std::vector<JSValue> args;
  args.emplace_back(newJSString(message));
  if (auto fileName = env->findOrUndef(JSEnv::DEFINED_FILENAME); !isUndefined(fileName)) {
    args.emplace_back(fileName);
    if (lineNum) {
      args.emplace_back(static_cast<double>(lineNum));
    }
  }
  if (auto ret = callJSFunction(env, lineNum, func, JSValue(), std::move(args))) {
    return Err(JSThrown{std::move(ret.asOk())});
  } else {
    return Err(std::move(ret.asErr()));
  }
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

Result<JSValue, JSThrown> isInstanceOf(const std::shared_ptr<JSEnv> &env, unsigned int lineNum,
                                       const JSValue &value, const JSValue &constructor) {
  if (!std::holds_alternative<JSFunctionPtr>(constructor)) {
    return throwError(env, builtin::TYPE_ERROR, lineNum,
                      "Right-hand side of instanceof is not callable");
  }
  if (isUndefined(value) || isNull(value)) {
    return Ok(false);
  }

  const auto prototype = findProperty(env, lineNum, constructor, builtin::PROTOTYPE);
  if (!prototype || isUndefined(prototype.asOk()) || isNull(prototype.asOk())) {
    return Ok(false);
  }
  for (auto target = value;;) {
    auto proto = findProperty(env, lineNum, target, builtin::PROTO);
    if (!proto || isUndefined(proto.asOk()) || isNull(proto.asOk())) {
      return Ok(false);
    }
    if (strictlyEquals(proto.asOk(), prototype.asOk())) {
      break;
    }
    target = proto.asOk();
  }
  return Ok(true);
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

static Result<JSValue, JSThrown> errorConstructorImpl(const JSFunctionPtr &func,
                                                      const std::shared_ptr<JSEnv> &env) {
  JSObjectPtr obj;
  if (auto v = env->findOrUndef(builtin::THIS); std::holds_alternative<JSObjectPtr>(v)) {
    obj = std::get<JSObjectPtr>(v);
  } else {
    obj = std::make_shared<JSObject>();
  }
  env->assign(builtin::THIS, obj);
  obj->values[builtin::PROTO] = func->values.at(builtin::PROTOTYPE);
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

std::shared_ptr<JSEnv> initJSEnv() {
  auto global = JSEnv::createGlobal();
  global->define("undefined", JSValue());
  defineError(global);
  defineDerivedError(global, builtin::SYNTAX_ERROR);
  defineDerivedError(global, builtin::TYPE_ERROR);
  defineDerivedError(global, builtin::REF_ERROR);
  defineJSRegex(global);
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
};

struct UnaryExpr {
  std::string op;
  std::unique_ptr<Node> expr;
};

struct VarDecl { // currently only support `const`
  std::string name;
  std::unique_ptr<Node> expr;
};

struct Node {
  unsigned int lineNum;

  using Underlying =
      std::variant<NullLiteral, BoolLiteral, NumberLiteral, StringLiteral, RegexLiteral,
                   ArrayLiteral, ObjectLiteral, NameExpr, AccessExpr, CallExpr, UnaryExpr, VarDecl>;
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
  OP(LB)                                                                                           \
  OP(LBC)                                                                                          \
  OP(LP)

#define EACH_LA_JS_EXPRESSION(OP)                                                                  \
  OP(NOT)                                                                                          \
  EACH_LA_JS_PRIMARY(OP)

#define EACH_LA_JS_STATEMENT(OP)                                                                   \
  OP(CONST)                                                                                        \
  EACH_LA_JS_EXPRESSION(OP)

#define GEN_LA_CASE(CASE) case JSTokenKind::CASE:
#define GEN_LA_ALTER(CASE) JSTokenKind::CASE,

#define E_ALTER(...)                                                                               \
  do {                                                                                             \
    this->reportNoViableAlterError((JSTokenKind[]){__VA_ARGS__});                                  \
    return nullptr;                                                                                \
  } while (false)

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
  std::unique_ptr<Node> parseStatement();

  std::unique_ptr<Node> parseExpression();

  std::unique_ptr<Node> parseUnaryExpression();

  std::unique_ptr<Node> parseMemberExpression();

  std::unique_ptr<Node> parseWithArguments(std::unique_ptr<Node> &&node);

  std::unique_ptr<Node> parsePrimary();

  std::unique_ptr<Node> parseNumber();

  std::unique_ptr<Node> parseObject();

  std::unique_ptr<Node> parseArray();
};

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

std::unique_ptr<Node> JSParser::parseStatement() {
  switch (this->curKind) {
  case JSTokenKind::CONST: {
    this->consume();
    Token token = TRY(this->expect(JSTokenKind::IDENTIFIER));
    TRY(this->expect(JSTokenKind::ASSIGN));
    auto expr = TRY(this->parseExpression());
    TRY(this->expect(JSTokenKind::LINE_END));
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  VarDecl{this->lexer->toTokenText(token), std::move(expr)});
  }
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

std::unique_ptr<Node> JSParser::parseExpression() { return this->parseUnaryExpression(); }

std::unique_ptr<Node> JSParser::parseUnaryExpression() {
  switch (this->curKind) {
  case JSTokenKind::NOT: {
    Token token = this->expect(JSTokenKind::NOT);
    auto expr = TRY(this->parseUnaryExpression());
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  UnaryExpr{this->lexer->toTokenText(token), std::move(expr)});
  }
  default:
    return this->parseMemberExpression();
  }
}

std::unique_ptr<Node> JSParser::parseMemberExpression() {
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
    default:
      goto END;
    }
  }
END:
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
    assert(std::holds_alternative<JSObjectPtr>(prototype.asOk()));
    if (auto ret = createJSRegexFromLiteral(std::get<JSObjectPtr>(prototype.asOk()),
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

#undef TRY
#define TRY(...)                                                                                   \
  ({                                                                                               \
    auto v__ = (__VA_ARGS__);                                                                      \
    if (!v__) {                                                                                    \
      return v__;                                                                                  \
    }                                                                                              \
    std::move(v__.asOk());                                                                         \
  })

static Result<JSValue, JSThrown> evaluate(const Node &node, const std::shared_ptr<JSEnv> &env);

static Result<JSValue, JSThrown> evalArray(const ArrayLiteral &literal,
                                           const std::shared_ptr<JSEnv> &env) {
  JSArrayPtr array = std::make_shared<JSArray>();
  array->values.reserve(literal.values.size());
  for (auto &e : literal.values) {
    auto ret = TRY(evaluate(*e, env));
    array->values.push_back(std::move(ret));
  }
  return Ok(std::move(array));
}

static Result<JSValue, JSThrown> evalObject(const ObjectLiteral &literal,
                                            const std::shared_ptr<JSEnv> &env) {
  JSObjectPtr object = std::make_shared<JSObject>();
  for (auto &[k, v] : literal.values) {
    auto value = TRY(evaluate(*v, env));
    object->values[k] = std::move(value);
  }
  return Ok(std::move(object));
}

static Result<JSValue, JSThrown> evalCallExpr(const CallExpr &callExpr, const unsigned int lineNum,
                                              const std::shared_ptr<JSEnv> &env) {
  JSValue callee;
  JSValue recv;
  if (std::holds_alternative<AccessExpr>(callExpr.func->value)) {
    auto &access = std::get<AccessExpr>(callExpr.func->value);
    recv = TRY(evaluate(*access.recv, env));
    callee = TRY(findProperty(env, lineNum, recv, access.name));
  } else {
    callee = TRY(evaluate(*callExpr.func, env));
  }
  JSFunctionPtr func;
  if (std::holds_alternative<JSFunctionPtr>(callee)) {
    func = std::get<JSFunctionPtr>(callee);
  } else {
    return throwError(env, builtin::TYPE_ERROR, lineNum, "not a function");
  }
  std::vector<JSValue> args;
  for (auto &e : callExpr.args) {
    args.push_back(TRY(evaluate(*e, env)));
  }
  return callJSFunction(env, lineNum, func, std::move(recv), std::move(args));
}

static Result<JSValue, JSThrown> evaluate(const Node &node, const std::shared_ptr<JSEnv> &env) {
  return std::visit(
      [env, lineNum = node.lineNum](auto &&element) -> Result<JSValue, JSThrown> {
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
        } else if constexpr (std::is_same_v<T, NameExpr>) {
          if (auto *v = env->find(element.name)) {
            return Ok(*v);
          }
          std::string message = element.name;
          message += " is not defined";
          return throwError(env, builtin::REF_ERROR, lineNum, message);
        } else if constexpr (std::is_same_v<T, AccessExpr>) {
          auto recv = TRY(evaluate(*element.recv, env));
          return findProperty(env, lineNum, recv, element.name);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          return evalCallExpr(element, lineNum, env);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          auto value = TRY(evaluate(*element.expr, env));
          if (element.op == "!") {
            value = !toBool(value);
          }
          return Ok(std::move(value));
        } else if constexpr (std::is_same_v<T, VarDecl>) {
          auto value = TRY(evaluate(*element.expr, env));
          if (!env->define(element.name, std::move(value))) { // TODO: should be syntax error
            std::string message = "'";
            message += element.name;
            message += "' is already defined";
            return throwError(env, builtin::TYPE_ERROR, lineNum, message);
          }
          return Ok(JSValue());
        } else {
          fatal("unreachable");
        }
      },
      node.value);
}

Result<JSValue, JSThrown> jsEval(const char *sourceName, StringRef source,
                                 std::shared_ptr<JSEnv> global, const bool debug,
                                 std::string *syntaxErr) {
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
        return throwError(global, builtin::SYNTAX_ERROR, error.value().lineNum,
                          error.value().message);
      }
    }
  }
  JSValue last;
  for (auto &node : nodes) {
    last = TRY(evaluate(*node, global));
  }
  return Ok(std::move(last));
}

std::string formatEvalResult(const std::shared_ptr<JSEnv> &env,
                             const Result<JSValue, JSThrown> &result) {
  std::string out;
  auto &v = result ? result.asOk() : result.asErr().value;
  if (!result) {
    out += "[uncaught]\n";
  }
  if (auto ret = isInstanceOf(env, 0, v, env->findGlobalEnv()->findOrUndef(builtin::ERROR));
      ret && std::get<bool>(ret.asOk())) {
    if (auto r = findProperty(env, 1, v, "name")) {
      toPrettyString(r.asOk(), out);
    }
    if (auto r = findProperty(env, 1, v, "message")) {
      out += ": ";
      toPrettyString(r.asOk(), out);
      out += '\n';
    }
    if (auto r = findProperty(env, 1, v, "fileName")) {
      out += "    at ";
      toPrettyString(r.asOk(), out);
      out += ":";
      r = findProperty(env, 1, v, "lineNumber");
      if (r) {
        toPrettyString(r.asOk(), out);
      }
      out += '\n';
    }
  } else {
    toPrettyString(v, out);
    out += '\n';
  }
  return out;
}

} // namespace arsh::re262