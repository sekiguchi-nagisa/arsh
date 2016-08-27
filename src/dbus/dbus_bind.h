/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_DBUS_DBUS_BIND_IMPL_H
#define YDSH_DBUS_DBUS_BIND_IMPL_H

#include <unordered_set>

#include "object.h"
#include "core.h"
#include "dbus_util.h"

namespace ydsh {

struct DBusMessageDeleter {
    void operator()(DBusMessage *msg) const {
        if(msg != nullptr) {
            dbus_message_unref(msg);
        }
    }
};

using ScopedDBusMessage = std::unique_ptr<DBusMessage, DBusMessageDeleter>;

// represent for SystemBus, SessionBus, or specific bus.
class Bus_Object : public DSObject {
private:
    DBusConnection *conn;

    /**
     * if true, system bus.
     * if else, session bus.
     */
    bool systemBus;

public:
    Bus_Object(DSType &type, bool systemBus);
    ~Bus_Object();

    /**
     * get DBusConnection.
     */
    void initConnection(DSState &ctx, bool systemBus);

    DBusConnection *getConnection() {
        return this->conn;
    }

    bool isSystemBus() {
        return this->systemBus;
    }

    DSValue service(DSState &ctx, std::string &&serviceName);
    DSValue listNames(DSState &ctx, bool activeName);
};

class Service_Object : public DSObject {
private:
    /**
     * must be Bus_Object
     */
    DSValue bus;

    /**
     * service name of destination process.
     */
    std::string serviceName;

    /**
     * unique connection name.
     */
    std::string uniqueName;

public:
    Service_Object(DSType &type, const DSValue &bus,
                       std::string &&serviceName, std::string &&uniqueName);
    ~Service_Object() = default;

    const DSValue &getBus() const {
        return this->bus;
    }

    DBusConnection *getConnection() const {
        return typeAs<Bus_Object>(this->bus)->getConnection();
    }

    const char *getServiceName() const {
        return this->serviceName.c_str();
    }

    const char *getUniqueName() const {
        return this->uniqueName.c_str();
    }

    std::string toString(DSState &ctx, VisitedSet *set) override;

    /**
     * objectPath is String_Object
     */
    DSValue object(DSState &ctx, const DSValue &objectPath);
};

class DBus_Object : public DSObject {
private:
    /**
     * must be Bus_Object
     */
    DSValue systemBus;

    /**
     * must be Bus_Object
     */
    DSValue sessionBus;

    MessageBuilder builder;

public:
    explicit DBus_Object(TypePool &typePool);
    ~DBus_Object() = default;

    /**
     * return null, before call getSystemBus(ctx)
     */
    const DSValue &getSystemBus() {
        return this->systemBus;
    }

    /**
     * return null, before call getSessionBus(ctx)
     */
    const DSValue &getSessionBus() {
        return this->sessionBus;
    }

    MessageBuilder &getBuilder() {
        return this->builder;
    }

    /**
     * init and get Bus_Object representing for system bus.
     * return false, if error happened
     */
    DSValue getSystemBus(DSState &ctx);

    /**
     * init and get Bus_Object representing for session bus.
     * return false, if error happened
     */
    DSValue getSessionBus(DSState &ctx);

    void initSignalMatchRule(DSState &st);
    std::vector<DSValue> waitSignal(DSState &st);

    DSValue getServiceFromProxy(DSState &ctx, const DSValue &proxy);
    DSValue getObjectPathFromProxy(DSState &ctx, const DSValue &proxy);
    DSValue getIfaceListFromProxy(DSState &ctx, const DSValue &proxy);
    DSValue introspectProxy(DSState &ctx, const DSValue &proxy);
};

/**
 * first is interface name, second is method name.
 */
typedef std::pair<const char *, const char *> SignalSelector;

struct SignalSelectorComparator {
    bool operator() (const SignalSelector &x,
                     const SignalSelector &y) const;
};

struct SignalSelectorHash {
    std::size_t operator() (const SignalSelector &key) const;
};

typedef std::unordered_map<SignalSelector, DSValue,
        SignalSelectorHash, SignalSelectorComparator> HandlerMap;

// represent for D-Bus object.
class DBusProxy_Object : public ProxyObject {
private:
    /**
     * must be Service_Object
     */
    DSValue srv;

    /**
     * must be String_Object
     */
    DSValue objectPath;

    /**
     * contains having interface name.
     */
    std::unordered_set<std::string> ifaceSet;

    /**
     * contains signal handler(handler is FuncObject)
     */
    HandlerMap handerMap;

public:
    /**
     * objectPath must be String_Object
     */
    DBusProxy_Object(DSType &type, const DSValue &srcObj, const DSValue &objectPath);

    ~DBusProxy_Object() = default;

    std::string toString(DSState &ctx, VisitedSet *set) override;
    bool introspect(DSState &ctx, DSType *targetType) override;

    DSValue invokeMethod(DSState &ctx, const char *methodName, const MethodHandle *handle) override;
    DSValue invokeGetter(DSState &ctx, const DSType *recvType,
                         const char *fieldName, const DSType *fieldType) override;
    void invokeSetter(DSState &ctx, const DSType *recvType,
                      const char *fieldName, const DSType *fieldType) override;

    const DSValue &getService();
    const DSValue &getObjectPath();

    /**
     * return Array_Object
     */
    DSValue createIfaceList(DSState &ctx);

    /**
     * lookup signal handler
     * @param ifaceName
     * @param methodName
     * @return
     * must be FuncObject
     */
    DSValue lookupHandler(const char *ifaceName, const char *methodName) const {
        auto iter = this->handerMap.find(std::make_pair(ifaceName, methodName));
        if(iter == this->handerMap.end()) {
            return nullptr;
        }
        return iter->second;
    }

    bool matchObject(const char *serviceName, const char *objectPath);

    /**
     * create signal match rule and write to ruleList.
     */
    void createSignalMatchRule(std::vector<std::string> &ruleList);

    bool isBelongToSystemBus();

    /**
     * call only once
     */
    void doIntrospection(DSState &ctx);

private:
    ScopedDBusMessage newMethodCallMsg(const char *ifaceName, const char *methodName);

    /**
     * send message and unref send message.
     * return reply message.
     */
    ScopedDBusMessage sendMessage(DSState &ctx, ScopedDBusMessage &&sendMsg);

    /**
     * obj must be FuncObject
     */
    void addHandler(const char *ifaceName, const char *methodName, const DSValue &obj);
};

} // namespace ydsh


#endif //YDSH_DBUS_DBUS_BIND_IMPL_H
