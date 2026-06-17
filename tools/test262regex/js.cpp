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

namespace arsh::re262 {

// for node definition
struct Node;

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

struct VarDecl { // currently only support `const`
  std::string name;
  std::unique_ptr<Node> expr;
};

struct Node {
  using Underlying =
      std::variant<BoolLiteral, NumberLiteral, StringLiteral, RegexLiteral, ArrayLiteral,
                   ObjectLiteral, NameExpr, AccessExpr, CallExpr, VarDecl>;
  Underlying value;
};

// ###################
// ##     JSEnv     ##
// ###################

void JSEnv::define(const std::string &name, JSValue value) {
  this->values[name] = std::move(value);
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

static JSValue findProperty(const std::shared_ptr<JSEnv> &env, const JSValue &recv,
                            const std::string &name) {
  (void)env;
  (void)recv;
  (void)name;

  return {};
}

static auto throwReferenceError(const std::shared_ptr<JSEnv> &, const std::string &) {
  return Err(JSThrown{12.0}); // TODO: lookuo
}

static auto throwTypeError(const std::shared_ptr<JSEnv> &, const std::string &) {
  return Err(JSThrown{12.0}); // TODO: lookuo
}

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
  object->values.reserve(literal.values.size());
  for (auto &[k, v] : literal.values) {
    auto value = TRY(evaluate(*v, env));
    object->values[k] = std::move(value);
  }
  return Ok(std::move(object));
}

static Result<JSValue, JSThrown> evalCallExpr(const CallExpr &callExpr,
                                              const std::shared_ptr<JSEnv> &env) {
  JSValue callee;
  JSValue recv;
  if (std::holds_alternative<AccessExpr>(callExpr.func->value)) {
    auto &access = std::get<AccessExpr>(callExpr.func->value);
    recv = TRY(evaluate(*access.recv, env));
    callee = findProperty(env, recv, access.name);
  } else {
    callee = TRY(evaluate(*callExpr.func, env));
  }
  JSFunctionPtr func;
  if (std::holds_alternative<JSFunctionPtr>(callee)) {
    func = std::get<JSFunctionPtr>(callee);
  } else {
    return throwTypeError(env, "not a function");
  }
  auto funcEnv = func->definedEnv.lock()->createChild();
  funcEnv->define("this", std::move(recv));
  const size_t maxArgs = std::max(func->params.size(), callExpr.args.size());
  for (size_t i = 0; i < maxArgs; i++) {
    JSValue arg;
    if (i < callExpr.args.size()) {
      arg = TRY(evaluate(*callExpr.args[i], env));
    }
    if (i < func->params.size()) {
      funcEnv->define(func->params[i], std::move(arg));
    }
  }
  return func->impl(std::move(funcEnv));
}

static Result<JSValue, JSThrown> evaluate(const Node &node, const std::shared_ptr<JSEnv> &env) {
  return std::visit(
      [env](auto &&element) -> Result<JSValue, JSThrown> {
        using T = std::decay_t<decltype(element)>;
        if constexpr (std::is_same_v<T, BoolLiteral> || std::is_same_v<T, NumberLiteral> ||
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
          return throwReferenceError(env, element.name);
        } else if constexpr (std::is_same_v<T, AccessExpr>) {
          auto recv = TRY(evaluate(*element.recv, env));
          return Ok(findProperty(env, recv, element.name));
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          return evalCallExpr(element, env);
        } else if constexpr (std::is_same_v<T, VarDecl>) {
          auto value = TRY(evaluate(*element.expr, env));
          env->define(element.name, std::move(value));
          return Ok(JSValue());
        }
        return Err(JSThrown{JSValue(12.0)}); // TODO
      },
      node.value);
}

Result<JSValue, JSThrown> jsEval(const char *sourceName, StringRef source,
                                 const std::shared_ptr<JSEnv> &global) {
  JSLexer lexer(sourceName, source);
  std::unique_ptr<Node> node;
  return evaluate(*node, global);
}

} // namespace arsh::re262