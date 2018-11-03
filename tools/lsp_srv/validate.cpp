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

#include "jsonrpc.h"

namespace json {

// #########################
// ##     TypeMatcher     ##
// #########################

bool TypeMatcher::match(json::Validator &validator, const json::JSON &value) const {
    return validator.match(*this, value);
}

std::string TypeMatcher::str() const {
    return this->name;
}

// ########################
// ##     AnyMatcher     ##
// ########################

bool AnyMatcher::match(json::Validator &validator, const json::JSON &value) const {
    return validator.match(*this, value);
}

std::string AnyMatcher::str() const {
    return "any";
}

// ##########################
// ##     ArrayMatcher     ##
// ##########################

bool ArrayMatcher::match(json::Validator &validator, const json::JSON &value) const {
    return validator.match(*this, value);
}

std::string ArrayMatcher::str() const {
    std::string str = this->name;
    str += "<";
    str += this->matcher->str();
    str += ">";
    return str;
}


// ###########################
// ##     ObjectMatcher     ##
// ###########################

bool ObjectMatcher::match(json::Validator &validator, const json::JSON &value) const {
    return validator.match(*this, value);
}

std::string ObjectMatcher::str() const {
    return this->name;
}

// ##########################
// ##     UnionMatcher     ##
// ##########################

bool UnionMatcher::match(json::Validator &validator, const json::JSON &value) const {
    return validator.match(*this, value);
}

std::string UnionMatcher::str() const {
    if(!this->name.empty()) {
        return this->name;
    }

    std::string str = this->left->str();
    str += " | ";
    str += this->right->str();
    return str;
}

static constexpr int TAG_LONG = JSON::Tag<long>::value;
static constexpr int TAG_DOBULE = JSON::Tag<double>::value;
static constexpr int TAG_STRING = JSON::Tag<String>::value;
static constexpr int TAG_BOOL = JSON::Tag<bool>::value;
static constexpr int TAG_NULL = JSON::Tag<std::nullptr_t>::value;

const TypeMatcherPtr number = std::make_shared<UnionMatcher>(
        "number",
        std::make_shared<TypeMatcher>("long", TAG_LONG),
        std::make_shared<TypeMatcher>("double", TAG_DOBULE)
);

const TypeMatcherPtr string = std::make_shared<TypeMatcher>("string", TAG_STRING);
const TypeMatcherPtr boolean = std::make_shared<TypeMatcher>("boolean", TAG_BOOL);
const TypeMatcherPtr null = std::make_shared<TypeMatcher>("null", TAG_NULL);
const TypeMatcherPtr any = std::make_shared<AnyMatcher>();


// #######################
// ##     Interface     ##
// #######################

Interface& Interface::field(const char *name, json::TypeMatcherPtr type, bool require) {
    auto pair = this->fields.emplace(name, Field(std::move(type), require));
    if(!pair.second) {
        fatal("already defined field: %s\n", name);
    }
    return *this;
}

// ##########################
// ##     InterfaceMap     ##
// ##########################

Interface& InterfaceMap::interface(const char *name) {
    auto pair = this->map.emplace(name, Interface());
    if(!pair.second) {
        fatal("already defined interface: %s\n", name);
    }
    return pair.first->second;
}

const Interface* InterfaceMap::lookup(const std::string &name) const {
    auto iter = this->map.find(name);
    if(iter != this->map.end()) {
        return &iter->second;
    }
    return nullptr;
}

// #######################
// ##     Validator     ##
// #######################

bool Validator::match(const TypeMatcher &matcher, const json::JSON &value) {
    return matcher.tag == value.tag();
}

bool Validator::match(const json::AnyMatcher &, const json::JSON &) {
    return true;
}

bool Validator::match(const json::ArrayMatcher &matcher, const json::JSON &value) {
    if(matcher.tag != value.tag()) {
        return false;
    }
    for(auto &e : value.asArray()) {
        if(!matcher.matcher->match(*this, e)) {
            return false;
        }
    }
    return true;
}

bool Validator::match(const json::ObjectMatcher &matcher, const json::JSON &value) {
    return this->match(matcher.name, value);
}

bool Validator::match(const json::UnionMatcher &matcher, const json::JSON &value) {
    if(matcher.left->match(*this, value)) {
        return true;
    }
    this->errors.clear();
    return matcher.right->match(*this, value);
}

bool Validator::match(const std::string &ifaceName, const JSON &value) {
    auto *ptr = this->map.lookup(ifaceName);
    if(ptr == nullptr) {
        fatal("undefined interface: %s\n", ifaceName.c_str());
    }
    auto &iface = *ptr;

    if(value.tag() != JSON::Tag<Object>::value) {
        return false;
    }
    for(auto &e : iface.getFields()) {
        auto iter = value.asObject().find(e.first);
        if(iter == value.asObject().end()) {
            if(!e.second.isRequire()) {
                continue;
            }
            std::string str = "require field `";
            str += e.first;
            str += "' in `";
            str += ifaceName;
            str += "'";
            this->errors.emplace_back(std::move(str));
            return false;
        }

        if(!e.second.getMatcher().match(*this, iter->second)) {
            std::string str = "field `";
            str += e.first;
            str += "' requires `";
            str += e.second.getMatcher().str();
            str += "' type in `";
            str += ifaceName;
            str += "'";
            this->errors.emplace_back(std::move(str));
            return false;
        }
    }
    return true;
}

std::string Validator::formatError() const {
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

} // namespace json