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

#ifndef DBUS_DBUSBINDIMPL_H
#define DBUS_DBUSBINDIMPL_H

#include "../core/DSObject.h"
#include <unordered_set>

struct DBusConnection;

namespace ydsh {
namespace core {

// represent for SystemBus, SessionBus, or specific bus.
struct Bus_Object : public DSObject {
    DBusConnection *conn;

    Bus_Object(DSType *type);
    ~Bus_Object();

    /**
     * get DBusConnection.
     * return false, if error happened.
     */
    bool initConnection(RuntimeContext &ctx, bool systemBus);
};

// represent for D-Bus object.
struct DBusProxy_Object : public ProxyObject {
    DBusConnection *conn;

    std::string destination;
    std::string objectPath;

    /**
     * contains having interface name.
     */
    std::unordered_set<std::string> ifaceSet;

    DBusProxy_Object(DSType *type, const std::shared_ptr<DSObject> &busObj,
                     std::string &&destination, std::string &&objectPath);

    std::string toString(RuntimeContext &ctx); // override
    bool introspect(RuntimeContext &ctx, DSType *targetType); // override

    /**
     * call only once
     */
    bool doIntrospection(RuntimeContext &ctx);

    bool invokeMethod(RuntimeContext &ctx, const std::string &methodName, MethodHandle *handle);    // override
    bool invokeGetter(RuntimeContext &ctx, const std::string &fieldName, DSType *fieldType);    // override
    bool invokeSetter(RuntimeContext &ctx, const std::string &fieldName, DSType *fieldType);    // override

    static bool newObject(RuntimeContext &ctx, const std::shared_ptr<DSObject> &busObj,
                          std::string &&destination, std::string &&objectPath);
};

} // namespace core
} // namespace ydsh


#endif //DBUS_DBUSBINDIMPL_H
