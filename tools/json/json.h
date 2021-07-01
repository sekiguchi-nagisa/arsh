/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_JSON_JSON_H
#define YDSH_TOOLS_JSON_JSON_H

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <misc/lexer_base.hpp>
#include <misc/parser_base.hpp>
#include <misc/result.hpp>

namespace ydsh {
namespace json {

class JSON;

using String = std::string;
using Array = std::vector<JSON>;
using Object = std::map<std::string, JSON>;

struct JSONMember;

class JSON : public Union<std::nullptr_t, bool, int64_t, double, String, Array, Object> {
public:
  using Base = Union<std::nullptr_t, bool, int64_t, double, String, Array, Object>;

  explicit JSON() : Base() {}

  JSON(bool v) : Base(v) {}                                // NOLINT
  JSON(int64_t v) : Base(v) {}                             // NOLINT
  JSON(int v) : JSON(static_cast<int64_t>(v)) {}           // NOLINT
  JSON(double v) : Base(v) {}                              // NOLINT
  JSON(const char *str) : JSON(std::string(str)) {}        // NOLINT
  JSON(const std::string &str) : JSON(std::string(str)) {} // NOLINT
  JSON(String &&v) : Base(std::move(v)) {}                 // NOLINT
  JSON(Array &&v) : Base(std::move(v)) {}                  // NOLINT
  JSON(Object &&v) : Base(std::move(v)) {}                 // NOLINT
  JSON(std::initializer_list<JSONMember> list);                // NOLINT
  JSON(std::nullptr_t) : Base(nullptr) {}                  // NOLINT

  /**
   * if text is invalid json, return empty json object
   * @param text
   * @return
   */
  static JSON fromString(const char *text);

  bool isInvalid() const { return this->tag() < 0; }

  bool isNull() const { return is<std::nullptr_t>(*this); }

  bool isBool() const { return is<bool>(*this); }

  bool isLong() const { return is<int64_t>(*this); }

  bool isDouble() const { return is<double>(*this); }

  bool isNumber() const { return this->isLong() || this->isDouble(); }

  bool isString() const { return is<String>(*this); }

  bool isArray() const { return is<Array>(*this); }

  bool isObject() const { return is<Object>(*this); }

  bool asBool() const { return get<bool>(*this); }

  int64_t asLong() const { return get<int64_t>(*this); }

  double asDouble() const { return get<double>(*this); }

  std::string &asString() { return get<String>(*this); }

  const std::string &asString() const { return get<String>(*this); }

  std::vector<JSON> &asArray() { return get<Array>(*this); }

  const std::vector<JSON> &asArray() const { return get<Array>(*this); }

  std::map<std::string, JSON> &asObject() { return get<Object>(*this); }

  const std::map<std::string, JSON> &asObject() const { return get<Object>(*this); }

  JSON &operator[](unsigned int index);

  JSON &operator[](const std::string &key);

  bool operator==(const JSON &json) const;

  bool operator!=(const JSON &json) const { return !(*this == json); }

  size_t hash() const;

  size_t size() const;

  std::string serialize(unsigned int tab = 0) const;
};

struct JSONMember {
  std::string key;
  JSON value;

  NON_COPYABLE(JSONMember);

  JSONMember(std::string &&key, JSON &&value) : key(std::move(key)), value(std::move(value)) {}
};

namespace __detail {

inline void append(Array &) {}

inline void append(Object &) {}

template <typename... T>
void append(Array &array, JSON &&v, T &&...arg) {
  array.push_back(std::move(v));
  append(array, std::forward<T>(arg)...);
}

template <typename... T>
void append(Object &object, JSONMember &&v, T &&...arg) {
  object.emplace(std::move(v.key), std::move(v.value));
  append(object, std::forward<T>(arg)...);
}

} // namespace __detail

inline Array array() { return std::vector<JSON>(); }

template <typename... Arg>
inline Array array(JSON &&v, Arg &&...arg) {
  auto value = array();
  __detail::append(value, std::forward<JSON>(v), std::forward<Arg>(arg)...);
  return value;
}

inline Object object() { return std::map<std::string, JSON>(); }

template <typename... Arg>
inline Object object(JSONMember &&m, Arg &&...arg) {
  auto value = object();
  __detail::append(value, std::forward<JSONMember>(m), std::forward<Arg>(arg)...);
  return value;
}

#define EACH_JSON_TOKEN(OP)                                                                        \
  OP(INVALID, "<invalid>")                                                                         \
  OP(EOS, "<EOS>")                                                                                 \
  OP(NUMBER, "<Number>")                                                                           \
  OP(TRUE, "true")                                                                                 \
  OP(FALSE, "false")                                                                               \
  OP(NIL, "null")                                                                                  \
  OP(STRING, "<String>")                                                                           \
  OP(ARRAY_OPEN, "[")                                                                              \
  OP(ARRAY_CLOSE, "]")                                                                             \
  OP(OBJECT_OPEN, "{")                                                                             \
  OP(OBJECT_CLOSE, "}")                                                                            \
  OP(COLON, ":")                                                                                   \
  OP(COMMA, ",")

enum class JSONTokenKind : unsigned int {
#define GEN_TOKEN(T, S) T,
  EACH_JSON_TOKEN(GEN_TOKEN)
#undef GEN_TOKEN
};

inline bool isInvalidToken(JSONTokenKind kind) { return kind == JSONTokenKind::INVALID; }

inline bool isEOSToken(JSONTokenKind kind) { return kind == JSONTokenKind::EOS; }

const char *toString(JSONTokenKind kind);

class JSONLexer : public LexerBase {
public:
  template <typename... Arg>
  explicit JSONLexer(Arg &&...arg) : LexerBase("(string)", std::forward<Arg>(arg)...) {}

  JSONTokenKind nextToken(Token &token);
};

class JSONParser : public ParserBase<JSONTokenKind, JSONLexer> {
private:
  JSONLexer lex;

public:
  explicit JSONParser(JSONLexer &&lex) : lex(std::move(lex)) { this->lexer = &this->lex; }

  template <typename... Arg>
  explicit JSONParser(Arg &&...arg) : JSONParser(JSONLexer(std::forward<Arg>(arg)...)) {}

  JSON operator()();

  explicit operator bool() const { return this->curKind != JSONTokenKind::EOS; }

  std::string formatError() const;

  void showError(FILE *fp = stderr) const;

private:
  JSON parseValue();

  JSON parseNumber();

  std::pair<std::string, JSON> parseMember();

  JSON parseArray();

  JSON parseObject();

  bool unescapeStr(Token token, std::string &str);
};

} // namespace json

// specialize Optional<JSON>
template <>
class OptionalBase<json::JSON> : public json::JSON {
public:
  using base_type = json::JSON;

  OptionalBase() noexcept : JSON() {}

  OptionalBase(JSON &&json) : JSON(std::move(json)) {} // NOLINT

  json::JSON &unwrap() noexcept { return static_cast<base_type &>(*this); }

  const json::JSON &unwrap() const noexcept { return static_cast<const base_type &>(*this); }
};

} // namespace ydsh

#endif // YDSH_TOOLS_JSON_JSON_H
