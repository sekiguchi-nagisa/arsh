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
    unsigned int usedSize;
    unsigned int size;
    char *buf;

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
    void push(DSObject *obj);
    DSObject *pop();
    DSObject *peek();
    void append(DSType *type, DSObject *value);
    DescriptorBuilder *getBuilder();
};

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

struct DBus_ObjectImpl : public DBus_Object {
    std::shared_ptr<Bus_Object> systemBus;
    std::shared_ptr<Bus_Object> sessionBus;
    MessageBuilder builder;

    DBus_ObjectImpl(TypePool *typePool);
    ~DBus_ObjectImpl();

    bool getSystemBus(RuntimeContext &ctx); // override
    bool getSessionBus(RuntimeContext &ctx);    // override
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
    bool invokeGetter(RuntimeContext &ctx,DSType *recvType,
                      const std::string &fieldName, DSType *fieldType);    // override
    bool invokeSetter(RuntimeContext &ctx, DSType *recvType,
                      const std::string &fieldName, DSType *fieldType);    // override

    DBusMessage *newMethodCallMsg(const char *ifaceName, const char *methodName);
    DBusMessage *newMethodCallMsg(const std::string &ifaceName, const std::string &methodName);

    static bool newObject(RuntimeContext &ctx, const std::shared_ptr<DSObject> &busObj,
                          std::string &&destination, std::string &&objectPath);
};

} // namespace core
} // namespace ydsh


#endif //DBUS_DBUSBIND_H
