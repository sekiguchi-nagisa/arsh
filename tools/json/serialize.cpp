/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include <cassert>
#include <cstdarg>

#include "serialize.h"

namespace ydsh {
namespace json {

// ############################
// ##     JSONSerializer     ##
// ############################

JSONSerializer JSONSerializer::asArray() {
    Array v;
    JSONSerializer s;
    s.result = std::move(v);
    return s;
}

JSONSerializer JSONSerializer::asObject() {
    Object v;
    JSONSerializer s;
    s.result = std::move(v);
    return s;
}

void JSONSerializer::operator()(const char *fieldName, std::nullptr_t) {
    this->append(fieldName, JSON(nullptr));
}

void JSONSerializer::operator()(const char *fieldName, bool v) {
    this->append(fieldName, v);
}

void JSONSerializer::operator()(const char *fieldName, int64_t v) {
    this->append(fieldName, v);
}

void JSONSerializer::operator()(const char *fieldName, double v) {
    this->append(fieldName, v);
}

void JSONSerializer::operator()(const char *fieldName, const std::string &v) {
    this->append(fieldName, v);
}

void JSONSerializer::operator()(const char *fieldName, const JSON &v) {
    this->append(fieldName, JSON(v));
}

void JSONSerializer::append(const char *fieldName, JSON &&json) {
    if(this->result.isArray()) {
        this->result.asArray().push_back(std::move(json));
    } else if(this->result.isObject()) {
        assert(fieldName);
        this->result.asObject().emplace(fieldName, std::move(json));
    } else if(this->result.isInvalid()) {
        this->result = std::move(json);
    } else {
        fatal("broken");
    }
}

// ###########################
// ##     JSONValidator     ##
// ###########################

std::string JSONValidator::formatError() const {
    std::string str;
    if(!this->errors.empty()) {
        str = this->errors.back();
        for(auto iter = this->errors.rbegin() + 1; iter != this->errors.rend(); ++iter) {
            str += "\n    from: ";
            str += *iter;
        }
    }
    return str;
}

void JSONValidator::appendError(const char *fmt, ...) {
    va_list arg;
    va_start(arg, fmt);
    char *str = nullptr;
    if(vasprintf(&str, fmt, arg) == -1) {
        fatal_perror("");
    }
    va_end(arg);

    this->errors.emplace_back(str);
    free(str);
}

static const char *getJSONTypeStr(int tag) {
    switch(tag) {
    case JSON::TAG<std::nullptr_t>:
        return "null";
    case JSON::TAG<bool>:
        return "bool";
    case JSON::TAG<int64_t>:
        return "long";
    case JSON::TAG<double>:
        return "double";
    case JSON::TAG<String>:
        return "string";
    case JSON::TAG<Object>:
        return "object";
    case JSON::TAG<Array>:
        return "array";
    default:
        break;
    }
    return "invalid";
}

static const char *getJSONTypeStr(const JSON &v) {
    return getJSONTypeStr(v.tag());
}

bool JSONValidator::validate(const JSON &value, int required, const char *messagePrefix) {
    if(value.tag() != required) {
        this->appendError("%srequire %s, but is: `%s'",
                          messagePrefix,
                          getJSONTypeStr(required),
                          getJSONTypeStr(value));
        return false;
    }
    return true;
}

bool JSONValidator::validate(const JSON &value, const char *fieldName, int required) {
    if(!this->validate<Object>(value)) {
        return false;
    }
    auto &obj = value.asObject();
    auto iter = obj.find(fieldName);
    if(iter == obj.end()) {
        this->appendError("undefined field: `%s'", fieldName);
        return false;
    }
    std::string prefix = "field: `";
    prefix += fieldName;
    prefix += "' ";
    return this->validate(iter->second, required, prefix.c_str());
}

// ##############################
// ##     JSONDeserializer     ##
// ##############################

void JSONDeserializer::operator()(const char *fieldName, std::nullptr_t &) {
    if(fieldName) {
        this->validator->validate<std::nullptr_t>(this->root, fieldName);
    } else {
        this->validator->validate<std::nullptr_t>(this->root);
    }
}

void JSONDeserializer::operator()(const char *fieldName, bool &v) {
    if(fieldName) {
        if(this->validator->validate<bool>(this->root, fieldName)) {
            v = this->root[fieldName].asBool();
        }
    } else if(this->validator->validate<bool>(this->root)) {
        v = this->root.asBool();
    }
}

void JSONDeserializer::operator()(const char *fieldName, int64_t &v) {
    if(fieldName) {
        if(this->validator->validate<int64_t>(this->root, fieldName)) {
            v = this->root[fieldName].asLong();
        }
    } else if(this->validator->validate<int64_t>(this->root)) {
        v = this->root.asLong();
    }
}

void JSONDeserializer::operator()(const char *fieldName, double &v) {
    if(fieldName) {
        if(this->validator->validate<double>(this->root, fieldName)) {
            v = this->root[fieldName].asDouble();
        }
    } else if(this->validator->validate<double>(this->root)) {
        v = this->root.asDouble();
    }
}

void JSONDeserializer::operator()(const char *fieldName, std::string &v) {
    if(fieldName) {
        if(this->validator->validate<String>(this->root, fieldName)) {
            v = std::move(this->root[fieldName].asString());
        }
    } else if(this->validator->validate<String>(this->root)) {
        v = std::move(this->root.asString());
    }
}

void JSONDeserializer::operator()(const char *fieldName, JSON &v) {
    if(fieldName) {
        if(this->validator->validate<Object>(this->root, fieldName)) {
            v = std::move(this->root[fieldName]);
        }
    } else if(this->validator->validate<Object>(this->root)) {
        v = std::move(this->root);
    }
}

JSON JSONDeserializer::validateAndTakeArray(const char *fieldName) {
    JSON json;
    if(fieldName) {
        if(this->validator->validate<Array>(this->root, fieldName)) {
            json = std::move(this->root[fieldName]);
        }
    } else if(this->validator->validate<Array>(this->root)) {
        json = std::move(this->root);
    }
    return json;
}

JSON JSONDeserializer::validateAndTakeObject(const char *fieldName) {
    JSON json;
    if(fieldName) {
        if(this->validator->validate<Object>(this->root, fieldName)) {
            json = std::move(this->root[fieldName]);
        }
    } else if(this->validator->validate<Object>(this->root)) {
        json = std::move(this->root);
    }
    return json;
}

} // namespace json
} // namespace ydsh