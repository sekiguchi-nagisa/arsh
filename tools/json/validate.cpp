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

namespace ydsh {
namespace json {

// #########################
// ##     TypeMatcher     ##
// #########################

bool TypeMatcher::match(Validator &validator, const JSON &value) const {
    return validator.match(*this, value);
}

std::string TypeMatcher::str() const {
    return this->name;
}

// ########################
// ##     AnyMatcher     ##
// ########################

bool AnyMatcher::match(Validator &validator, const JSON &value) const {
    return validator.match(*this, value);
}

std::string AnyMatcher::str() const {
    return "any";
}

// ##########################
// ##     ArrayMatcher     ##
// ##########################

bool ArrayMatcher::match(Validator &validator, const JSON &value) const {
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

bool ObjectMatcher::match(Validator &validator, const JSON &value) const {
    return validator.match(*this, value);
}

std::string ObjectMatcher::str() const {
    return this->name;
}

// ##########################
// ##     UnionMatcher     ##
// ##########################

bool UnionMatcher::match(Validator &validator, const JSON &value) const {
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

const TypeMatcherPtr integer = std::make_shared<TypeMatcher>("integer", TAG_LONG);  //NOLINT

const TypeMatcherPtr number = std::make_shared<UnionMatcher>(  //NOLINT
        "number",
        std::make_shared<TypeMatcher>("long", TAG_LONG),
        std::make_shared<TypeMatcher>("double", TAG_DOBULE)
);

const TypeMatcherPtr string = std::make_shared<TypeMatcher>("string", TAG_STRING);  //NOLINT
const TypeMatcherPtr boolean = std::make_shared<TypeMatcher>("boolean", TAG_BOOL);  //NOLINT
const TypeMatcherPtr null = std::make_shared<TypeMatcher>("null", TAG_NULL);    //NOLINT
const TypeMatcherPtr any = std::make_shared<AnyMatcher>();  //NOLINT


// ####################
// ##     Fields     ##
// ####################

Fields::Fields(std::initializer_list<std::pair<std::string, Field>> list) {
    for(auto &e : list) {
        auto &pair = const_cast<std::pair<std::string, json::Field>&>(e);
        this->value[std::move(pair.first)] = std::move(pair.second);
    }
}

// #######################
// ##     Interface     ##
// #######################

bool Interface::match(Validator &validator, const JSON &json) const {
    return validator.match(*this, json);
}

// ###########################
// ##     VoidInterface     ##
// ###########################

bool VoidInterface::match(Validator &validator, const JSON &json) const {
    return validator.match(*this, json);
}


// ##########################
// ##     InterfaceMap     ##
// ##########################

InterfacePtr InterfaceMap::interface(const char *name, Fields &&fields) {
    auto iface = std::make_shared<Interface>(name, std::move(fields));
    return std::static_pointer_cast<Interface>(this->add(std::move(iface)));
}

VoidInterfacePtr InterfaceMap::interface() {
    return std::static_pointer_cast<VoidInterface>(this->add(std::make_shared<VoidInterface>()));
}

InterfaceBasePtr InterfaceMap::add(InterfaceBasePtr &&iface) {
    auto pair = this->map.emplace(iface->getName(), iface);
    if(!pair.second) {
        fatal("already defined interface: %s\n", iface->getName());
    }
    return std::move(iface);
}

const InterfaceBase* InterfaceMap::lookup(const char *name) const {
    auto iter = this->map.find(name);
    if(iter != this->map.end()) {
        return iter->second.get();
    }
    return nullptr;
}

// #######################
// ##     Validator     ##
// #######################

bool Validator::match(const TypeMatcher &matcher, const JSON &value) {
    return matcher.tag == value.tag();
}

bool Validator::match(const AnyMatcher &, const JSON &) {
    return true;
}

bool Validator::match(const ArrayMatcher &matcher, const JSON &value) {
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

bool Validator::match(const ObjectMatcher &matcher, const JSON &value) {
    return this->match(matcher.name, value);
}

bool Validator::match(const UnionMatcher &matcher, const JSON &value) {
    if(matcher.left->match(*this, value)) {
        return true;
    }
    this->errors.clear();
    return matcher.right->match(*this, value);
}

bool Validator::match(const std::string &ifaceName, const JSON &value) {
    if(value.tag() != JSON::Tag<Object>::value) {
        return false;
    }

    if(ifaceName.empty()) {
        return true;
    }
    auto *ptr = this->map.lookup(ifaceName);
    if(ptr == nullptr) {
        fatal("undefined interface: %s\n", ifaceName.c_str());
    }
    return ptr->match(*this, value);
}

bool Validator::match(const Interface &iface, const JSON &value) {
    for(auto &e : iface.getFields()) {
        auto iter = value.asObject().find(e.first);
        if(iter == value.asObject().end()) {
            if(!e.second.isRequire()) {
                continue;
            }
            std::string str = "require field `";
            str += e.first;
            str += "' in `";
            str += iface.getName();
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
            str += iface.getName();
            str += "'";
            this->errors.emplace_back(std::move(str));
            return false;
        }
    }
    return true;
}

bool Validator::match(const VoidInterface &, const JSON &value) {
    if(!value.asObject().empty()) {
        this->errors.emplace_back("must be empty object");
        return false;
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
} // namespace ydsh