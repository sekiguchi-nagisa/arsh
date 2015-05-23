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
#include "../core/DSType.h"

#include <unordered_set>

struct DBusConnection;
struct DBusMessageIter;
struct DBusMessage;

namespace ydsh {
namespace core {

/**
 * for base type descriptor
 */
class BaseTypeDescriptorMap {
private:
    std::unordered_map<unsigned long, int> map;

public:
    BaseTypeDescriptorMap(TypePool *pool);

    /**
     * return DBUS_TYPE_INVALID, if not base type.
     */
    int getDescriptor(DSType *type);
};


/**
 * for container type descriptor generation.
 */
class DescriptorBuilder : public TypeVisitor {
private:
    TypePool *pool;
    BaseTypeDescriptorMap *typeMap;
    std::string buf;

public:
    DescriptorBuilder(TypePool *pool, BaseTypeDescriptorMap *typeMap);
    ~DescriptorBuilder();

    const char *buildDescriptor(DSType *type);

    void visitFunctionType(FunctionType *type) override;
    void visitBuiltinType(BuiltinType *type) override;
    void visitReifiedType(ReifiedType *type) override;
    void visitTupleType(TupleType *type) override;
    void visitInterfaceType(InterfaceType *type) override;
    void visitErrorType(ErrorType *type) override;

private:
    void append(char ch);
};

class MessageBuilder : public TypeVisitor {
private:
    TypePool *pool;

    BaseTypeDescriptorMap *typeMap;
    DescriptorBuilder *descBuilder;

    std::vector<DSObject *> objStack;

    DBusMessageIter *iter;

public:
    MessageBuilder(TypePool *pool);
    ~MessageBuilder();
    void appendArg(DBusMessageIter *iter, DSType *argType, const std::shared_ptr<DSObject> &arg);

    void visitFunctionType(FunctionType *type) override;
    void visitBuiltinType(BuiltinType *type) override;
    void visitReifiedType(ReifiedType *type) override;
    void visitTupleType(TupleType *type) override;
    void visitInterfaceType(InterfaceType *type) override;
    void visitErrorType(ErrorType *type) override;

private:
    DSObject *peek();
    void append(DSType *type, DSObject *value);
    DescriptorBuilder *getBuilder();

    /**
     * open container iter and set it to this->iter. return old iter
     */
    DBusMessageIter *openContainerIter(int dbusType, const char *desc, DBusMessageIter *subIter);

    /**
     * close container iter and set parentIter to this->iter,
     */
    void closeContainerIter(DBusMessageIter *parentIter, DBusMessageIter *subIter);
};

// represent for SystemBus, SessionBus, or specific bus.
struct Bus_Object : public DSObject {
    DBusConnection *conn;

    /**
     * if true, system bus.
     * if else, session bus.
     */
    bool systemBus;

    Bus_Object(DSType *type, bool systemBus);
    ~Bus_Object();

    /**
     * get DBusConnection.
     * return false, if error happened.
     */
    bool initConnection(RuntimeContext &ctx, bool systemBus);

    bool isSystemBus();
};

struct Service_Object : public DSObject {
    std::shared_ptr<Bus_Object> bus;

    /**
     * service name of destination process.
     */
    std::string serviceName;

    Service_Object(DSType *type, const std::shared_ptr<Bus_Object> &bus, std::string &&serviceName);
    ~Service_Object();

    std::string toString(RuntimeContext &ctx); // override

    static bool newServiceObject(RuntimeContext &ctx,
                                 const std::shared_ptr<DSObject> &obj, std::string &&serviceName);
};

struct DBus_ObjectImpl : public DBus_Object {
    std::shared_ptr<Bus_Object> systemBus;
    std::shared_ptr<Bus_Object> sessionBus;
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
    std::shared_ptr<Service_Object> srv;
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

private:
    /**
     * call only once
     */
    bool doIntrospection(RuntimeContext &ctx);

    DBusMessage *newMethodCallMsg(const char *ifaceName, const char *methodName);
    DBusMessage *newMethodCallMsg(const std::string &ifaceName, const std::string &methodName);

    /**
     * send message.
     */
    DBusMessage *sendAndUnrefMessage(RuntimeContext &ctx, DBusMessage *sendMsg, bool &status);

    /**
     * obj must be FuncObject
     */
    void addHandler(const std::string &ifaceName, const std::string &methodName, std::shared_ptr<DSObject> &&obj);

public:
    static bool newObject(RuntimeContext &ctx, const std::shared_ptr<DSObject> &busObj,
                          std::string &&objectPath);
};

} // namespace core
} // namespace ydsh


#endif //DBUS_DBUSBIND_H
