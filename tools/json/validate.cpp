/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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