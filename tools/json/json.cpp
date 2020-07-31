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

#include <misc/num_util.hpp>
#include <misc/unicode.hpp>

#include "json.h"

namespace ydsh {
namespace json {

// ##################
// ##     JSON     ##
// ##################

JSON::JSON(std::initializer_list<json::Member> list) : JSON(object()) {
    for(auto &v : list) {
        this->asObject().emplace(
                std::move(const_cast<Member &>(v).key),
                std::move(const_cast<Member &>(v).value));
    }
}

JSON JSON::fromString(const char *text) {
    Lexer lexer(text);
    Parser parser(std::move(lexer));
    return parser();
}

JSON &JSON::operator[](unsigned int index) {
    if(!this->isArray()) {
        fatal("must be array\n");
    }
    return this->asArray()[index];
}

JSON &JSON::operator[](const std::string &key) {
    if(!this->isObject()) {
        fatal("must be object\n");
    }
    return this->asObject()[key];
}

bool JSON::operator==(const JSON &json) const {
    if(this->tag() != json.tag()) {
        return false;
    }
    switch(this->tag()) {
    case JSON::TAG<std::nullptr_t>:
        return true;
    case JSON::TAG<bool>:
        return this->asBool() == json.asBool();
    case JSON::TAG<int64_t>:
        return this->asLong() == json.asLong();
    case JSON::TAG<double>:
        return this->asDouble() == json.asDouble();
    case JSON::TAG<String>:
        return this->asString() == json.asString();
    case JSON::TAG<Array>:
    case JSON::TAG<Object>:
        fatal("Array and Object are not allowed\n");
    default:
        break;
    }
    return true;
}

size_t JSON::hash() const {
    switch(this->tag()) {
    case JSON::TAG<std::nullptr_t>:
        break;
    case JSON::TAG<bool>:
        return std::hash<bool>()(this->asBool());
    case JSON::TAG<int64_t>:
        return std::hash<int64_t>()(this->asLong());
    case JSON::TAG<double>:
        return std::hash<double>()(this->asDouble());
    case JSON::TAG<String>:
        return std::hash<String>()(this->asString());
    default:
        fatal("Array and Object are not allowed\n");
    }
    return 0;
}

size_t JSON::size() const {
    if(this->isArray()) {
        return this->asArray().size();
    }
    if(this->isObject()) {
        return this->asObject().size();
    }
    return 0;
}

static unsigned int actualSize(const Array &value) {
    unsigned int count = 0;
    for(auto &e : value) {
        if(!e.isInvalid()) {
            count++;
        }
    }
    return count;
}

static unsigned int actualSize(const Object &value) {
    unsigned int count = 0;
    for(auto &e : value) {
        if(!e.second.isInvalid()) {
            count++;
        }
    }
    return count;
}

#define EACH_JSON_TYPE(T) \
    T(std::nullptr_t) \
    T(bool) \
    T(int64_t) \
    T(double) \
    T(String) \
    T(Array) \
    T(Object)

struct Serializer {
    const unsigned int tab;
    unsigned int level{0};
    std::string str;

    explicit Serializer(unsigned int tab) : tab(tab) {}

    void operator()(const JSON &value) {
        if(value.isInvalid()) {
            return;
        }
        this->serialize(value);

        if(this->tab > 0) {
            this->str += '\n';
        }
    }

    void serialize(const JSON &value) {
#define GEN_CASE(T) case JSON::TAG<T>: this->serialize(get<T>(value)); break;
        switch(value.tag()) {
        EACH_JSON_TYPE(GEN_CASE)
        default:
            fatal("invalid JSON object\n");
        }
#undef GEN_CASE
    }

    void serialize(std::nullptr_t) {
        this->str += "null";
    }

    void serialize(bool value) {
        this->str += value ? "true" : "false";
    }

    void serialize(int64_t value) {
        this->str += std::to_string(value);
    }

    void serialize(double value) {
        this->str += std::to_string(value);
    }

    void serialize(const String &value) {
        this->str += '"';
        for(int ch : value) {
            if(ch >= 0 && ch < 0x1F) {
                char buf[16];
                snprintf(buf, 16, "\\u%04x", ch);
                str += buf;
                continue;
            } else if(ch == '\\' || ch == '"') {
                this->str += '\\';
            }
            this->str += static_cast<char>(ch);
        }
        this->str += '"';
    }

    void serialize(const Array &value) {
        if(actualSize(value) == 0) {
            this->str += "[]";
            return;
        }

        this->enter('[');
        unsigned int count = 0;
        for(auto &e : value) {
            if(e.isInvalid()) {
                continue;
            }

            if(count++ > 0) {
                this->separator();
            }
            this->indent();
            this->serialize(e);
        }
        this->leave(']');
    }

    void serialize(const Object &value) {
        if(actualSize(value) == 0) {
            this->str += "{}";
            return;
        }

        this->enter('{');
        unsigned int count = 0;
        for(auto &e : value) {
            if(e.second.isInvalid()) {
                continue;
            }

            if(count++ > 0) {
                this->separator();
            }
            this->indent();
            this->serialize(e);
        }
        this->leave('}');
    }

    void enter(char ch) {
        this->str += ch;
        if(this->tab > 0) {
            this->str += '\n';
        }
        this->level++;
    }

    void leave(char ch) {
        if(this->tab > 0) {
            this->str += '\n';
        }
        this->level--;
        this->indent();
        this->str += ch;
    }

    void separator() {
        this->str += ',';
        if(this->tab > 0) {
            this->str += '\n';
        }
    }

    void serialize(const std::pair<const std::string, JSON> &value) {
        this->serialize(value.first);
        this->str += ':';
        if(this->tab > 0) {
            this->str += ' ';
        }
        this->serialize(value.second);
    }

    void indent() {
        for(unsigned int i = 0; i < this->level; i++) {
            for(unsigned j = 0; j < this->tab; j++) {
                this->str += ' ';
            }
        }
    }
};

std::string JSON::serialize(unsigned int tab) const {
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

// ####################
// ##     Parser     ##
// ####################

#define EACH_LA_VALUE(OP) \
    OP(NIL) \
    OP(TRUE) \
    OP(FALSE) \
    OP(NUMBER) \
    OP(STRING) \
    OP(ARRAY_OPEN) \
    OP(OBJECT_OPEN)


#define GEN_LA_CASE(CASE) case CASE:
#define GEN_LA_ALTER(CASE) CASE,

#define E_ALTER(...) \
do { this->reportNoViableAlterError((JSONTokenKind[]) { __VA_ARGS__ }); return JSON(); } while(false)

#define TRY(expr) \
({ auto v = expr; if(this->hasError()) { return JSON(); } std::forward<decltype(v)>(v); })

struct CallCounter {
    unsigned int &count;

    explicit CallCounter(unsigned int &count) : count(count) {}

    ~CallCounter() {
        --this->count;
    }
};

#define MAX_NESTING_DEPTH 8000
#define GUARD_DEEP_NESTING(name) \
if(++this->callCount == MAX_NESTING_DEPTH) { this->reportDeepNestingError(); return JSON(); } \
CallCounter name(this->callCount)


JSON Parser::operator()() {
    this->fetchNext();
    auto value = TRY(this->parseValue());
    TRY(this->expect(EOS));
    return value;
}

JSON Parser::parseValue() {
    GUARD_DEEP_NESTING(guard);

    switch(this->curKind) {
    case NIL:
        this->consume();    // NIL
        return nullptr;
    case TRUE:
        this->consume();    // TRUE
        return true;
    case FALSE:
        this->consume();    // FALSE
        return false;
    case NUMBER:
        return this->parseNumber();
    case STRING: {
        Token token = this->expect(STRING); // always success
        std::string str;
        TRY(this->unescapeStr(token, str));
        return JSON(std::move(str));    // for prevent build error in older gcc/clang
    }
    case ARRAY_OPEN:
        return this->parseArray();
    case OBJECT_OPEN:
        return this->parseObject();
    default:
        E_ALTER(EACH_LA_VALUE(GEN_LA_ALTER));
    }
}

static bool isFloat(const char *str) {
    return strchr(str, '.') != nullptr || strchr(str, 'e') != nullptr || strchr(str, 'E') != nullptr;
}

JSON Parser::parseNumber() {
    auto token = this->expect(NUMBER);  // always success
    char data[token.size + 1];
    auto ref = this->lexer->toStrRef(token);
    memcpy(data, ref.data(), ref.size());
    data[token.size] = '\0';

    if(isFloat(data)) {
        int status = 0;
        auto v = convertToDouble(data, status);
        if(status == 0) {
            return v;
        }
    } else {
        auto ret = convertToNum<int64_t>(data);
        if(ret.second) {
            return static_cast<int64_t>(ret.first);
        }
    }
    this->reportTokenFormatError(NUMBER, token, "out of range");
    return JSON();
}

#define TRY2(expr) \
({ auto v = expr; if(this->hasError()) { return {"", JSON()}; } std::forward<decltype(v)>(v); })

std::pair<std::string, JSON> Parser::parseMember() {
    Token token = this->expect(STRING); // always success
    TRY2(this->expect(COLON));
    JSON value = TRY2(this->parseValue());

    std::string key;
    TRY2(this->unescapeStr(token, key));

    return {std::move(key), std::move(value)};
}

JSON Parser::parseArray() {
    this->expect(ARRAY_OPEN);   // always success

    auto value = array();
    for(unsigned int count = 0; this->curKind != ARRAY_CLOSE; count++) {
        if(count > 0) {
            if(this->curKind != COMMA) {
                E_ALTER(COMMA, ARRAY_CLOSE);
            }
            this->consume();    // COMMA
        }
        switch(this->curKind) {
        EACH_LA_VALUE(GEN_LA_CASE)
            value.push_back(TRY(this->parseValue()));
            break;
        default:
            E_ALTER(EACH_LA_VALUE(GEN_LA_ALTER)
                            ARRAY_CLOSE);
        }
    }
    this->expect(ARRAY_CLOSE);
    return JSON(std::move(value));
}

JSON Parser::parseObject() {
    this->expect(OBJECT_OPEN);

    auto value = object();
    for(unsigned int count = 0; this->curKind != OBJECT_CLOSE; count++) {
        if(count > 0) {
            if(this->curKind != COMMA) {
                E_ALTER(COMMA, OBJECT_CLOSE);
            }
            this->consume();    // COMMA
        }
        if(this->curKind == STRING) {
            value.insert(TRY(this->parseMember()));
        } else {
            E_ALTER(STRING, OBJECT_CLOSE);
        }
    }
    this->expect(OBJECT_CLOSE);
    return JSON(std::move(value));
}

static unsigned short parseHex(const char *&iter) {
    unsigned short v = 0;
    for(unsigned int i = 0; i < 4; i++) {
        char ch = *(iter++);
        assert(isHex(ch));
        v *= 16;
        v += hexToNum(ch);
    }
    return v;
}

static int unescape(const char *&iter, const char *end) {
    if(iter == end) {
        return -1;
    }

    char ch = *(iter++);
    if(ch == '\\') {
        char next = *(iter++);
        switch(next) {
        case '"':
        case '\\':
        case '/':
            ch = next;
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
            if(UnicodeUtil::isLowSurrogate(ch)) {
                return -1;
            }
            if(UnicodeUtil::isHighSurrogate(ch)) {
                if(iter == end || *iter != '\\' || (iter + 1) == end || *(iter + 1) != 'u') {
                    return -1;
                }
                iter += 2;
                int low = parseHex(iter);
                if(!UnicodeUtil::isLowSurrogate(low)) {
                    return -1;
                }
                ch = UnicodeUtil::utf16ToCodePoint(ch, low);
            }
            break;
        default:
            return -1;
        }
    }
    return ch;
}

bool Parser::unescapeStr(Token token, std::string &str) {
    auto actual = token;
    actual.pos++;
    actual.size -= 2;

    auto range = this->lexer->toStrRef(actual);
    for(auto iter = range.begin(); iter != range.end();) {
        int codePoint = unescape(iter, range.end());
        if(codePoint < 0) {
            this->reportTokenFormatError(STRING, token, "illegal string format");
            return false;
        }
        char buf[4];
        unsigned int size = UnicodeUtil::codePointToUtf8(codePoint, buf);
        str.append(buf, size);
    }
    return true;
}

std::string Parser::formatError() const {
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

    str += this->lexer->toTokenText(lineToken);
    str += '\n';
    str += this->lexer->formatLineMarker(lineToken, errorToken);
    str += '\n';

    return str;
}

void Parser::showError(FILE *fp) const {
    assert(fp != nullptr);
    auto str = this->formatError();
    fputs(str.c_str(), fp);
    fflush(fp);
}

} // namespace json
} // namespace ydsh