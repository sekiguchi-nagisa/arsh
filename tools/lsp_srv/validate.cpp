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
    std::string str = this->left->str();
    str += " | ";
    str += this->right->str();
    return str;
}


// #######################
// ##     Interface     ##
// #######################

bool Interface::match(Validator &, const JSON &value) const {
    if(value.tag() != JSON::Tag<Object>::value) {
        return false;
    }

    return true;
}

Interface& Interface::addField(const char *name, MatcherPtr type, bool opt) {
    auto pair = this->fields.emplace(name, Field(type, opt));
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

bool Validator::match(const json::ObjectMatcher &matcher, const json::JSON &value) {
    return this->match(matcher.name, value);
}

bool Validator::match(const json::UnionMatcher &matcher, const json::JSON &value) {
    return matcher.left->match(*this, value) || matcher.right->match(*this, value);
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
            if(e.second.optional()) {
                continue;
            }
            std::string str = "require field `";
            str += e.first;
            str += "' at interface `";
            str += ifaceName;
            str += "'";
            this->errors.emplace_back(std::move(str));
            return false;
        }

        if(!e.second.getMatcher().match(*this, iter->second)) {
            std::string str = "require type `";
            str += e.second.getMatcher().str();
            str += "' at interface `";
            str += ifaceName;
            str += "'";
            this->errors.emplace_back(std::move(str));
            return false;
        }
    }
    return true;
}

} // namespace json