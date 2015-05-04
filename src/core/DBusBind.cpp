/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#include "DBusBind.h"

namespace ydsh {
namespace core {

// #########################
// ##     DBus_Object     ##
// #########################

DBus_Object::DBus_Object(TypePool &pool) :
        DSObject(pool.getDBusType()),
        systemBus(std::make_shared<Bus_Object>(pool.getBusType(), true)),
        sessionBus(std::make_shared<Bus_Object>(pool.getBusType(), false)) {
}

const std::shared_ptr<Bus_Object> &DBus_Object::getSystemBus() {
    return this->systemBus;
}

const std::shared_ptr<Bus_Object> &DBus_Object::getSessionBus() {
    return this->sessionBus;
}

// ########################
// ##     Bus_Object     ##
// ########################

Bus_Object::Bus_Object(DSType *type, bool systemBus) :
        DSObject(type), systemBus(systemBus) {
}

bool Bus_Object::isSystemBus() {
    return this->systemBus;
}

// ###############################
// ##     Connection_Object     ##
// ###############################

Connection_Object::Connection_Object(DSType *type, const std::shared_ptr<DSObject> &destination) :
        DSObject(type), destination(destination) {
}

// ##############################
// ##     DBusProxy_Object     ##
// ##############################

DBusProxy_Object::DBusProxy_Object(DSType *type, bool systemBus,
                                   std::string &&destination, std::string &&objectPath) :
        DSObject(type), systemBus(systemBus), destination(destination), objectPath(objectPath) {
}

std::string DBusProxy_Object::toString(RuntimeContext &ctx) {
    std::string str("[dest=");
    str += this->destination;
    str += ", path=";
    str += this->objectPath;
    str += "]";
    return str;
}

} // namespace core
} // namespace ydsh