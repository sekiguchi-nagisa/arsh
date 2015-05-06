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
#include "RuntimeContext.h"

namespace ydsh {
namespace core {


// #########################
// ##     DBus_Object     ##
// #########################

DBus_Object::DBus_Object(TypePool &pool) :
        DSObject(pool.getDBusType()), systemBus(), sessionBus() {
}

bool DBus_Object::getSystemBus(RuntimeContext &ctx) {
    if(!this->systemBus) {
        this->systemBus.reset(new Bus_Object(ctx.pool.getBusType()));
        if(!this->systemBus->initConnection(ctx, true)) {
            return false;
        }
    }
    ctx.returnObject = this->systemBus;
    return true;
}

bool DBus_Object::getSessionBus(RuntimeContext &ctx) {
    if(!this->sessionBus) {
        this->sessionBus.reset(new Bus_Object(ctx.pool.getBusType()));
        if(!this->sessionBus->initConnection(ctx, false)) {
            return false;
        }
    }
    ctx.returnObject = this->sessionBus;
    return true;
}

bool newObject(RuntimeContext &ctx, const std::shared_ptr<DSObject> &busObj,
               std::string &&destination, std::string &&objectPath) {
#ifdef X_NO_DBUS
    ctx.throwError(ctx.pool.getErrorType(), "not support D-Bus proxy object");
    return false;
#else
    return DBusProxy_Object::newObject(ctx, busObj, std::move(destination), std::move(objectPath));
#endif
}

} // namespace core
} // namespace ydsh