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

#ifndef CORE_DBUSBIND_H
#define CORE_DBUSBIND_H

#include "DSObject.h"

#ifndef X_NO_DBUS
#include "../dbus/DBusBindImpl.h"
#endif

namespace ydsh {
namespace core {

#ifdef X_NO_DBUS
struct Bus_Object : public DSObject {
    Bus_Object(DSType *type) :
            DSObject(type) {
    }

    bool initConnection(RuntimeContext &ctx, bool systemBus) {
        return true;
    }
};
#endif

// management object for some D-Bus related function (ex. Bus)
struct DBus_Object : public DSObject {
    std::shared_ptr<Bus_Object> systemBus;
    std::shared_ptr<Bus_Object> sessionBus;

    DBus_Object(TypePool &pool);

    /**
     * init and get Bus_Object representing for system bus.
     * return false, if error happened
     */
    bool getSystemBus(RuntimeContext &ctx);

    /**
     * init and get Bus_Object representing for session bus.
     * return false, if error happened
     */
    bool getSessionBus(RuntimeContext &ctx);
};

bool newObject(RuntimeContext &ctx, const std::shared_ptr<DSObject> &busObj,
               std::string &&destination, std::string &&objectPath);


} // namespace core
} // namespace ydsh

#endif //CORE_DBUSBIND_H
