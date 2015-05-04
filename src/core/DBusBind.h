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

namespace ydsh {
namespace core {

// represent for SystemBus, SessionBus, or specific bus.
struct Bus_Object : public DSObject {
    /**
     * if true, SystemBus.
     * if false, SessionBus.
     */
    bool systemBus;

    Bus_Object(DSType *type, bool systemBus);

    bool isSystemBus();
};

// management object for some D-Bus related function (ex. Bus)
struct DBus_Object : public DSObject {  //FIXME:
    std::shared_ptr<Bus_Object> systemBus;
    std::shared_ptr<Bus_Object> sessionBus;

    DBus_Object(TypePool &pool);

    const std::shared_ptr<Bus_Object> &getSystemBus();
    const std::shared_ptr<Bus_Object> &getSessionBus();
};

// represent for connection
struct Connection_Object : public DSObject {    //FIXME:
    /**
     * actually, String_Object
     */
    std::shared_ptr<DSObject> destination;

    Connection_Object(DSType *type, const std::shared_ptr<DSObject> &destination);
};

// represent for D-Bus object.
struct DBusProxy_Object : public DSObject { //FIXME: implemented interface
    /**
     * if true, SystemBus.
     * if false, SessionBus.
     */
    bool systemBus;

    std::string destination;
    std::string objectPath;

    DBusProxy_Object(DSType *type, bool systemBus,
                     std::string &&destination, std::string &&objectPath);
    std::string toString(RuntimeContext &ctx); // override
};


} // namespace core
} // namespace ydsh

#endif //CORE_DBUSBIND_H
