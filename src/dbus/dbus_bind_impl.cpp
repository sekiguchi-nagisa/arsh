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

#include <cstring>

#include "dbus_bind_impl.h"
#include "../core/logger.h"
#include "../misc/resource.hpp"

namespace ydsh {

// helper util
struct DBusErrorDeleter {
    void operator()(DBusError &e) const {
        if(dbus_error_is_set(&e)) {
            dbus_error_free(&e);
        }
    }
};

using ScopedDBusError = ScopedResource<DBusError, DBusErrorDeleter>;

static DBusError *operator&(ScopedDBusError &e) {
    return &const_cast<DBusError &>(e.get());
}

static ScopedDBusError newDBusError() {
    auto e = makeScopedResource(DBusError(), DBusErrorDeleter());
    dbus_error_init(&e);
    return e;
}

static void reportDBusError(RuntimeContext &ctx, const char *dbusErrorName, std::string &&message) {
    std::string name(dbusErrorName);
    auto &type = ctx.getPool().createErrorType(name, ctx.getPool().getDBusErrorType());
    ctx.throwError(type, std::move(message));
}

static void reportDBusError(RuntimeContext &ctx, ScopedDBusError &error) {
    reportDBusError(ctx, error.get().name, std::string(error.get().message));
}

static ScopedDBusMessage wrap(DBusMessage *msg) {
    return ScopedDBusMessage(msg);
}

//FIXME: empty array
static DSValue decodeMessageIter(RuntimeContext &ctx, DBusMessageIter *iter) {
    int dbusType = dbus_message_iter_get_arg_type(iter);
    switch(dbusType) {
    case DBUS_TYPE_BYTE: {
        unsigned char value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(ctx.getPool().getByteType(), value);
    }
    case DBUS_TYPE_INT16: {
        dbus_int16_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(ctx.getPool().getInt16Type(), value);
    }
    case DBUS_TYPE_UINT16: {
        dbus_uint16_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(ctx.getPool().getUint16Type(), value);
    }
    case DBUS_TYPE_INT32: {
        dbus_int32_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), value);
    }
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value);
    }
    case DBUS_TYPE_INT64: {
        dbus_int64_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value);
    }
    case DBUS_TYPE_UINT64: {
        dbus_uint64_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value);
    }
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t value;
        dbus_message_iter_get_basic(iter, &value);
        return value == TRUE ? ctx.getTrueObj() : ctx.getFalseObj();
    }
    case DBUS_TYPE_DOUBLE: {
        double value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Float_Object>(ctx.getPool().getFloatType(), value);
    }
    case DBUS_TYPE_STRING: {
        const char *value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<String_Object>(ctx.getPool().getStringType(), std::string(value));
    }
    case DBUS_TYPE_OBJECT_PATH: {
        const char *value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<String_Object>(ctx.getPool().getObjectPathType(), std::string(value));
    }
    case DBUS_TYPE_UNIX_FD: {
        fatal("unsupported dbus type: UNIX_FD");
        return DSValue();
    }
    case DBUS_TYPE_ARRAY: {
        int elementType = dbus_message_iter_get_element_type(iter);
        DBusMessageIter subIter;
        dbus_message_iter_recurse(iter, &subIter);
        if(elementType == DBUS_TYPE_DICT_ENTRY) {   // map
            if(!dbus_message_iter_has_next(&subIter)) { //empty map
                char *desc = dbus_message_iter_get_signature(&subIter);
                std::string mapDesc("a");
                mapDesc += desc;
                dbus_free(desc);
                auto &mapType = decodeTypeDescriptor(&ctx.getPool(), mapDesc.c_str());
                return DSValue::create<Map_Object>(mapType);
            }

            int firstElementType = DBUS_TYPE_INVALID;
            std::vector<std::pair<DSValue, DSValue>> entries;
            do {
                DBusMessageIter entryIter;
                dbus_message_iter_recurse(&subIter, &entryIter);

                auto key(decodeMessageIter(ctx, &entryIter));
                dbus_message_iter_next(&entryIter);
                if(firstElementType == DBUS_TYPE_INVALID) {
                    firstElementType = dbus_message_iter_get_arg_type(&entryIter);
                }
                auto value(decodeMessageIter(ctx, &entryIter));
                entries.push_back(std::make_pair(std::move(key), std::move(value)));
            } while(dbus_message_iter_next(&subIter));
            std::vector<DSType *> types(2);
            types[0] = entries.back().first->getType();
            types[1] = firstElementType == DBUS_TYPE_VARIANT ?
                       &ctx.getPool().getVariantType() : entries.back().second->getType();

            auto map = DSValue::create<Map_Object>(
                    ctx.getPool().createReifiedType(
                            ctx.getPool().getMapTemplate(), std::move(types)));
            for(auto &e : entries) {
                typeAs<Map_Object>(map)->add(std::move(e));
            }
            return map;
        } else {    // array
            if(!dbus_message_iter_has_next(&subIter)) { //empty array
                char *desc = dbus_message_iter_get_signature(&subIter);
                std::vector<DSType *> types(1);
                types[0] = &decodeTypeDescriptor(&ctx.getPool(), desc);
                dbus_free(desc);
                return DSValue::create<Array_Object>(
                        ctx.getPool().createReifiedType(
                                ctx.getPool().getArrayTemplate(), std::move(types)));
            }

            std::vector<DSValue> values;
            do {
                values.push_back(decodeMessageIter(ctx, &subIter));
            } while(dbus_message_iter_next(&subIter));
            std::vector<DSType *> types(1);
            types[0] = elementType == DBUS_TYPE_VARIANT ? &ctx.getPool().getVariantType() : values[0]->getType();

            return DSValue::create<Array_Object>(
                    ctx.getPool().createReifiedType(
                            ctx.getPool().getArrayTemplate(), std::move(types)), std::move(values));
        }

    }
    case DBUS_TYPE_STRUCT: {
        DBusMessageIter subIter;
        dbus_message_iter_recurse(iter, &subIter);

        std::vector<DSType *> types;
        std::vector<DSValue> values;
        do {
            values.push_back(decodeMessageIter(ctx, &subIter));
            types.push_back(values.back()->getType());
        } while(dbus_message_iter_next(&subIter));
        auto &tupleType = ctx.getPool().createTupleType(std::move(types));
        DSValue tuple(new Tuple_Object(tupleType));
        unsigned int size = values.size();
        for(unsigned int i = 0; i < size; i++) {
            typeAs<Tuple_Object>(tuple)->set(i, values[i]);
        }
        return tuple;
    }
    case DBUS_TYPE_VARIANT: {
        DBusMessageIter subIter;
        dbus_message_iter_recurse(iter, &subIter);
        return decodeMessageIter(ctx, &subIter);
    }
    default:
        fatal("unsupported dbus type: %c\n", (char) dbusType);
        return DSValue();
    }
}

static std::vector<DSValue> decodeMessageRaw(RuntimeContext &ctx,
                                                  const std::vector<DSType *> &types, ScopedDBusMessage &&msg) {
    std::vector<DSValue> values;
    DBusMessageIter iter;
    dbus_message_iter_init(msg.get(), &iter);

    // decode message
    do {
        values.push_back(decodeMessageIter(ctx, &iter));
    } while(dbus_message_iter_next(&iter));

    // check type
    unsigned int size = values.size();
    if(types.size() != size) {
        std::string str = "mismatched return value number, require size is: ";
        str += std::to_string(types.size());
        str += ", but is: ";
        str += std::to_string(values.size());
        reportDBusError(ctx, DBUS_ERROR_INVALID_SIGNATURE, std::move(str));
    }

    for(unsigned int i = 0; i < size; i++) {
        if(!types[i]->isSameOrBaseTypeOf(*values[i]->getType())) {
            std::string str = "require type is: ";
            str += ctx.getPool().getTypeName(*types[i]);
            str += ", but is: ";
            str += ctx.getPool().getTypeName(*values[i]->getType());
            reportDBusError(ctx, DBUS_ERROR_INVALID_SIGNATURE, std::move(str));
        }
    }

    return values;
}

static DSValue decodeMessage(RuntimeContext &ctx,
                             const std::vector<DSType *> &types, ScopedDBusMessage &&msg) {
    std::vector<DSValue> values = decodeMessageRaw(ctx, types, std::move(msg));

    unsigned int size = values.size();
    if(size == 0) {
        fatal("broken message, must need more than one argument");
    } else if(size == 1) {
        return std::move(values[0]);
    }

    DSValue tuple(new Tuple_Object(
            ctx.getPool().createTupleType(std::vector<DSType *>(types))));
    for(unsigned int i = 0; i < size; i++) {
        typeAs<Tuple_Object>(tuple)->set(i, values[i]);
    }
    return tuple;
}

static DSValue decodeMessage(RuntimeContext &ctx, DSType &type, ScopedDBusMessage &&msg) {
    std::vector<DSType *> types(1);
    types[0] = &type;
    return decodeMessage(ctx, types, std::move(msg));
}

static void appendArg(RuntimeContext &ctx, DBusMessageIter *iter, DSType &argType, const DSValue &arg) {
    DBus_ObjectImpl *dbus = typeAs<DBus_ObjectImpl>(ctx.getDBus());
    dbus->getBuilder().appendArg(iter, argType, arg);
}

static void appendArg(RuntimeContext &ctx, DBusMessageIter *iter,
                      DSType &argType, unsigned int index) {
    appendArg(ctx, iter, argType, ctx.getLocal(index));
}

static ScopedDBusMessage sendMessage(RuntimeContext &ctx,
                                     DBusConnection *conn, ScopedDBusMessage &&sendMsg) {
    auto error = newDBusError();
    auto retMsg = wrap(
            dbus_connection_send_with_reply_and_block(
                    conn, sendMsg.get(), DBUS_TIMEOUT_USE_DEFAULT, &error));

    if(dbus_error_is_set(&error)) {
        reportDBusError(ctx, error);
    }
    return retMsg;
}


// ############################
// ##     Bus_ObjectImpl     ##
// ############################

Bus_ObjectImpl::Bus_ObjectImpl(DSType &type, bool systemBus) :
        Bus_Object(type), conn(), systemBus(systemBus) {
}

Bus_ObjectImpl::~Bus_ObjectImpl() {
    if(this->conn != nullptr) {
        dbus_connection_unref(this->conn);
    }
}

bool Bus_ObjectImpl::initConnection(RuntimeContext &ctx, bool systemBus) {
    // get connection
    auto error = newDBusError();

    this->conn = dbus_bus_get(systemBus ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION, &error);
    if(dbus_error_is_set(&error)) {
        reportDBusError(ctx, error);
        return false;
    }

    if(this->conn == nullptr) {
        fatal("must not null\n");
    }
    return true;
}

bool Bus_ObjectImpl::service(RuntimeContext &ctx, std::string &&serviceName) {
    auto msg = wrap(dbus_message_new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetNameOwner"));

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    const char *value = serviceName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &value);

    // send
    auto reply = sendMessage(ctx, this->conn, std::move(msg));

    // get result
    DBusMessageIter replyIter;
    dbus_message_iter_init(reply.get(), &replyIter);
    assert(dbus_message_iter_get_arg_type(&replyIter) == DBUS_TYPE_STRING);
    dbus_message_iter_get_basic(&replyIter, &value);

    std::string uniqueName(value);

    // init service object
    ctx.push(DSValue::create<Service_ObjectImpl>(
            ctx.getPool().getServiceType(), DSValue(this),
            std::move(serviceName), std::move(uniqueName)));
    return true;
}

bool Bus_ObjectImpl::listNames(RuntimeContext &ctx, bool activeName) {
    auto msg = wrap(dbus_message_new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", activeName ? "ListActivatableNames" : "ListNames"));
    auto reply = sendMessage(ctx, this->conn, std::move(msg));

    // decode result
    auto result(decodeMessage(ctx, ctx.getPool().getStringArrayType(), std::move(reply)));
    ctx.push(std::move(result));
    return true;
}


// ################################
// ##     Service_ObjectImpl     ##
// ################################

Service_ObjectImpl::Service_ObjectImpl(DSType &type, const DSValue &bus,
                                       std::string &&serviceName, std::string &&uniqueName) :
        Service_Object(type), bus(bus), serviceName(std::move(serviceName)), uniqueName(std::move(uniqueName)) {
}

std::string Service_ObjectImpl::toString(RuntimeContext &, VisitedSet *) {
    return this->serviceName;
}

bool Service_ObjectImpl::object(RuntimeContext &ctx, const DSValue &objectPath) {
    DSValue obj(new DBusProxy_Object(ctx.getPool().getDBusObjectType(), DSValue(this), objectPath));

    // first call Introspection and resolve interface type.
    if(!typeAs<DBusProxy_Object>(obj)->doIntrospection(ctx)) {
        return false;
    }

    ctx.push(std::move(obj));
    return true;
}

DBus_Object *newDBusObject(TypePool *pool) {
    return new DBus_ObjectImpl(pool);
}

// #############################
// ##     DBus_ObjectImpl     ##
// #############################

DBus_ObjectImpl::DBus_ObjectImpl(TypePool *typePool) :
        DBus_Object(typePool), systemBus(), sessionBus(), builder(typePool) {
}

bool DBus_ObjectImpl::getSystemBus(RuntimeContext &ctx) {
    if(!this->systemBus) {
        this->systemBus = DSValue::create<Bus_ObjectImpl>(ctx.getPool().getBusType(), true);
        if(!typeAs<Bus_ObjectImpl>(this->systemBus)->initConnection(ctx, true)) {
            return false;
        }
    }
    ctx.push(this->systemBus);
    return true;
}

bool DBus_ObjectImpl::getSessionBus(RuntimeContext &ctx) {
    if(!this->sessionBus) {
        this->sessionBus = DSValue::create<Bus_ObjectImpl>(ctx.getPool().getBusType(), false);
        if(!typeAs<Bus_ObjectImpl>(this->sessionBus)->initConnection(ctx, false)) {
            return false;
        }
    }
    ctx.push(this->sessionBus);
    return true;
}

bool DBus_ObjectImpl::waitSignal(RuntimeContext &ctx) {
    std::vector<DBusProxy_Object *> proxies;
    proxies.push_back(typeAs<DBusProxy_Object>(ctx.getLocal(1)));

    // add signal match rule
    Bus_ObjectImpl *busObj =
            typeAs<Bus_ObjectImpl>(proxies[0]->isBelongToSystemBus() ? this->systemBus : this->sessionBus);
    DBusConnection *conn = busObj->getConnection();


    std::vector<std::string> ruleList;
    proxies[0]->createSignalMatchRule(ruleList);

    auto error = newDBusError();
    for(auto &rule : ruleList) {
        LOG(TRACE_SIGNAL, "match rule: " << rule);
        dbus_bus_add_match(conn, rule.c_str(), &error);
        if(dbus_error_is_set(&error)) {
            reportDBusError(ctx, error);
            return false;
        }
    }

    // wait and dispatch
    while(true) {
        dbus_connection_read_write(conn, 1000);
        auto message = wrap(dbus_connection_pop_message(conn));
        LOG(TRACE_SIGNAL, "timeout");
        if(message == nullptr) {
            continue;
        }

        LOG(TRACE_SIGNAL, "receive message");

        if(dbus_message_get_type(message.get()) != DBUS_MESSAGE_TYPE_SIGNAL) {
            fatal("must be signal\n");
            return false;
        }

        LOG(TRACE_SIGNAL, "receive signal");

        // check service name and object path
        const char *srv = dbus_message_get_sender(message.get());
        LOG(TRACE_SIGNAL, "sender = " << srv);
        const char *path = dbus_message_get_path(message.get());
        LOG(TRACE_SIGNAL, "path = " << path);
        const char *ifaceName = dbus_message_get_interface(message.get());
        LOG(TRACE_SIGNAL, "interface name = " << ifaceName);
        const char *methodName = dbus_message_get_member(message.get());
        LOG(TRACE_SIGNAL, "method name = " << methodName);


        DBusProxy_Object *matchedProxy = nullptr;
        for(auto *p : proxies) {
            if(p->matchObject(srv, path)) {
                matchedProxy = p;
                break;
            }
        }
        if(matchedProxy == nullptr) {
            LOG(TRACE_SIGNAL, "not found matched proxy");
            continue;
        }

        FunctionType *handlerType = matchedProxy->lookupHandler(ctx, ifaceName, methodName);
        if(handlerType != nullptr) {
            // invoke signal handler.
            std::vector<DSValue> values = decodeMessageRaw(ctx, handlerType->getParamTypes(), std::move(message));

            // push to stack
            const unsigned int size = values.size();
            for(unsigned int i = 0; i < size; i++) {
                ctx.push(std::move(values[i]));
            }

            // apply handler
//            if(ctx.applyFuncObject(0, true, size) != EvalStatus::SUCCESS) {
//                return false;
//            }
            //FIXME: use vm
            fatal("unsupported\n");
        }
    }

    return true;
}

bool DBus_ObjectImpl::getServiceFromProxy(RuntimeContext &ctx, const DSValue &proxy) {
    ctx.push(typeAs<DBusProxy_Object>(proxy)->getService());
    return true;
}

bool DBus_ObjectImpl::getObjectPathFromProxy(RuntimeContext &ctx, const DSValue &proxy) {
    ctx.push(typeAs<DBusProxy_Object>(proxy)->getObjectPath());
    return true;
}

bool DBus_ObjectImpl::getIfaceListFromProxy(RuntimeContext &ctx, const DSValue &proxy) {
    ctx.push(typeAs<DBusProxy_Object>(proxy)->createIfaceList(ctx));
    return true;
}

bool DBus_ObjectImpl::introspectProxy(RuntimeContext &ctx, const DSValue &proxy) {
    DBusProxy_Object *obj = typeAs<DBusProxy_Object>(proxy);
    auto msg = wrap(dbus_message_new_method_call(
            typeAs<Service_ObjectImpl>(obj->getService())->getServiceName(),
            typeAs<String_Object>(obj->getObjectPath())->getValue(),
            "org.freedesktop.DBus.Introspectable", "Introspect"));
    auto reply = sendMessage(
            ctx, typeAs<Service_ObjectImpl>(obj->getService())->getConnection(), std::move(msg));

    // decode result
    auto result(decodeMessage(ctx, ctx.getPool().getStringType(), std::move(reply)));
    ctx.push(std::move(result));
    return true;
}


bool SignalSelectorComparator::operator()(const SignalSelector &x,
                                          const SignalSelector &y) const {
    return strcmp(x.first, y.first) == 0 && strcmp(x.second, y.second) == 0;
}

std::size_t SignalSelectorHash::operator()(const SignalSelector &key) const {
    size_t hash = 0;

    for(unsigned int i = 0; key.first[i] != '\0'; i++) {
        hash = hash * 61 + key.first[i];
    }

    for(unsigned int i = 0; key.second[i] != '\0'; i++) {
        hash = hash * 61 + key.second[i];
    }
    return hash;
}


// ##############################
// ##     DBusProxy_Object     ##
// ##############################

DBusProxy_Object::DBusProxy_Object(DSType &type, const DSValue &srcObj, const DSValue &objectPath) :
        ProxyObject(type), srv(srcObj), objectPath(objectPath), ifaceSet(), handerMap() {
    assert(this->srv);
}

std::string DBusProxy_Object::toString(RuntimeContext &, VisitedSet *) {
    std::string str("[dest=");
    str += typeAs<Service_ObjectImpl>(this->srv)->getServiceName();
    str += ", path=";
    str += typeAs<String_Object>(this->objectPath)->getValue();
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
    const std::string &typeName = ctx.getPool().getTypeName(*targetType);
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
            if(ch == '"') {
                if(!buf.empty()) {
                    finish = true;
                    i--;
                }
            } else {
                buf += ch;
            }
        }

        ifaceSet.insert(std::move(buf));
    }
}

bool DBusProxy_Object::doIntrospection(RuntimeContext &ctx) {
    auto ret = this->sendMessage(ctx, this->newMethodCallMsg("org.freedesktop.DBus.Introspectable", "Introspect"));

    int retType = dbus_message_get_type(ret.get());
    switch(retType) {
    case DBUS_MESSAGE_TYPE_ERROR: {
        fatal("dbus error: name=%s\n", dbus_message_get_error_name(ret.get()));
        break;
    };
    case DBUS_MESSAGE_TYPE_METHOD_RETURN: {
        DBusMessageIter iter;
        dbus_message_iter_init(ret.get(), &iter);

        int argType = dbus_message_iter_get_arg_type(&iter);
        if(argType == DBUS_TYPE_STRING) {
            char *value;
            dbus_message_iter_get_basic(&iter, &value);
            extractInterfaceName(this->ifaceSet, value);
        } else {
            fatal("invalied argType\n");
        }
    };
    default:
        break;
    }

    if(this->ifaceSet.empty()) {
        std::string str = ("illegal object path: ");
        str += typeAs<String_Object>(this->objectPath)->getValue();
        reportDBusError(ctx, DBUS_ERROR_UNKNOWN_OBJECT, std::move(str));
        return false;
    }
    return true;
}

bool DBusProxy_Object::invokeMethod(RuntimeContext &ctx, const std::string &methodName, MethodHandle *handle) {
    // check signal
    if(handle->isSignal()) {
        this->addHandler(ctx.getPool().getTypeName(*handle->getRecvType()),
                         methodName, ctx.getLocal(1));
        return true;
    }

    auto msg = this->newMethodCallMsg(ctx.getPool().getTypeName(*handle->getRecvType()), methodName);

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);

    unsigned int paramSize = handle->getParamTypes().size();
    for(unsigned int i = 0; i < paramSize; i++) {
        appendArg(ctx, &iter, *handle->getParamTypes()[i], i + 1);
    }

    // send message
    auto retMsg = this->sendMessage(ctx, std::move(msg));

    // decode result
    if(retMsg != nullptr) {
        DSValue result;
        if(handle->hasMultipleReturnType()) {
            result = decodeMessage(ctx, static_cast<TupleType *>(handle->getReturnType())->getElementTypes(),
                                   std::move(retMsg));
        } else {
            DSType *type = handle->getReturnType();
            if(*type == ctx.getPool().getVoidType()) {
                return true;
            }
            result = decodeMessage(ctx, *handle->getReturnType(), std::move(retMsg));
        }
        ctx.push(std::move(result));
    }
    return true;
}

bool DBusProxy_Object::invokeGetter(RuntimeContext &ctx, DSType *recvType,
                                    const std::string &fieldName, DSType *fieldType) {
    auto msg = this->newMethodCallMsg("org.freedesktop.DBus.Properties", "Get");

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);

    const char *ifaceName = ctx.getPool().getTypeName(*recvType).c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifaceName);

    const char *propertyName = fieldName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &propertyName);

    // call getter
    auto ret = this->sendMessage(ctx, std::move(msg));

    // decode result
    auto result(decodeMessage(ctx, *fieldType, std::move(ret)));
    ctx.push(std::move(result));
    return true;
}

bool DBusProxy_Object::invokeSetter(RuntimeContext &ctx, DSType *recvType,
                                    const std::string &fieldName, DSType *) {
    auto msg = this->newMethodCallMsg("org.freedesktop.DBus.Properties", "Set");

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);

    const char *ifaceName = ctx.getPool().getTypeName(*recvType).c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifaceName);

    const char *propertyName = fieldName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &propertyName);

    appendArg(ctx, &iter, ctx.getPool().getVariantType(), ctx.peek());

    // call setter
    auto ret = this->sendMessage(ctx, std::move(msg));
    return true;
}

const DSValue &DBusProxy_Object::getService() {
    return this->srv;
}

const DSValue &DBusProxy_Object::getObjectPath() {
    return this->objectPath;
}

DSValue DBusProxy_Object::createIfaceList(RuntimeContext &ctx) {
    std::vector<DSValue> list(this->ifaceSet.size());
    unsigned int i = 0;
    for(auto &value : this->ifaceSet) {
        list[i++] = DSValue::create<String_Object>(ctx.getPool().getStringType(), value);
    }
    return DSValue::create<Array_Object>(ctx.getPool().getStringArrayType(), std::move(list));
}

FunctionType *DBusProxy_Object::lookupHandler(RuntimeContext &ctx,
                                              const char *ifaceName, const char *methodName) {
    auto iter = this->handerMap.find(std::make_pair(ifaceName, methodName));
    if(iter == this->handerMap.end()) {
        return nullptr;
    }
    ctx.push(iter->second);
    return typeAs<OldFuncObject>(iter->second)->getFuncType();
}

bool DBusProxy_Object::matchObject(const char *serviceName, const char *objectPath) {
    return strcmp(serviceName, typeAs<Service_ObjectImpl>(this->srv)->getUniqueName()) == 0 &&
           strcmp(objectPath, typeAs<String_Object>(this->objectPath)->getValue()) == 0;
}

static inline void quote(std::string &str, const char *value) {
    str += "'";
    str += value;
    str += "'";
}

void DBusProxy_Object::createSignalMatchRule(std::vector<std::string> &ruleList) {
    for(auto &pair : this->handerMap) {
        std::string rule("type=");
        quote(rule, "signal");
        rule += ", sender=";
        quote(rule, typeAs<Service_ObjectImpl>(this->srv)->getServiceName());
        rule += ", path=";
        quote(rule, typeAs<String_Object>(this->objectPath)->getValue());
        rule += ", interface=";
        quote(rule, pair.first.first);
        rule += ", member=";
        quote(rule, pair.first.second);

        ruleList.push_back(std::move(rule));
    }
}

bool DBusProxy_Object::isBelongToSystemBus() {
    return typeAs<Bus_ObjectImpl>(typeAs<Service_ObjectImpl>(this->srv)->getBus())->isSystemBus();
}

ScopedDBusMessage DBusProxy_Object::newMethodCallMsg(const char *ifaceName, const char *methodName) {
    return wrap(dbus_message_new_method_call(
            typeAs<Service_ObjectImpl>(this->srv)->getServiceName(),
            typeAs<String_Object>(this->objectPath)->getValue(), ifaceName, methodName));
}

ScopedDBusMessage DBusProxy_Object::newMethodCallMsg(const std::string &ifaceName, const std::string &methodName) {
    return this->newMethodCallMsg(ifaceName.c_str(), methodName.c_str());
}

ScopedDBusMessage DBusProxy_Object::sendMessage(RuntimeContext &ctx, ScopedDBusMessage &&sendMsg) {
    return ::ydsh::sendMessage(ctx, typeAs<Service_ObjectImpl>(this->srv)->getConnection(), std::move(sendMsg));
}

void DBusProxy_Object::addHandler(const std::string &ifaceName,
                                  const std::string &methodName, const DSValue &obj) {
    this->handerMap[std::make_pair(ifaceName.c_str(), methodName.c_str())] = obj;
}

} // namespace ydsh