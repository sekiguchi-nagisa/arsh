/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#include "dbus_bind.h"
#include "handle.h"
#include "../logger.h"
#include "../misc/resource.hpp"

namespace ydsh {

// helper util
struct DBusErrorDeleter {
    void operator()(DBusError &e) const {
        if(dbus_error_is_set(&e) != 0u) {
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

static void throwDBusError(DSState &ctx, const char *dbusErrorName, std::string &&message) {
    std::string name(dbusErrorName);
    auto &type = getPool(ctx).createErrorType(name, getPool(ctx).getDBusErrorType());
    throwError(ctx, type, std::move(message));
}

static void throwDBusError(DSState &ctx, ScopedDBusError &error) {
    throwDBusError(ctx, error.get().name, std::string(error.get().message));
}

static ScopedDBusMessage wrap(DBusMessage *msg) {
    return ScopedDBusMessage(msg);
}

//FIXME: empty array
static DSValue decodeMessageIter(DSState &ctx, DBusMessageIter *iter) {
    int dbusType = dbus_message_iter_get_arg_type(iter);
    switch(dbusType) {
    case DBUS_TYPE_BYTE: {
        unsigned char value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(getPool(ctx).getByteType(), value);
    }
    case DBUS_TYPE_INT16: {
        dbus_int16_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(getPool(ctx).getInt16Type(), value);
    }
    case DBUS_TYPE_UINT16: {
        dbus_uint16_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(getPool(ctx).getUint16Type(), value);
    }
    case DBUS_TYPE_INT32: {
        dbus_int32_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(getPool(ctx).getInt32Type(), value);
    }
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Int_Object>(getPool(ctx).getUint32Type(), value);
    }
    case DBUS_TYPE_INT64: {
        dbus_int64_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Long_Object>(getPool(ctx).getInt64Type(), value);
    }
    case DBUS_TYPE_UINT64: {
        dbus_uint64_t value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Long_Object>(getPool(ctx).getUint64Type(), value);
    }
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t value;
        dbus_message_iter_get_basic(iter, &value);
        return value == TRUE ? getTrueObj(ctx) : getFalseObj(ctx);
    }
    case DBUS_TYPE_DOUBLE: {
        double value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<Float_Object>(getPool(ctx).getFloatType(), value);
    }
    case DBUS_TYPE_STRING: {
        const char *value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<String_Object>(getPool(ctx).getStringType(), std::string(value));
    }
    case DBUS_TYPE_OBJECT_PATH: {
        const char *value;
        dbus_message_iter_get_basic(iter, &value);
        return DSValue::create<String_Object>(getPool(ctx).getObjectPathType(), std::string(value));
    }
    case DBUS_TYPE_UNIX_FD: {
        fatal("unsupported dbus type: UNIX_FD");
    }
    case DBUS_TYPE_ARRAY: {
        int elementType = dbus_message_iter_get_element_type(iter);
        DBusMessageIter subIter{};
        dbus_message_iter_recurse(iter, &subIter);
        if(elementType == DBUS_TYPE_DICT_ENTRY) {   // map
            if(dbus_message_iter_has_next(&subIter) == 0u) { //empty map
                char *desc = dbus_message_iter_get_signature(&subIter);
                std::string mapDesc("a");
                mapDesc += desc;
                dbus_free(desc);
                auto &mapType = decodeTypeDescriptor(&getPool(ctx), mapDesc.c_str());
                return DSValue::create<Map_Object>(mapType);
            }

            int firstElementType = DBUS_TYPE_INVALID;
            std::vector<std::pair<DSValue, DSValue>> entries;
            do {
                DBusMessageIter entryIter{};
                dbus_message_iter_recurse(&subIter, &entryIter);

                auto key(decodeMessageIter(ctx, &entryIter));
                dbus_message_iter_next(&entryIter);
                if(firstElementType == DBUS_TYPE_INVALID) {
                    firstElementType = dbus_message_iter_get_arg_type(&entryIter);
                }
                auto value(decodeMessageIter(ctx, &entryIter));
                entries.emplace_back(std::move(key), std::move(value));
            } while(dbus_message_iter_next(&subIter) != 0u);
            std::vector<DSType *> types(2);
            types[0] = entries.back().first->getType();
            types[1] = firstElementType == DBUS_TYPE_VARIANT ?
                       &getPool(ctx).getVariantType() : entries.back().second->getType();

            auto map = DSValue::create<Map_Object>(
                    getPool(ctx).createReifiedType(getPool(ctx).getMapTemplate(), std::move(types)));
            for(auto &e : entries) {
                typeAs<Map_Object>(map)->add(std::move(e));
            }
            return map;
        }
        // array
        if(dbus_message_iter_has_next(&subIter) == 0u) { //empty array
            char *desc = dbus_message_iter_get_signature(&subIter);
            std::vector<DSType *> types(1);
            types[0] = &decodeTypeDescriptor(&getPool(ctx), desc);
            dbus_free(desc);
            return DSValue::create<Array_Object>(
                    getPool(ctx).createReifiedType(getPool(ctx).getArrayTemplate(), std::move(types)));
        }

        std::vector<DSValue> values;
        do {
            values.push_back(decodeMessageIter(ctx, &subIter));
        } while(dbus_message_iter_next(&subIter) != 0u);
        std::vector<DSType *> types(1);
        types[0] = elementType == DBUS_TYPE_VARIANT ? &getPool(ctx).getVariantType() : values[0]->getType();

        return DSValue::create<Array_Object>(
                getPool(ctx).createReifiedType(getPool(ctx).getArrayTemplate(), std::move(types)), std::move(values));
    }
    case DBUS_TYPE_STRUCT: {
        DBusMessageIter subIter{};
        dbus_message_iter_recurse(iter, &subIter);

        std::vector<DSType *> types;
        std::vector<DSValue> values;
        do {
            values.push_back(decodeMessageIter(ctx, &subIter));
            types.push_back(values.back()->getType());
        } while(dbus_message_iter_next(&subIter) != 0u);
        auto &tupleType = getPool(ctx).createTupleType(std::move(types));
        DSValue tuple(new Tuple_Object(tupleType));
        unsigned int size = values.size();
        for(unsigned int i = 0; i < size; i++) {
            typeAs<Tuple_Object>(tuple)->set(i, values[i]);
        }
        return tuple;
    }
    case DBUS_TYPE_VARIANT: {
        DBusMessageIter subIter{};
        dbus_message_iter_recurse(iter, &subIter);
        return decodeMessageIter(ctx, &subIter);
    }
    default:
        fatal("unsupported dbus type: %c\n", (char) dbusType);
        return DSValue();
    }
}

static std::vector<DSValue> decodeMessageRaw(DSState &ctx,
                                             const std::vector<DSType *> &types, ScopedDBusMessage &&msg) {
    std::vector<DSValue> values;
    DBusMessageIter iter{};
    dbus_message_iter_init(msg.get(), &iter);

    // decode message
    do {
        values.push_back(decodeMessageIter(ctx, &iter));
    } while(dbus_message_iter_next(&iter) != 0u);

    // check type
    unsigned int size = values.size();
    if(types.size() != size) {
        std::string str = "mismatched return value number, require size is: ";
        str += std::to_string(types.size());
        str += ", but is: ";
        str += std::to_string(values.size());
        throwDBusError(ctx, DBUS_ERROR_INVALID_SIGNATURE, std::move(str));
    }

    for(unsigned int i = 0; i < size; i++) {
        if(!types[i]->isSameOrBaseTypeOf(*values[i]->getType())) {
            std::string str = "require type is: ";
            str += getPool(ctx).getTypeName(*types[i]);
            str += ", but is: ";
            str += getPool(ctx).getTypeName(*values[i]->getType());
            throwDBusError(ctx, DBUS_ERROR_INVALID_SIGNATURE, std::move(str));
        }
    }

    return values;
}

static DSValue decodeMessage(DSState &ctx, const std::vector<DSType *> &types, ScopedDBusMessage &&msg) {
    std::vector<DSValue> values = decodeMessageRaw(ctx, types, std::move(msg));

    unsigned int size = values.size();
    if(size == 0) {
        fatal("broken message, must need more than one argument");
    } else if(size == 1) {
        return std::move(values[0]);
    }

    DSValue tuple(new Tuple_Object(getPool(ctx).createTupleType(std::vector<DSType *>(types))));
    for(unsigned int i = 0; i < size; i++) {
        typeAs<Tuple_Object>(tuple)->set(i, values[i]);
    }
    return tuple;
}

static DSValue decodeMessage(DSState &ctx, DSType &type, ScopedDBusMessage &&msg) {
    std::vector<DSType *> types(1);
    types[0] = &type;
    return decodeMessage(ctx, types, std::move(msg));
}

static void appendArg(DSState &ctx, DBusMessageIter *iter, DSType &argType, const DSValue &arg) {
    auto *dbus = typeAs<DBus_Object>(getGlobal(ctx, toIndex(BuiltinVarOffset::DBUS)));
    dbus->getBuilder().appendArg(iter, argType, arg);
}

static void appendArg(DSState &ctx, DBusMessageIter *iter, DSType &argType, unsigned int index) {
    appendArg(ctx, iter, argType, getLocal(ctx, index));
}

static ScopedDBusMessage sendMessage(DSState &ctx, DBusConnection *conn, ScopedDBusMessage &&sendMsg) {
    auto error = newDBusError();
    auto retMsg = wrap(
            dbus_connection_send_with_reply_and_block(
                    conn, sendMsg.get(), DBUS_TIMEOUT_USE_DEFAULT, &error));

    if(dbus_error_is_set(&error) != 0u) {
        throwDBusError(ctx, error);
    }
    return retMsg;
}


// ########################
// ##     Bus_Object     ##
// ########################

void Bus_Object::initConnection(DSState &ctx, bool systemBus) {
    // get connection
    auto error = newDBusError();

    this->conn = dbus_bus_get(systemBus ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION, &error);
    if(dbus_error_is_set(&error) != 0u) {
        throwDBusError(ctx, error);
    }

    if(this->conn == nullptr) {
        fatal("must not null\n");
    }
}

DSValue Bus_Object::service(DSState &ctx, std::string &&serviceName) {
    auto msg = wrap(dbus_message_new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetNameOwner"));

    // append arg
    DBusMessageIter iter{};
    dbus_message_iter_init_append(msg.get(), &iter);
    const char *value = serviceName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &value);

    // send
    auto reply = sendMessage(ctx, this->conn, std::move(msg));

    // get result
    DBusMessageIter replyIter{};
    dbus_message_iter_init(reply.get(), &replyIter);
    assert(dbus_message_iter_get_arg_type(&replyIter) == DBUS_TYPE_STRING);
    dbus_message_iter_get_basic(&replyIter, &value);

    std::string uniqueName(value);

    // init service object
    return DSValue::create<Service_Object>(
            getPool(ctx).getServiceType(), DSValue(this), std::move(serviceName), std::move(uniqueName));
}

DSValue Bus_Object::listNames(DSState &ctx, bool activeName) {
    auto msg = wrap(dbus_message_new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", activeName ? "ListActivatableNames" : "ListNames"));
    auto reply = sendMessage(ctx, this->conn, std::move(msg));

    // decode result
    return decodeMessage(ctx, getPool(ctx).getStringArrayType(), std::move(reply));
}


// ############################
// ##     Service_Object     ##
// ############################

std::string Service_Object::toString(DSState &, VisitedSet *) {
    return this->serviceName;
}

DSValue Service_Object::object(DSState &ctx, const DSValue &objectPath) {
    DSValue obj(new DBusProxy_Object(getPool(ctx).getDBusObjectType(), DSValue(this), objectPath));

    // first call Introspection and resolve interface type.
    typeAs<DBusProxy_Object>(obj)->doIntrospection(ctx);
    return obj;
}


// #########################
// ##     DBus_Object     ##
// #########################

DSValue DBus_Object::getSystemBus(DSState &ctx) {
    if(!this->systemBus) {
        this->systemBus = DSValue::create<Bus_Object>(getPool(ctx).getBusType(), true);
        typeAs<Bus_Object>(this->systemBus)->initConnection(ctx, true);
    }
    return this->systemBus;
}

DSValue DBus_Object::getSessionBus(DSState &ctx) {
    if(!this->sessionBus) {
        this->sessionBus = DSValue::create<Bus_Object>(getPool(ctx).getBusType(), false);
        typeAs<Bus_Object>(this->sessionBus)->initConnection(ctx, false);
    }
    return this->sessionBus;
}

void DBus_Object::initSignalMatchRule(DSState &st) {
    auto *proxy = typeAs<DBusProxy_Object>(getLocal(st, 1));

    // add signal match rule
    auto *busObj = typeAs<Bus_Object>(proxy->isBelongToSystemBus() ? this->systemBus : this->sessionBus);
    DBusConnection *conn = busObj->getConnection();

    std::vector<std::string> ruleList;
    proxy->createSignalMatchRule(ruleList);

    auto error = newDBusError();
    for(auto &rule : ruleList) {
        LOG(TRACE_SIGNAL, "match rule: " << rule);
        dbus_bus_add_match(conn, rule.c_str(), &error);
        if(dbus_error_is_set(&error) != 0u) {
            throwDBusError(st, error);
        }
    }
}

std::vector<DSValue> DBus_Object::waitSignal(DSState &ctx) {    //TODO: timeout
    auto *proxy = typeAs<DBusProxy_Object>(getLocal(ctx, 1));

    auto *busObj = typeAs<Bus_Object>(proxy->isBelongToSystemBus() ? this->systemBus : this->sessionBus);
    DBusConnection *conn = busObj->getConnection();

    std::vector<DBusProxy_Object *> proxies = {proxy};

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

        auto handler = matchedProxy->lookupHandler(ifaceName, methodName);
        if(handler) {
            auto *handlerType = static_cast<FunctionType *>(typeAs<FuncObject>(handler)->getType());
            std::vector<DSValue> values = decodeMessageRaw(ctx, handlerType->getParamTypes(), std::move(message));
            values.insert(values.begin(), std::move(handler));
            return values;
        }
    }
}

DSValue DBus_Object::getServiceFromProxy(DSState &, const DSValue &proxy) {
    return typeAs<DBusProxy_Object>(proxy)->getService();
}

DSValue DBus_Object::getObjectPathFromProxy(DSState &, const DSValue &proxy) {
    return typeAs<DBusProxy_Object>(proxy)->getObjectPath();
}

DSValue DBus_Object::getIfaceListFromProxy(DSState &ctx, const DSValue &proxy) {
    return typeAs<DBusProxy_Object>(proxy)->createIfaceList(ctx);
}

DSValue DBus_Object::introspectProxy(DSState &ctx, const DSValue &proxy) {
    auto *obj = typeAs<DBusProxy_Object>(proxy);
    auto msg = wrap(dbus_message_new_method_call(
            typeAs<Service_Object>(obj->getService())->getServiceName(),
            typeAs<String_Object>(obj->getObjectPath())->getValue(),
            "org.freedesktop.DBus.Introspectable", "Introspect"));
    auto reply = sendMessage(
            ctx, typeAs<Service_Object>(obj->getService())->getConnection(), std::move(msg));

    // decode result
    return decodeMessage(ctx, getPool(ctx).getStringType(), std::move(reply));
}

// ##############################
// ##     DBusProxy_Object     ##
// ##############################

std::string DBusProxy_Object::toString(DSState &, VisitedSet *) {
    std::string str("[dest=");
    str += typeAs<Service_Object>(this->srv)->getServiceName();
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

bool DBusProxy_Object::introspect(DSState &ctx, DSType *targetType) {
    const char *typeName = getPool(ctx).getTypeName(*targetType);
    auto iter = this->ifaceSet.find(typeName);
    return iter != this->ifaceSet.end();
}

static void extractInterfaceName(std::unordered_set<std::string> &ifaceSet, const char *str) {
    const char *prefix = "<interface name=";

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

void DBusProxy_Object::doIntrospection(DSState &ctx) {
    auto ret = this->sendMessage(ctx, this->newMethodCallMsg("org.freedesktop.DBus.Introspectable", "Introspect"));

    int retType = dbus_message_get_type(ret.get());
    switch(retType) {
    case DBUS_MESSAGE_TYPE_ERROR: {
        fatal("dbus error: name=%s\n", dbus_message_get_error_name(ret.get()));
        break;
    };
    case DBUS_MESSAGE_TYPE_METHOD_RETURN: {
        DBusMessageIter iter{};
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
        throwDBusError(ctx, DBUS_ERROR_UNKNOWN_OBJECT, std::move(str));
    }
}

DSValue DBusProxy_Object::invokeMethod(DSState &ctx, const char *methodName, const MethodHandle *handle) {
    // check signal
    if(handle->isSignal()) {
        this->addHandler(getPool(ctx).getTypeName(*handle->getRecvType()), methodName, getLocal(ctx, 1));
        return DSValue();
    }

    auto msg = this->newMethodCallMsg(getPool(ctx).getTypeName(*handle->getRecvType()), methodName);

    // append arg
    DBusMessageIter iter{};
    dbus_message_iter_init_append(msg.get(), &iter);

    unsigned int paramSize = handle->getParamTypes().size();
    for(unsigned int i = 0; i < paramSize; i++) {
        appendArg(ctx, &iter, *handle->getParamTypes()[i], i + 1);
    }

    // send message
    auto retMsg = this->sendMessage(ctx, std::move(msg));

    // decode result
    DSValue result;
    if(retMsg != nullptr) {
        if(handle->hasMultipleReturnType()) {
            auto type = static_cast<TupleType *>(handle->getReturnType());
            result = decodeMessage(ctx, type->getElementTypes(), std::move(retMsg));
        } else if(!handle->getReturnType()->isVoidType()) {
            result = decodeMessage(ctx, *handle->getReturnType(), std::move(retMsg));
        }
    }
    return result;
}

DSValue DBusProxy_Object::invokeGetter(DSState &ctx, const DSType *recvType,
                                       const char *fieldName, const DSType *fieldType) {
    auto msg = this->newMethodCallMsg("org.freedesktop.DBus.Properties", "Get");

    // append arg
    DBusMessageIter iter{};
    dbus_message_iter_init_append(msg.get(), &iter);

    const char *ifaceName = getPool(ctx).getTypeName(*recvType);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifaceName);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &fieldName);

    // call getter
    auto ret = this->sendMessage(ctx, std::move(msg));

    // decode result
    return decodeMessage(ctx, *const_cast<DSType *>(fieldType), std::move(ret));    //FIXME: remove const_cast
}

void DBusProxy_Object::invokeSetter(DSState &ctx, const DSType *recvType,
                                    const char *fieldName, const DSType *) {
    auto msg = this->newMethodCallMsg("org.freedesktop.DBus.Properties", "Set");

    // append arg
    DBusMessageIter iter{};
    dbus_message_iter_init_append(msg.get(), &iter);

    const char *ifaceName = getPool(ctx).getTypeName(*recvType);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifaceName);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &fieldName);

    appendArg(ctx, &iter, getPool(ctx).getVariantType(), getLocal(ctx, 1));

    // call setter
    this->sendMessage(ctx, std::move(msg));
}

DSValue DBusProxy_Object::createIfaceList(DSState &ctx) {
    std::vector<DSValue> list(this->ifaceSet.size());
    unsigned int i = 0;
    for(auto &value : this->ifaceSet) {
        list[i++] = DSValue::create<String_Object>(getPool(ctx).getStringType(), value);
    }
    return DSValue::create<Array_Object>(getPool(ctx).getStringArrayType(), std::move(list));
}

bool DBusProxy_Object::matchObject(const char *serviceName, const char *objectPath) {
    return strcmp(serviceName, typeAs<Service_Object>(this->srv)->getUniqueName()) == 0 &&
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
        quote(rule, typeAs<Service_Object>(this->srv)->getServiceName());
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
    return typeAs<Bus_Object>(typeAs<Service_Object>(this->srv)->getBus())->isSystemBus();
}

ScopedDBusMessage DBusProxy_Object::newMethodCallMsg(const char *ifaceName, const char *methodName) {
    return wrap(dbus_message_new_method_call(
            typeAs<Service_Object>(this->srv)->getServiceName(),
            typeAs<String_Object>(this->objectPath)->getValue(), ifaceName, methodName));
}

ScopedDBusMessage DBusProxy_Object::sendMessage(DSState &ctx, ScopedDBusMessage &&sendMsg) {
    return ::ydsh::sendMessage(ctx, typeAs<Service_Object>(this->srv)->getConnection(), std::move(sendMsg));
}


/**
 *
 * implementation of DBus related builtin method
 */

#define LOCAL(i) getLocal(ctx, i)

DSValue newDBusObject(TypePool &pool) {
    return DSValue::create<DBus_Object>(pool);
}

DSValue dbus_systemBus(DSState &ctx) {
    return typeAs<DBus_Object>(LOCAL(0))->getSystemBus(ctx);
}

DSValue dbus_sessionBus(DSState &ctx) {
    return typeAs<DBus_Object>(LOCAL(0))->getSessionBus(ctx);
}

DSValue dbus_getSrv(DSState &ctx) {
    return typeAs<DBus_Object>(LOCAL(0))->getServiceFromProxy(ctx, LOCAL(1));
}

DSValue dbus_getPath(DSState &ctx) {
    return typeAs<DBus_Object>(LOCAL(0))->getObjectPathFromProxy(ctx, LOCAL(1));
}

DSValue dbus_getIface(DSState &ctx) {
    return typeAs<DBus_Object>(LOCAL(0))->getIfaceListFromProxy(ctx, LOCAL(1));
}

DSValue dbus_introspect(DSState &ctx) {
    return typeAs<DBus_Object>(LOCAL(0))->introspectProxy(ctx, LOCAL(1));
}

DSValue bus_service(DSState &ctx) {
    auto *strObj = typeAs<String_Object>(LOCAL(1));
    return typeAs<Bus_Object>(LOCAL(0))->service(ctx, std::string(strObj->getValue()));
}

DSValue bus_listNames(DSState &ctx) {
    return typeAs<Bus_Object>(LOCAL(0))->listNames(ctx, false);
}

DSValue bus_listActiveNames(DSState &ctx) {
    return typeAs<Bus_Object>(LOCAL(0))->listNames(ctx, true);
}

DSValue service_object(DSState &ctx) {
    return typeAs<Service_Object>(LOCAL(0))->object(ctx, LOCAL(1));
}

// implementation of DBus specific instruction

/**
 * implemantion of DBUS_INIT_SIG instruction
 * @param st
 */
void DBusInitSignal(DSState &ctx) {
    typeAs<DBus_Object>(LOCAL(0))->initSignalMatchRule(ctx);
}

/**
 * implemation of DBUS_WAIT_SIG instruction.
 * after call it, push signal handler function and parameters on operand stack.
 * @param st
 * @return
 * contains lookuped handler and parameters.
 */
std::vector<DSValue> DBusWaitSignal(DSState &ctx) {
    return typeAs<DBus_Object>(LOCAL(0))->waitSignal(ctx);
}


} // namespace ydsh