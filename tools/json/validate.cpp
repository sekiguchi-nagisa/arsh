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

// #######################
// ##     Validator     ##
// #######################

bool Validator::operator()(const std::string &ifaceName, const JSON &value) {
    if(value.tag() != JSON::TAG<Object>) {
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

// #########################
// ##     TypeMatcher     ##
// #########################

bool TypeMatcher::operator()(Validator &, const JSON &value) const {
    return this->tag == value.tag();
}

std::string TypeMatcher::str() const {
    return this->name;
}

// ########################
// ##     AnyMatcher     ##
// ########################

bool AnyMatcher::operator()(Validator &, const JSON &) const {
    return true;
}

std::string AnyMatcher::str() const {
    return "any";
}

// ##########################
// ##     ArrayMatcher     ##
// ##########################

bool ArrayMatcher::operator()(Validator &validator, const JSON &value) const {
    if(this->tag != value.tag()) {
        return false;
    }
    for(auto &e : value.asArray()) {
        if(!(*this->matcher)(validator, e)) {
            return false;
        }
    }
    return true;
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

bool ObjectMatcher::operator()(Validator &validator, const JSON &value) const {
    return validator(this->name, value);
}

std::string ObjectMatcher::str() const {
    return this->name;
}

// ##########################
// ##     UnionMatcher     ##
// ##########################

bool UnionMatcher::operator()(Validator &validator, const JSON &value) const {
    if((*this->left)(validator, value)) {
        return true;
    }
    validator.clearError();
    return (*this->right)(validator, value);
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

static constexpr int TAG_LONG = JSON::TAG<long>;
static constexpr int TAG_DOUBLE = JSON::TAG<double>;
static constexpr int TAG_STRING = JSON::TAG<String>;
static constexpr int TAG_BOOL = JSON::TAG<bool>;
static constexpr int TAG_NULL = JSON::TAG<std::nullptr_t>;

const TypeMatcherPtr integer = std::make_shared<TypeMatcher>("integer", TAG_LONG);  //NOLINT

const TypeMatcherPtr number = std::make_shared<UnionMatcher>(  //NOLINT
        "number",
        std::make_shared<TypeMatcher>("long", TAG_LONG),
        std::make_shared<TypeMatcher>("double", TAG_DOUBLE)
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
    for(auto &e : this->getFields()) {
        auto iter = json.asObject().find(e.first);
        if(iter == json.asObject().end()) {
            if(!e.second.isRequire()) {
                continue;
            }
            std::string str = "require field `";
            str += e.first;
            str += "' in `";
            str += this->getName();
            str += "'";
            validator.appendError(std::move(str));
            return false;
        }

        if(!e.second.getMatcher()(validator, iter->second)) {
            std::string str = "field `";
            str += e.first;
            str += "' requires `";
            str += e.second.getMatcher().str();
            str += "' type in `";
            str += this->getName();
            str += "'";
            validator.appendError(std::move(str));
            return false;
        }
    }
    return true;
}

// ###########################
// ##     VoidInterface     ##
// ###########################

bool VoidInterface::match(Validator &validator, const JSON &json) const {
    if(!json.asObject().empty()) {
        validator.appendError("must be empty object");
        return false;
    }
    return true;
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

} // namespace json
} // namespace ydsh