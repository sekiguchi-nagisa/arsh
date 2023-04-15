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

#include <constant.h>

#include <misc/num_util.hpp>
#include <misc/resource.hpp>
#include <misc/unicode.hpp>

#include "json.h"

namespace ydsh::json {

// ##################
// ##     JSON     ##
// ##################

JSON::JSON(std::initializer_list<json::JSONMember> list) : JSON(object()) {
  for (auto &v : list) {
    this->asObject().emplace(std::move(const_cast<JSONMember &>(v).key),
                             std::move(const_cast<JSONMember &>(v).value));
  }
}

JSON JSON::fromString(const char *text) {
  JSONLexer lexer(text);
  JSONParser parser(std::move(lexer));
  return parser();
}

JSON &JSON::operator[](unsigned int index) {
  if (!this->isArray()) {
    fatal("must be array\n");
  }
  return this->asArray()[index];
}

JSON &JSON::operator[](const std::string &key) {
  if (!this->isObject()) {
    fatal("must be object\n");
  }
  return this->asObject()[key];
}

size_t JSON::size() const {
  if (this->isArray()) {
    return this->asArray().size();
  }
  if (this->isObject()) {
    return this->asObject().size();
  }
  return 0;
}

static unsigned int actualSize(const Array &value) {
  unsigned int count = 0;
  for (auto &e : value) {
    if (!e.isInvalid()) {
      count++;
    }
  }
  return count;
}

static unsigned int actualSize(const Object &value) {
  unsigned int count = 0;
  for (auto &e : value) {
    if (!e.second.isInvalid()) {
      count++;
    }
  }
  return count;
}

#define EACH_JSON_TYPE(T)                                                                          \
  T(std::nullptr_t)                                                                                \
  T(bool)                                                                                          \
  T(int64_t)                                                                                       \
  T(double)                                                                                        \
  T(String)                                                                                        \
  T(Array)                                                                                         \
  T(Object)

struct Serializer {
  const unsigned int tab;
  unsigned int level{0};
  std::string str;

  explicit Serializer(unsigned int tab) : tab(tab) {}

  void operator()(const JSON &value) {
    if (value.isInvalid()) {
      return;
    }
    this->serialize(value);

    if (this->tab > 0) {
      this->str += '\n';
    }
  }

  void serialize(const JSON &value) {
#define GEN_CASE(T)                                                                                \
  case JSON::TAG<T>:                                                                               \
    this->serialize(get<T>(value));                                                                \
    break;
    switch (value.tag()) {
      EACH_JSON_TYPE(GEN_CASE)
    default:
      fatal("invalid JSON object\n");
    }
#undef GEN_CASE
  }

  void serialize(std::nullptr_t) { this->str += "null"; }

  void serialize(bool value) { this->str += value ? "true" : "false"; }

  void serialize(int64_t value) { this->str += std::to_string(value); }

  void serialize(double value) { this->str += std::to_string(value); }

  void serialize(const String &value) {
    this->str += '"';
    for (unsigned char ch : value) {
      if (ch < 0x1F) {
        char buf[16];
        snprintf(buf, 16, "\\u%04x", ch);
        str += buf;
        continue;
      } else if (ch == '\\' || ch == '"') {
        this->str += '\\';
      }
      this->str += static_cast<char>(ch);
    }
    this->str += '"';
  }

  void serialize(const Array &value) {
    if (actualSize(value) == 0) {
      this->str += "[]";
      return;
    }

    this->enter('[');
    unsigned int count = 0;
    for (auto &e : value) {
      if (e.isInvalid()) {
        continue;
      }

      if (count++ > 0) {
        this->separator();
      }
      this->indent();
      this->serialize(e);
    }
    this->leave(']');
  }

  void serialize(const Object &value) {
    if (actualSize(value) == 0) {
      this->str += "{}";
      return;
    }

    this->enter('{');
    unsigned int count = 0;
    for (auto &e : value) {
      if (e.second.isInvalid()) {
        continue;
      }

      if (count++ > 0) {
        this->separator();
      }
      this->indent();
      this->serialize(e);
    }
    this->leave('}');
  }

  void enter(char ch) {
    this->str += ch;
    if (this->tab > 0) {
      this->str += '\n';
    }
    this->level++;
  }

  void leave(char ch) {
    if (this->tab > 0) {
      this->str += '\n';
    }
    this->level--;
    this->indent();
    this->str += ch;
  }

  void separator() {
    this->str += ',';
    if (this->tab > 0) {
      this->str += '\n';
    }
  }

  void serialize(const std::pair<const std::string, JSON> &value) {
    this->serialize(value.first);
    this->str += ':';
    if (this->tab > 0) {
      this->str += ' ';
    }
    this->serialize(value.second);
  }

  void indent() {
    for (unsigned int i = 0; i < this->level; i++) {
      for (unsigned j = 0; j < this->tab; j++) {
        this->str += ' ';
      }
    }
  }
};

std::string JSON::serialize(unsigned int tab) const {
  LC_NUMERIC_C.use(); // always use C locale (for std::to_string)

  Serializer serializer(tab);
  serializer(*this);
  return std::move(serializer.str);
}

const char *toString(JSONTokenKind kind) {
  const char *table[] = {
#define GEN_STR(T, S) S,
      EACH_JSON_TOKEN(GEN_STR)
#undef GEN_STR
  };
  return table[static_cast<unsigned int>(kind)];
}

// ########################
// ##     JSONParser     ##
// ########################

#define EACH_LA_VALUE(OP)                                                                          \
  OP(NIL)                                                                                          \
  OP(TRUE)                                                                                         \
  OP(FALSE)                                                                                        \
  OP(NUMBER)                                                                                       \
  OP(STRING)                                                                                       \
  OP(ARRAY_OPEN)                                                                                   \
  OP(OBJECT_OPEN)

#define GEN_LA_CASE(CASE) case JSONTokenKind::CASE:
#define GEN_LA_ALTER(CASE) JSONTokenKind::CASE,

#define E_ALTER(...)                                                                               \
  do {                                                                                             \
    this->reportNoViableAlterError((JSONTokenKind[]){__VA_ARGS__});                                \
    return JSON();                                                                                 \
  } while (false)

#define TRY(expr)                                                                                  \
  ({                                                                                               \
    auto v = expr;                                                                                 \
    if (unlikely(this->hasError())) {                                                              \
      return JSON();                                                                               \
    }                                                                                              \
    std::forward<decltype(v)>(v);                                                                  \
  })

#define MAX_NESTING_DEPTH 8000
#define GUARD_DEEP_NESTING(name)                                                                   \
  CallCounter name(this->callCount);                                                               \
  if (this->callCount == MAX_NESTING_DEPTH) {                                                      \
    this->reportDeepNestingError();                                                                \
    return JSON();                                                                                 \
  }                                                                                                \
  (void)name

static bool lookahead_LA_VALUE(JSONTokenKind k) {
  switch (k) {
    // clang-format off
  EACH_LA_VALUE(GEN_LA_CASE)
    // clang-format on
    return true;
  default:
    return false;
  }
}

JSON JSONParser::operator()(bool maybeEmpty) {
  this->fetchNext();
  if (maybeEmpty && !static_cast<bool>(*this)) {
    return JSON();
  }
  auto value = TRY(this->parseValue());
  TRY(this->expect(JSONTokenKind::EOS));
  return value;
}

JSON JSONParser::parseValue() {
  GUARD_DEEP_NESTING(guard);

  switch (this->curKind) {
  case JSONTokenKind::NIL:
    this->consume(); // NIL
    return nullptr;
  case JSONTokenKind::TRUE:
    this->consume(); // TRUE
    return true;
  case JSONTokenKind::FALSE:
    this->consume(); // FALSE
    return false;
  case JSONTokenKind::NUMBER:
    return this->parseNumber();
  case JSONTokenKind::STRING:
    return this->parseString();
  case JSONTokenKind::ARRAY_OPEN:
    return this->parseArray();
  case JSONTokenKind::OBJECT_OPEN:
    return this->parseObject();
  default:
    E_ALTER(EACH_LA_VALUE(GEN_LA_ALTER));
  }
}

static bool isFloat(const char *str) {
  return strchr(str, '.') != nullptr || strchr(str, 'e') != nullptr || strchr(str, 'E') != nullptr;
}

JSON JSONParser::parseNumber() {
  auto token = this->expect(JSONTokenKind::NUMBER); // always success
  char data[token.size + 1];
  auto ref = this->lexer->toStrRef(token);
  memcpy(data, ref.data(), ref.size());
  data[token.size] = '\0';

  if (isFloat(data)) {
    if (auto ret = convertToDouble(data); ret.second == 0) {
      return ret.first;
    }
  } else {
    if (auto ret = convertToDecimal<int64_t>(data); ret.second) {
      return static_cast<int64_t>(ret.first);
    }
  }
  this->reportTokenFormatError(JSONTokenKind::NUMBER, token, "out of range");
  return JSON();
}

#define TRY2(expr)                                                                                 \
  ({                                                                                               \
    auto v = expr;                                                                                 \
    if (unlikely(this->hasError())) {                                                              \
      return {"", JSON()};                                                                         \
    }                                                                                              \
    std::forward<decltype(v)>(v);                                                                  \
  })

std::pair<std::string, JSON> JSONParser::parseMember() {
  JSON key = TRY2(this->parseString());
  TRY2(this->expect(JSONTokenKind::COLON));
  JSON value = TRY2(this->parseValue());
  assert(key.isString());
  return {std::move(key.asString()), std::move(value)};
}

JSON JSONParser::parseArray() {
  this->expect(JSONTokenKind::ARRAY_OPEN); // always success

  auto value = array();
  for (unsigned int count = 0; this->curKind != JSONTokenKind::ARRAY_CLOSE; count++) {
    if (count > 0) {
      if (this->curKind != JSONTokenKind::COMMA) {
        E_ALTER(JSONTokenKind::COMMA, JSONTokenKind::ARRAY_CLOSE);
      }
      this->consume(); // COMMA
    }
    if (lookahead_LA_VALUE(this->curKind)) {
      value.push_back(TRY(this->parseValue()));
    } else {
      E_ALTER(EACH_LA_VALUE(GEN_LA_ALTER) JSONTokenKind::ARRAY_CLOSE);
    }
  }
  this->expect(JSONTokenKind::ARRAY_CLOSE);
  return JSON(std::move(value));
}

JSON JSONParser::parseObject() {
  this->expect(JSONTokenKind::OBJECT_OPEN);

  auto value = object();
  for (unsigned int count = 0; this->curKind != JSONTokenKind::OBJECT_CLOSE; count++) {
    if (count > 0) {
      if (this->curKind != JSONTokenKind::COMMA) {
        E_ALTER(JSONTokenKind::COMMA, JSONTokenKind::OBJECT_CLOSE);
      }
      this->consume(); // COMMA
    }
    if (this->curKind == JSONTokenKind::STRING) {
      value.insert(TRY(this->parseMember()));
    } else {
      E_ALTER(JSONTokenKind::STRING, JSONTokenKind::OBJECT_CLOSE);
    }
  }
  this->expect(JSONTokenKind::OBJECT_CLOSE);
  return JSON(std::move(value));
}

static unsigned short parseHex(const char *&iter) {
  unsigned short v = 0;
  for (unsigned int i = 0; i < 4; i++) {
    char ch = *(iter++);
    assert(isHex(ch));
    v *= 16;
    v += hexToNum(ch);
  }
  return v;
}

static int parseEscape(const char *&iter, const char *end) {
  if (iter == end || *iter != '\\') {
    return -1;
  }

  iter++; // skip '\\'
  int ch = static_cast<unsigned char>(*(iter++));
  switch (ch) {
  case '"':
  case '\\':
  case '/':
    break;
  case 'b':
    ch = '\b';
    break;
  case 'f':
    ch = '\f';
    break;
  case 'n':
    ch = '\n';
    break;
  case 'r':
    ch = '\r';
    break;
  case 't':
    ch = '\t';
    break;
  case 'u':
    ch = parseHex(iter);
    if (UnicodeUtil::isLowSurrogate(ch)) {
      return -1;
    }
    if (UnicodeUtil::isHighSurrogate(ch)) {
      if (iter == end || *iter != '\\' || (iter + 1) == end || *(iter + 1) != 'u') {
        return -1;
      }
      iter += 2;
      int low = parseHex(iter);
      if (!UnicodeUtil::isLowSurrogate(low)) {
        return -1;
      }
      ch = UnicodeUtil::utf16ToCodePoint(ch, low);
    }
    break;
  default:
    return -1;
  }
  return ch;
}

JSON JSONParser::parseString() {
  Token token = TRY(this->expect(JSONTokenKind::STRING));
  auto ref = this->lexer->toStrRef(token);
  ref.removePrefix(1); // prefix ["]
  ref.removeSuffix(1); // suffix ["]

  std::string value;
  const char *end = ref.end();
  for (const char *iter = ref.begin(); iter != end;) {
    if (*iter == '\\') {
      int code = parseEscape(iter, end);
      if (code < 0) {
        this->reportTokenFormatError(JSONTokenKind::STRING, token, "illegal string format");
        return JSON(); // broken escape sequence
      }
      char buf[4];
      unsigned int size = UnicodeUtil::codePointToUtf8(code, buf);
      value.append(buf, size);
    } else {
      value += *(iter++);
    }
  }
  return JSON(std::move(value));
}

std::string JSONParser::formatError() const {
  assert(this->hasError());

  std::string str;

  unsigned int lineNum = this->lexer->getLineNumByPos(this->getError().getErrorToken().pos);

  str += this->lexer->getSourceName();
  str += ':';
  str += std::to_string(lineNum);
  str += ": [error] ";
  str += this->getError().getMessage();
  str += '\n';

  auto eToken = this->getError().getErrorToken();
  auto errorToken = this->lexer->shiftEOS(eToken);
  auto lineToken = this->lexer->getLineToken(errorToken);

  str += this->lexer->formatTokenText(lineToken);
  str += this->lexer->formatLineMarker(lineToken, errorToken);
  str += '\n';

  return str;
}

void JSONParser::showError(FILE *fp) const {
  assert(fp != nullptr);
  auto str = this->formatError();
  fputs(str.c_str(), fp);
  fflush(fp);
}

} // namespace ydsh::json