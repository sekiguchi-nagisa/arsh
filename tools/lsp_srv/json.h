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

#ifndef TOOLS_JSON_H
#define TOOLS_JSON_H

#include <vector>
#include <memory>
#include <map>
#include <string>
#include <cstring>
#include <cstdlib>

#include <misc/result.hpp>
#include <misc/lexer_base.hpp>
#include <misc/parser_base.hpp>

namespace json {

class JSON;

using String = std::unique_ptr<std::string>;
using Array = std::unique_ptr<std::vector<JSON>>;
using Object = std::unique_ptr<std::map<std::string, JSON>>;

struct Member;

namespace __detail {

inline void append(Array &) {}

template <typename ...T>
void append(Array &array, JSON &&v, T && ...arg) {
    array->push_back(std::move(v));
    append(array, std::forward<T>(arg)...);
}

} // namespace

template <typename ...Arg>
inline String createString(Arg && ...arg) {
    return ydsh::unique<std::string>(std::forward<Arg>(arg)...);
}

template <typename ... Arg>
inline Array array(Arg&& ...arg) {
    auto value = ydsh::unique<std::vector<JSON>>();
    __detail::append(value, std::forward<Arg>(arg)...);
    return value;
}

inline Object createObject() {
    return ydsh::unique<std::map<std::string, JSON>>();
}

class JSON : public ydsh::Union<bool, long, double, String, Array, Object> {
public:
    using Base = ydsh::Union<bool, long, double, String, Array, Object>;

    JSON() : Base() {}
    JSON(std::nullptr_t) : Base() {}
    JSON(bool v) : Base(v) {}
    JSON(long v) : Base(v) {}
    JSON(int v) : JSON(static_cast<long>(v)) {}
    JSON(double v) : Base(v) {}
    JSON(const char *str) : Base(createString(str)) {}
    JSON(const std::string &str) : JSON(str.c_str()) {}
    JSON(String &&v) : Base(std::move(v)) {}
    JSON(Array &&v) : Base(std::move(v)) {}
    JSON(Object &&v) : Base(std::move(v)) {}
    JSON(std::initializer_list<Member> list);


    bool isNull() const {
        return this->tag() < 0;
    }

    bool isBool() const {
        return ydsh::is<bool>(*this);
    }

    bool isLong() const {
        return ydsh::is<long>(*this);
    }

    bool isDouble() const {
        return ydsh::is<double >(*this);
    }

    bool isString() const {
        return ydsh::is<String>(*this);
    }

    bool isArray() const {
        return ydsh::is<Array>(*this);
    }

    bool isObject() const {
        return ydsh::is<Object>(*this);
    }

    bool asBool() const {
        return ydsh::get<bool>(*this);
    }

    long asLong() const {
        return ydsh::get<long>(*this);
    }

    double asDouble() const {
        return ydsh::get<double >(*this);
    }

    const std::string &asString() const {
        return *ydsh::get<String>(*this);
    }

    std::vector<JSON> &asArray() {
        return *ydsh::get<Array>(*this);
    }

    const std::vector<JSON> &asArray() const {
        return *ydsh::get<Array>(*this);
    }

    std::map<std::string, JSON> &asObject() {
        return *ydsh::get<Object>(*this);
    }

    const std::map<std::string, JSON> &asObject() const {
        return *ydsh::get<Object>(*this);
    }

    JSON &operator[](unsigned int index);

    JSON &operator[](const std::string &key);

    size_t size() const;
};

struct Member {
    std::string key;
    JSON value;

    NON_COPYABLE(Member);

    Member(std::string &&key, JSON &&value) : key(std::move(key)), value(std::move(value)) {}

    Member(const std::string &key, JSON &&value) : key(key), value(std::move(value)) {}
};

#define EACH_JSON_TOKEN(OP) \
    OP(INVALID     , "<invalid>") \
    OP(EOS         , "<EOS>") \
    OP(NUMBER      , "<Number>") \
    OP(TRUE        , "true") \
    OP(FALSE       , "false") \
    OP(NIL         , "null") \
    OP(STRING      , "<String>") \
    OP(ARRAY_OPEN  , "[") \
    OP(ARRAY_CLOSE , "]") \
    OP(OBJECT_OPEN , "{") \
    OP(OBJECT_CLOSE, "}") \
    OP(COLON       , ":") \
    OP(COMMA       , ",")


enum JSONTokenKind : unsigned int {
#define GEN_TOKEN(T, S) T,
    EACH_JSON_TOKEN(GEN_TOKEN)
#undef GEN_TOKEN
};


inline bool isInvalidToken(JSONTokenKind kind) {
    return kind == INVALID;
}

const char *toString(JSONTokenKind kind);

using Token = ydsh::Token;

class Lexer : public ydsh::parser_base::LexerBase {
public:
    Lexer() = default;
    explicit Lexer(const char *line) : LexerBase(line) {}
    Lexer(const char *data, unsigned int size) : LexerBase(data, size) {}

    JSONTokenKind nextToken(Token &token);

    std::pair<const char *, const char *> getRange(Token token) const {
        const char *begin = (char *)this->buf.get() + token.pos;
        const char *end = begin + token.size;
        return {begin, end};
    }
};

class Parser : public ydsh::parser_base::AbstractParser<JSONTokenKind, Lexer> {
private:
    Lexer lex;

public:
    JSON operator()(Lexer &&lexer);

    JSON operator()(const char *date, unsigned int size) {
        return (*this)(Lexer(date, size));
    }

    JSON operator()(const char *str) {
        return (*this)(str, strlen(str));
    }

    void showError() const;

private:
    JSON parseValue();
    JSON parseNumber();
    std::pair<std::string, JSON> parseMember();
    Array parseArray();
    Object parseObject();

    int unescape(const char * &iter, const char *end) const;

    bool unescapeStr(Token token, std::string &str);
};

} // namespace json

#endif //TOOLS_JSON_H
