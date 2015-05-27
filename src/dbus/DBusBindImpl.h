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

#ifndef DBUS_DBUSBIND_H
#define DBUS_DBUSBIND_H

#include "../core/DSObject.h"
#include "../core/RuntimeContext.h"
#include "DBusUtil.h"

#include <unordered_set>

namespace ydsh {
namespace core {

// represent for SystemBus, SessionBus, or specific bus.
struct Bus_ObjectImpl : public Bus_Object,
                        public std::enable_shared_from_this<Bus_ObjectImpl> {
    DBusConnection *conn;

    /**
     * if true, system bus.
     * if else, session bus.
     */
    bool systemBus;

    Bus_ObjectImpl(DSType *type, bool systemBus);
    ~Bus_ObjectImpl();

    /**
     * get DBusConnection.
     * return false, if error happened.
     */
    bool initConnection(RuntimeContext &ctx, bool systemBus);

    bool isSystemBus();

    bool service(RuntimeContext &ctx, std::string &&serviceName); // override
};

struct Service_ObjectImpl : public Service_Object,
                            public std::enable_shared_from_this<Service_ObjectImpl> {
    std::shared_ptr<Bus_ObjectImpl> bus;

    /**
     * service name of destination process.
     */
    std::string serviceName;

    Service_ObjectImpl(DSType *type, const std::shared_ptr<Bus_ObjectImpl> &bus, std::string &&serviceName);
    ~Service_ObjectImpl();

    std::string toString(RuntimeContext &ctx); // override
    bool object(RuntimeContext &ctx, std::string &&objectPath); // override
};

struct DBus_ObjectImpl : public DBus_Object {
    std::shared_ptr<Bus_ObjectImpl> systemBus;
    std::shared_ptr<Bus_ObjectImpl> sessionBus;
    MessageBuilder builder;

    DBus_ObjectImpl(TypePool *typePool);
    ~DBus_ObjectImpl();

    bool getSystemBus(RuntimeContext &ctx); // override
    bool getSessionBus(RuntimeContext &ctx);    // override

    bool waitSignal(RuntimeContext &ctx);   // override
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

typedef std::unordered_map<SignalSelector, std::shared_ptr<FuncObject>,
        SignalSelectorHash, SignalSelectorComparator> HandlerMap;

// represent for D-Bus object.
class DBusProxy_Object : public ProxyObject {
private:
    std::shared_ptr<Service_ObjectImpl> srv;
    std::string objectPath;

    /**
     * contains having interface name.
     */
    std::unordered_set<std::string> ifaceSet;

    /**
     * contains signal handler
     */
    HandlerMap handerMap;

public:
    DBusProxy_Object(DSType *type, const std::shared_ptr<DSObject> &srcObj, std::string &&objectPath);

    std::string toString(RuntimeContext &ctx); // override
    bool introspect(RuntimeContext &ctx, DSType *targetType); // override

    bool invokeMethod(RuntimeContext &ctx, const std::string &methodName, MethodHandle *handle);    // override
    bool invokeGetter(RuntimeContext &ctx,DSType *recvType,
                      const std::string &fieldName, DSType *fieldType);    // override
    bool invokeSetter(RuntimeContext &ctx, DSType *recvType,
                      const std::string &fieldName, DSType *fieldType);    // override

    const std::string &getObjectPath();

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
    void addHandler(const std::string &ifaceName, const std::string &methodName, std::shared_ptr<DSObject> &&obj);
};

} // namespace core
} // namespace ydsh


#endif //DBUS_DBUSBIND_H
