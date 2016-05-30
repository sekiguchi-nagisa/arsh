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

#include "../core/object.h"
#include "../core/context.h"
#include "dbus_util.h"

namespace ydsh {

// represent for SystemBus, SessionBus, or specific bus.
class Bus_ObjectImpl : public Bus_Object {
private:
    DBusConnection *conn;

    /**
     * if true, system bus.
     * if else, session bus.
     */
    bool systemBus;

public:
    Bus_ObjectImpl(DSType &type, bool systemBus);
    ~Bus_ObjectImpl();

    /**
     * get DBusConnection.
     * return false, if error happened.
     */
    bool initConnection(RuntimeContext &ctx, bool systemBus);

    DBusConnection *getConnection() {
        return this->conn;
    }

    bool isSystemBus() {
        return this->systemBus;
    }

    bool service(RuntimeContext &ctx, std::string &&serviceName) override;
    bool listNames(RuntimeContext &ctx, bool activeName) override;
};

class Service_ObjectImpl : public Service_Object {
private:
    /**
     * must be Bus_ObjectImpl
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
    Service_ObjectImpl(DSType &type, const DSValue &bus,
                       std::string &&serviceName, std::string &&uniqueName);
    ~Service_ObjectImpl() = default;

    const DSValue &getBus() const {
        return this->bus;
    }

    DBusConnection *getConnection() const {
        return typeAs<Bus_ObjectImpl>(this->bus)->getConnection();
    }

    const char *getServiceName() const {
        return this->serviceName.c_str();
    }

    const char *getUniqueName() const {
        return this->uniqueName.c_str();
    }

    std::string toString(RuntimeContext &ctx, VisitedSet *set) override;
    bool object(RuntimeContext &ctx, const DSValue &objectPath) override;
};

class DBus_ObjectImpl : public DBus_Object {
private:
    /**
     * must be Bus_ObjectImpl
     */
    DSValue systemBus;

    /**
     * must be Bus_ObjectImpl
     */
    DSValue sessionBus;

    MessageBuilder builder;

public:
    explicit DBus_ObjectImpl(TypePool *typePool);
    ~DBus_ObjectImpl() = default;

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

    bool getSystemBus(RuntimeContext &ctx) override;
    bool getSessionBus(RuntimeContext &ctx) override;

    bool waitSignal(RuntimeContext &ctx) override;
    bool getServiceFromProxy(RuntimeContext &ctx, const DSValue &proxy) override;
    bool getObjectPathFromProxy(RuntimeContext &ctx, const DSValue &proxy) override;
    bool getIfaceListFromProxy(RuntimeContext &ctx, const DSValue &proxy) override;
    bool introspectProxy(RuntimeContext &ctx, const DSValue &proxy) override;
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
     * must be Service_ObjectImpl
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

    std::string toString(RuntimeContext &ctx, VisitedSet *set) override;
    bool introspect(RuntimeContext &ctx, DSType *targetType) override;

    bool invokeMethod(RuntimeContext &ctx, const std::string &methodName, MethodHandle *handle) override;
    bool invokeGetter(RuntimeContext &ctx,DSType *recvType,
                      const std::string &fieldName, DSType *fieldType) override;
    bool invokeSetter(RuntimeContext &ctx, DSType *recvType,
                      const std::string &fieldName, DSType *fieldType) override;

    const DSValue &getService();
    const DSValue &getObjectPath();

    /**
     * return Array_Object
     */
    DSValue createIfaceList(RuntimeContext &ctx);

    /**
     * lookup signal handler and push stack top. return func type of found handler.
     */
    FunctionType  *lookupHandler(RuntimeContext &ctx, const char *ifaceName, const char *methodName);

    bool matchObject(const char *serviceName, const char *objectPath);

    /**
     * create signal match rule and write to ruleList.
     */
    void createSignalMatchRule(std::vector<std::string> &ruleList);

    bool isBelongToSystemBus();

    /**
     * call only once
     */
    bool doIntrospection(RuntimeContext &ctx);

private:
    DBusMessage *newMethodCallMsg(const char *ifaceName, const char *methodName);
    DBusMessage *newMethodCallMsg(const std::string &ifaceName, const std::string &methodName);

    /**
     * send message and unref send message.
     * return reply message.
     */
    DBusMessage *sendMessage(RuntimeContext &ctx, DBusMessage *sendMsg, bool &status);

    /**
     * obj must be FuncObject
     */
    void addHandler(const std::string &ifaceName, const std::string &methodName, const DSValue &obj);
};

} // namespace ydsh


#endif //YDSH_DBUS_DBUS_BIND_IMPL_H
