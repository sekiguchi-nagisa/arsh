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
#include "../core/FieldHandle.h"
#include "../core/RuntimeContext.h"

extern "C" {
#include <dbus/dbus.h>
}


namespace ydsh {
namespace core {

// helper util
static void reportError(RuntimeContext &ctx, DBusError &error) {
    std::string name(error.name);
    DSType *type = ctx.pool.createAndGetErrorTypeIfUndefined(name, ctx.pool.getErrorType());
    ctx.throwError(type, error.message);
}

static void message_unref(DBusMessage *msg) {
    if(msg != nullptr) {
        dbus_message_unref(msg);
    }
}


// ########################
// ##     Bus_Object     ##
// ########################

Bus_Object::Bus_Object(DSType *type) :
        DSObject(type), conn() {
}

Bus_Object::~Bus_Object() {
    if(this->conn != nullptr) {
        dbus_connection_unref(this->conn);
    }
}

bool Bus_Object::initConnection(RuntimeContext &ctx, bool systemBus) {
    // get connection
    DBusError error;
    dbus_error_init(&error);

    this->conn = dbus_bus_get(systemBus ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION, &error);
    if(dbus_error_is_set(&error)) {
        reportError(ctx, error);
        dbus_error_free(&error);
        return false;
    }

    if(this->conn == nullptr) {
        fatal("must not null\n");
    }
    return true;
}

// #############################
// ##     DBus_ObjectImpl     ##
// #############################

DBus_ObjectImpl::DBus_ObjectImpl(DSType *type) :
        DBus_Object(type), systemBus(), sessionBus() {
}

DBus_ObjectImpl::~DBus_ObjectImpl() {
}

bool DBus_ObjectImpl::getSystemBus(RuntimeContext &ctx) {
    if(!this->systemBus) {
        this->systemBus.reset(new Bus_Object(ctx.pool.getBusType()));
        if(!this->systemBus->initConnection(ctx, true)) {
            return false;
        }
    }
    ctx.returnObject = this->systemBus;
    return true;
}

bool DBus_ObjectImpl::getSessionBus(RuntimeContext &ctx) {
    if(!this->sessionBus) {
        this->sessionBus.reset(new Bus_Object(ctx.pool.getBusType()));
        if(!this->sessionBus->initConnection(ctx, false)) {
            return false;
        }
    }
    ctx.returnObject = this->sessionBus;
    return true;
}


// ##############################
// ##     DBusProxy_Object     ##
// ##############################

DBusProxy_Object::DBusProxy_Object(DSType *type, const std::shared_ptr<DSObject> &busObj,
                                   std::string &&destination, std::string &&objectPath) :
        ProxyObject(type), conn(0), destination(destination), objectPath(objectPath), ifaceSet() {
    this->conn = TYPE_AS(Bus_Object, busObj)->conn;
}

std::string DBusProxy_Object::toString(RuntimeContext &ctx) {
    std::string str("[dest=");
    str += this->destination;
    str += ", path=";
    str += this->objectPath;
    str += ", iface=";
    unsigned int count = 0;
    for(auto &iter : this->ifaceSet) {
        if(count++ > 0) {
            str += ", ";
        }
        str += iter;
    }
    str += "]";
    return str;
}

bool DBusProxy_Object::introspect(RuntimeContext &ctx, DSType *targetType) {
    const std::string &typeName = ctx.pool.getTypeName(*targetType);
    auto iter = this->ifaceSet.find(typeName);
    return iter != this->ifaceSet.end();
}

static void extractInterfaceName(std::unordered_set<std::string> &ifaceSet, char *str) {
    static const char prefix[] = "<interface name=";

    for(unsigned int i = 0; str[i] != '\0'; i++) {
        bool match = true;
        for(unsigned int j = 0; prefix[j] != '\0'; j++) {
            if(prefix[j] != str[i]) {
                match = false;
                break;
            }
            i++;
        }

        if(!match) {
            continue;
        }

        std::string buf;
        bool finish = false;
        while(str[i] != '\0' && !finish) {
            char ch = str[i++];
            switch(ch) {
            case '"': {
                if(!buf.empty()) {
                    finish = true;
                    i--;
                }
                break;
            };
            default:
                buf += ch;
                break;
            }
        }

        ifaceSet.insert(std::move(buf));
    }
}

bool DBusProxy_Object::doIntrospection(RuntimeContext &ctx) {
    DBusError error;
    dbus_error_init(&error);

    if(!dbus_validate_bus_name(this->destination.c_str(), &error)) {
        reportError(ctx, error);
        return false;
    }

    DBusMessage *msg = dbus_message_new_method_call(
            this->destination.c_str(), this->objectPath.c_str(),
            "org.freedesktop.DBus.Introspectable", "Introspect");

    DBusMessage *ret = dbus_connection_send_with_reply_and_block(this->conn, msg, DBUS_TIMEOUT_USE_DEFAULT, &error);
    message_unref(msg);

    if(dbus_error_is_set(&error)) {
        reportError(ctx, error);

        dbus_error_free(&error);
        message_unref(ret);

        return false;
    }

    int retType = dbus_message_get_type(ret);
    switch(retType) {
    case DBUS_MESSAGE_TYPE_ERROR: {
        fatal("dbus error: name=%s\n", dbus_message_get_error_name(ret));
        break;
    };
    case DBUS_MESSAGE_TYPE_METHOD_RETURN: {
        DBusMessageIter iter;
        dbus_message_iter_init(ret, &iter);

        int argType = dbus_message_iter_get_arg_type(&iter);
        if(argType == DBUS_TYPE_STRING) {
            char *value;
            dbus_message_iter_get_basic(&iter, &value);
            extractInterfaceName(this->ifaceSet, value);
        } else {
            fatal("invalied argType\n");
        }
    };
    }
    message_unref(ret);

    return true;
}

bool DBusProxy_Object::invokeMethod(RuntimeContext &ctx, const std::string &methodName, MethodHandle *handle) {

    fatal("unimplemented\n");
    return false;
}

bool DBusProxy_Object::invokeGetter(RuntimeContext &ctx, const std::string &fieldName, DSType *fieldType) {
    fatal("unimplemented\n");
    return false;
}

bool DBusProxy_Object::invokeSetter(RuntimeContext &ctx, const std::string &fieldName, DSType *fieldType) {
    fatal("unimplemented\n");
    return false;
}

bool DBusProxy_Object::newObject(RuntimeContext &ctx, const std::shared_ptr<DSObject> &busObj,
                                 std::string &&destination, std::string &&objectPath) {
    std::shared_ptr<DBusProxy_Object> obj(
            new DBusProxy_Object(ctx.pool.getDBusObjectType(), busObj,
                                 std::move(destination), std::move(objectPath)));

    // first call Introspection and resolve interface type.
    if(!obj->doIntrospection(ctx)) {
        return false;
    }

    ctx.returnObject = std::move(obj);
    return true;
}

} // namespace core
} // namespace ydsh