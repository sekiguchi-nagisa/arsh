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

#include <misc/num.h>
#include <misc/unicode.hpp>

#include "json.h"

namespace json {

// ##################
// ##     JSON     ##
// ##################

JSON::JSON(std::initializer_list<json::Member> list) : JSON(createObject()) {
    for(auto &v : list) {
        this->asObject().emplace(
                std::move(const_cast<Member&>(v).key),
                std::move(const_cast<Member&>(v).value));
    }
}

JSON& JSON::operator[](unsigned int index) {
    if(!this->isArray()) {
        fatal("must be array\n");
    }
    return this->asArray()[index];
}

JSON& JSON::operator[](const std::string &key) {
    if(!this->isObject()) {
        fatal("must be object\n");
    }
    return this->asObject()[key];
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
do { this->raiseNoViableAlterError((JSONTokenKind[]) { __VA_ARGS__ }); return nullptr; } while(false)

#define TRY(expr) \
({ auto v = expr; if(this->hasError()) { return nullptr; } std::forward<decltype(v)>(v); })


JSON Parser::operator()(json::Lexer &&lexer) {
    this->lex = std::move(lexer);
    this->lexer = &this->lex;
    this->fetchNext();
    auto ret = TRY(this->parseValue());
    TRY(this->expect(EOS));
    return ret;
}

JSON Parser::parseValue() {
    switch(this->curKind) {
    case NIL:
        this->expect(NIL);
        return nullptr;
    case TRUE:
        this->expect(TRUE);
        return true;
    case FALSE:
        this->expect(FALSE);
        return false;
    case NUMBER:
        return this->parseNumber();
    case STRING: {
        Token token = this->expect(STRING); // always success
        auto str = createString();
        TRY(this->unescapeStr(token, *str));
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
    memcpy(data, this->lexer->getRange(token).first, token.size);
    data[token.size] = '\0';

    int status = 0;
    if(isFloat(data)) {
        auto v = ydsh::convertToDouble(data, status);
        if(status == 0) {
            return v;
        }
    } else {
        auto v = ydsh::convertToInt64(data, status);
        if(status == 0) {
            return v;
        }
    }
    this->raiseTokenFormatError(NUMBER, token, "out of range");
    return nullptr;
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

Array Parser::parseArray() {
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
            value->push_back(TRY(this->parseValue()));
            break;
        default:
            E_ALTER(EACH_LA_VALUE(GEN_LA_ALTER) ARRAY_CLOSE);
        }
    }
    this->expect(ARRAY_CLOSE);
    return value;
}

Object Parser::parseObject() {
    this->expect(OBJECT_OPEN);

    auto object = createObject();
    for(unsigned int count = 0; this->curKind != OBJECT_CLOSE; count++) {
        if(count > 0) {
            if(this->curKind != COMMA) {
                E_ALTER(COMMA, OBJECT_CLOSE);
            }
            this->consume();    // COMMA
        }
        if(this->curKind == STRING) {
            object->insert(TRY(this->parseMember()));
        } else {
            E_ALTER(STRING, OBJECT_CLOSE);
        }
    }
    this->expect(OBJECT_CLOSE);
    return object;
}

static unsigned short parseHex(const char *&iter) {
    unsigned short v = 0;
    for(unsigned int i = 0; i < 4; i++) {
        char ch = *(iter++);
        assert(ydsh::isHex(ch));
        v *= 16;
        v += ydsh::toHex(ch);
    }
    return v;
}

int Parser::unescape(const char *&iter, const char *end) const {
    if(iter == end) {
        return -1;
    }

    int ch = *(iter++);
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
            if(ydsh::UnicodeUtil::isLowSurrogate(ch)) {
                return -1;
            }
            if(ydsh::UnicodeUtil::isHighSurrogate(ch)) {
                if(iter == end || *iter != '\\' || (iter + 1) == end || *(iter + 1) != 'u') {
                    return -1;
                }
                iter += 2;
                int low = parseHex(iter);
                if(!ydsh::UnicodeUtil::isLowSurrogate(low)) {
                    return -1;
                }
                ch = ydsh::UnicodeUtil::utf16ToCodePoint(ch, low);
            }
            break;
        default:
            return -1;
        }
    }
    return ch;
}

bool Parser::unescapeStr(json::Token token, std::string &str)  {
    auto actual = token;
    actual.pos++;
    actual.size -= 2;

    auto range = this->lexer->getRange(actual);
    for(auto iter = range.first; iter != range.second;) {
        int codePoint = this->unescape(iter, range.second);
        if(codePoint < 0) {
            this->raiseTokenFormatError(STRING, token, "illegal string format");
            return false;
        }
        char buf[4];
        unsigned int size = ydsh::UnicodeUtil::codePointToUtf8(codePoint, buf);
        str.append(buf, size);
    }
    return true;
}

void Parser::showError() const {
    assert(this->hasError());

    unsigned int lineNum = 1;   // FIXME: fix lineNum
    const char *srcName = "(string)";   //FIXME:

    fprintf(stderr, "%s:%d: [error] %s\n", srcName, lineNum, this->getError().getMessage().c_str());


    auto eToken = this->getError().getErrorToken();
    auto errorToken = this->lexer->shiftEOS(eToken);
    auto lineToken = this->lexer->getLineToken(errorToken);
    auto line = this->lexer->toTokenText(lineToken);
    auto marker = this->lexer->formatLineMarker(lineToken, errorToken);

    fprintf(stderr, "%s\n%s\n", line.c_str(), marker.c_str());
    fflush(stderr);
}

} // namespace json