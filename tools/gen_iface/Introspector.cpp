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

#include <iostream>
#include <cassert>

#include <config.h>
#include "Introspector.h"

#include <dbus/DBusUtil.h>

static void checkError(DBusError &error) {
    if(dbus_error_is_set(&error)) {
        std::cerr << error.name << " : " << error.message << std::endl;
        dbus_error_free(&error);
        exit(1);
    }
}

static void unrefMessage(DBusMessage *msg) {
    if(msg != nullptr) {
        dbus_message_unref(msg);
    }
}


// ##########################
// ##     Introspector     ##
// ##########################

bool Introspector::operator!() const {
    return !this->available();
}

/**
 * return true, if available D-Bus support.
 */
bool Introspector::available() const {
    return true;
}

/**
 * if introspection failed, return empty string.
 */
std::string Introspector::operator()(bool isSystemBus,
                                     const std::string &serviceName, const std::string &path) const {
    DBusError error;
    dbus_error_init(&error);

    // init connection
    DBusConnection *conn = dbus_bus_get(isSystemBus ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION, &error);
    checkError(error);

    // send message
    auto sendMsg = dbus_message_new_method_call(serviceName.c_str(), path.c_str(),
            "org.freedesktop.DBus.Introspectable", "Introspect");
    auto replyMsg = dbus_connection_send_with_reply_and_block(
            conn, sendMsg, DBUS_TIMEOUT_USE_DEFAULT, &error);
    unrefMessage(sendMsg);
    checkError(error);

    assert(dbus_message_get_type(replyMsg) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
    DBusMessageIter iter;
    dbus_message_iter_init(replyMsg, &iter);
    assert(dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING);
    char *value;
    dbus_message_iter_get_basic(&iter, &value);
    std::string ret(value);

    // free
    unrefMessage(replyMsg);
    dbus_connection_unref(conn);

    return ret;
}