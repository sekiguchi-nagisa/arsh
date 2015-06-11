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

#include "DBusBindImpl.h"
#include "../core/FieldHandle.h"
#include "../misc/debug.h"

#include <assert.h>
#include <string.h>

namespace ydsh {
namespace core {

// helper util
static void reportDBusError(RuntimeContext &ctx, DBusError &error) {
    std::string name(error.name);
    DSType *type = ctx.getPool().createAndGetErrorTypeIfUndefined(name, ctx.getPool().getDBusErrorType());
    ctx.throwError(type, error.message);
}

static void reportDBusError(RuntimeContext &ctx, const char *dbusErrorName, std::string &&message) {
    std::string name(dbusErrorName);
    DSType *type = ctx.getPool().createAndGetErrorTypeIfUndefined(name, ctx.getPool().getDBusErrorType());
    ctx.throwError(type, std::move(message));
}

static void unrefMessage(DBusMessage *msg) {
    if(msg != nullptr) {
        dbus_message_unref(msg);
    }
}

//FIXME: empty array
static std::shared_ptr<DSObject> decodeMessageIter(RuntimeContext &ctx, DBusMessageIter *iter) {
    int dbusType = dbus_message_iter_get_arg_type(iter);
    switch(dbusType) {
    case DBUS_TYPE_BYTE: {
        unsigned char value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.getPool().getByteType(), value);
    };
    case DBUS_TYPE_INT16: {
        dbus_int16_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.getPool().getInt16Type(), value);
    };
    case DBUS_TYPE_UINT16: {
        dbus_uint16_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.getPool().getUint16Type(), value);
    };
    case DBUS_TYPE_INT32: {
        dbus_int32_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.getPool().getInt32Type(), value);
    };
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.getPool().getUint32Type(), value);
    };
    case DBUS_TYPE_INT64: {
        dbus_int64_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Long_Object>(ctx.getPool().getInt64Type(), value);
    };
    case DBUS_TYPE_UINT64: {
        dbus_uint64_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Long_Object>(ctx.getPool().getUint64Type(), value);
    };
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t value;
        dbus_message_iter_get_basic(iter, &value);
        return value == TRUE ? ctx.getTrueObj() : ctx.getFalseObj();
    };
    case DBUS_TYPE_DOUBLE: {
        double value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Float_Object>(ctx.getPool().getFloatType(), value);
    };
    case DBUS_TYPE_STRING: {
        const char *value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<String_Object>(ctx.getPool().getStringType(), std::string(value));
    };
    case DBUS_TYPE_OBJECT_PATH: {
        const char *value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<String_Object>(ctx.getPool().getObjectPathType(), std::string(value));
    };
    case DBUS_TYPE_UNIX_FD: {
        fatal("unsupported dbus type: UNIX_FD");
        return std::shared_ptr<DSObject>(nullptr);
    };
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
                DSType *mapType = decodeTypeDescriptor(&ctx.getPool(), mapDesc.c_str());
                return std::make_shared<Map_Object>(mapType);
            }

            int firstElementType = DBUS_TYPE_INVALID;
            std::vector<std::pair<std::shared_ptr<DSObject>, std::shared_ptr<DSObject>>> entries;
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
                       ctx.getPool().getVariantType() : entries.back().second->getType();

            auto map = std::make_shared<Map_Object>(
                    ctx.getPool().createAndGetReifiedTypeIfUndefined(
                            ctx.getPool().getMapTemplate(), std::move(types)));
            unsigned int size = entries.size();
            for(unsigned int i = 0; i < size; i++) {
                map->add(std::move(entries[i]));
            }
            return std::move(map);
        } else {    // array
            if(!dbus_message_iter_has_next(&subIter)) { //empty array
                char *desc = dbus_message_iter_get_signature(&subIter);
                std::vector<DSType*> types(1);
                types[0] = decodeTypeDescriptor(&ctx.getPool(), desc);
                dbus_free(desc);
                return std::make_shared<Array_Object>(
                        ctx.getPool().createAndGetReifiedTypeIfUndefined(
                                ctx.getPool().getArrayTemplate(), std::move(types)));
            }

            std::vector<std::shared_ptr<DSObject>> values;
            do {
                values.push_back(decodeMessageIter(ctx, &subIter));
            } while(dbus_message_iter_next(&subIter));
            std::vector<DSType *> types(1);
            types[0] = elementType == DBUS_TYPE_VARIANT ? ctx.getPool().getVariantType() : values[0]->getType();

            return std::make_shared<Array_Object>(
                    ctx.getPool().createAndGetReifiedTypeIfUndefined(
                            ctx.getPool().getArrayTemplate(), std::move(types)), std::move(values));
        }

    };
    case DBUS_TYPE_STRUCT: {
        DBusMessageIter subIter;
        dbus_message_iter_recurse(iter, &subIter);

        std::vector<DSType *> types;
        std::vector<std::shared_ptr<DSObject>> values;
        do {
            values.push_back(decodeMessageIter(ctx, &subIter));
            types.push_back(values.back()->getType());
        } while(dbus_message_iter_next(&subIter));
        DSType *tupleType = ctx.getPool().createAndGetTupleTypeIfUndefined(std::move(types));
        std::shared_ptr<Tuple_Object> tuple(new Tuple_Object(tupleType));
        unsigned int size = values.size();
        for(unsigned int i = 0; i < size; i++) {
            tuple->set(i, values[i]);
        }
        return std::move(tuple);
    };
    case DBUS_TYPE_VARIANT: {
        DBusMessageIter subIter;
        dbus_message_iter_recurse(iter, &subIter);
        return decodeMessageIter(ctx, &subIter);
    };
    default:
        fatal("unsupported dbus type: %c\n", (char)dbusType);
        return std::shared_ptr<DSObject>(nullptr);
    }
}

/**
 * decode read message.
 * after decoding, unref message.
 * return false, if illegal message.(ex. mismatch type)
 */
static bool decodeAndUnrefMessage(std::vector<std::shared_ptr<DSObject>> &values, RuntimeContext &ctx,
                                                       const std::vector<DSType *> &types, DBusMessage *msg) {
    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);

    // decode message
    do {
        values.push_back(decodeMessageIter(ctx, &iter));
    } while(dbus_message_iter_next(&iter));
    unrefMessage(msg);

    // check type
    unsigned int size = values.size();
    if(types.size() != size) {
        std::string msg = "mismatched return value number, require size is: ";
        msg += std::to_string(types.size());
        msg += ", but is: ";
        msg += std::to_string(values.size());
        reportDBusError(ctx, DBUS_ERROR_INVALID_SIGNATURE, std::move(msg));
        return false;
    }

    for(unsigned int i = 0; i < size; i++) {
        if(!types[i]->isAssignableFrom(values[i]->getType())) {
            std::string msg = "require type is: ";
            msg += ctx.getPool().getTypeName(*types[i]);
            msg += ", but is: ";
            msg += ctx.getPool().getTypeName(*values[i]->getType());
            reportDBusError(ctx, DBUS_ERROR_INVALID_SIGNATURE, std::move(msg));
            return false;
        }
    }
    return true;
}

static std::shared_ptr<DSObject> decodeAndUnrefMessage(RuntimeContext &ctx,
                                                       const std::vector<DSType *> &types, DBusMessage *msg) {
    std::vector<std::shared_ptr<DSObject>> values;
    if(!decodeAndUnrefMessage(values, ctx, types, msg)) {
        return std::shared_ptr<DSObject>();
    }

    unsigned int size = values.size();
    if(size == 0) {
        fatal("broken message, must need more than one argument");
    } else if(size == 1) {
        return std::move(values[0]);
    }

    std::shared_ptr<Tuple_Object> tuple(
            new Tuple_Object(ctx.getPool().createAndGetTupleTypeIfUndefined(std::vector<DSType *>(types))));
    for(unsigned int i = 0; i < size; i++) {
        tuple->set(i, values[i]);
    }
    return std::move(tuple);
}

static std::shared_ptr<DSObject> decodeAndUnrefMessage(RuntimeContext &ctx, DSType *type, DBusMessage *msg) {
    std::vector<DSType *> types(1);
    types[0] = type;
    return decodeAndUnrefMessage(ctx, types, msg);
}

static void appendArg(RuntimeContext &ctx, DBusMessageIter *iter,
                      DSType *argType, const std::shared_ptr<DSObject> &arg) {
    DBus_ObjectImpl *dbus = TYPE_AS(DBus_ObjectImpl, ctx.getDBus());
    dbus->getBuilder().appendArg(iter, argType, arg);
}

static void appendArg(RuntimeContext &ctx, DBusMessageIter *iter,
                      DSType *argType, unsigned int index) {
    appendArg(ctx, iter, argType, ctx.getLocal(index));
}


static DBusMessage *sendAndUnrefMessage(RuntimeContext &ctx,
                                        DBusConnection *conn, DBusMessage *sendMsg, bool &status) {
    DBusError error;
    dbus_error_init(&error);

    status = false;
    DBusMessage *retMsg = dbus_connection_send_with_reply_and_block(
            conn, sendMsg, DBUS_TIMEOUT_USE_DEFAULT, &error);
    unrefMessage(sendMsg);

    if(dbus_error_is_set(&error)) {
        reportDBusError(ctx, error);

        dbus_error_free(&error);
        unrefMessage(retMsg);

        return nullptr;
    }
    status = true;
    return retMsg;
}

// ############################
// ##     Bus_ObjectImpl     ##
// ############################

Bus_ObjectImpl::Bus_ObjectImpl(DSType *type, bool systemBus) :
        Bus_Object(type), conn(), systemBus(systemBus) {
}

Bus_ObjectImpl::~Bus_ObjectImpl() {
    if(this->conn != nullptr) {
        dbus_connection_unref(this->conn);
    }
}

bool Bus_ObjectImpl::initConnection(RuntimeContext &ctx, bool systemBus) {
    // get connection
    DBusError error;
    dbus_error_init(&error);

    this->conn = dbus_bus_get(systemBus ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION, &error);
    if(dbus_error_is_set(&error)) {
        reportDBusError(ctx, error);
        dbus_error_free(&error);
        return false;
    }

    if(this->conn == nullptr) {
        fatal("must not null\n");
    }
    return true;
}

bool Bus_ObjectImpl::service(RuntimeContext &ctx, std::string &&serviceName) {
    auto msg = dbus_message_new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetNameOwner");

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    const char *value = serviceName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &value);

    // send
    bool status;
    auto reply = sendAndUnrefMessage(ctx, this->conn, msg, status);
    if(!status) {
        return false;
    }

    // get result
    DBusMessageIter replyIter;
    dbus_message_iter_init(reply, &replyIter);
    assert(dbus_message_iter_get_arg_type(&replyIter) == DBUS_TYPE_STRING);
    dbus_message_iter_get_basic(&replyIter, &value);

    std::string uniqueName(value);

    unrefMessage(reply);

    // init service object
    ctx.push(std::make_shared<Service_ObjectImpl>(
            ctx.getPool().getServiceType(), this->shared_from_this(),
            std::move(serviceName), std::move(uniqueName)));
    return true;
}

bool Bus_ObjectImpl::listNames(RuntimeContext &ctx, bool activeName) {
    auto msg = dbus_message_new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", activeName ? "ListActivatableNames" : "ListNames");
    bool status;
    auto reply = sendAndUnrefMessage(ctx, this->conn, msg, status);
    if(!status) {
        return false;
    }

    // decode result
    auto result(decodeAndUnrefMessage(ctx, ctx.getPool().getStringArrayType(), reply));
    if(!result) {
        return false;
    }
    ctx.push(std::move(result));
    return true;
}


// ################################
// ##     Service_ObjectImpl     ##
// ################################

Service_ObjectImpl::Service_ObjectImpl(DSType *type, const std::shared_ptr<Bus_ObjectImpl> &bus,
                                       std::string &&serviceName, std::string &&uniqueName) :
        Service_Object(type), bus(bus), serviceName(std::move(serviceName)), uniqueName(std::move(uniqueName)) {
}

std::string Service_ObjectImpl::toString(RuntimeContext &ctx) {
    return this->serviceName;
}

bool Service_ObjectImpl::object(RuntimeContext &ctx, const std::shared_ptr<String_Object> &objectPath) {
    std::shared_ptr<DBusProxy_Object> obj(
            new DBusProxy_Object(ctx.getPool().getDBusObjectType(), this->shared_from_this(), objectPath));

    // first call Introspection and resolve interface type.
    if(!obj->doIntrospection(ctx)) {
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
        this->systemBus = std::make_shared<Bus_ObjectImpl>(ctx.getPool().getBusType(), true);
        if(!this->systemBus->initConnection(ctx, true)) {
            return false;
        }
    }
    ctx.push(this->systemBus);
    return true;
}

bool DBus_ObjectImpl::getSessionBus(RuntimeContext &ctx) {
    if(!this->sessionBus) {
        this->sessionBus = std::make_shared<Bus_ObjectImpl>(ctx.getPool().getBusType(), false);
        if(!this->sessionBus->initConnection(ctx, false)) {
            return false;
        }
    }
    ctx.push(this->sessionBus);
    return true;
}

bool DBus_ObjectImpl::waitSignal(RuntimeContext &ctx) {
    std::vector<DBusProxy_Object *> proxies;
    proxies.push_back(TYPE_AS(DBusProxy_Object, ctx.getLocal(1)));

    // add signal match rule
    DBusConnection *conn =
            (proxies[0]->isBelongToSystemBus() ? this->systemBus : this->sessionBus)->getConnection();
    std::vector<std::string> ruleList;
    proxies[0]->createSignalMatchRule(ruleList);

    DBusError error;
    dbus_error_init(&error);
    for(auto &rule : ruleList) {
        debugp("match rule: %s\n", rule.c_str());
        dbus_bus_add_match(conn, rule.c_str(), &error);
        if(dbus_error_is_set(&error)) {
            reportDBusError(ctx, error);
            dbus_error_free(&error);
            return false;
        }
    }

    // wait and dispatch
    while(true) {
        dbus_connection_read_write(conn, 1000);
        DBusMessage *message = dbus_connection_pop_message(conn);

        debugp("timeout\n");
        if(message == nullptr) {
            continue;
        }

        debugp("receive message\n");

        if(dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL) {
            fatal("must be signal\n");
            return false;
        }

        debugp("receive signal\n");

        // check service name and object path
        const char *srv = dbus_message_get_sender(message);
        debugp("sender = %s\n", srv);
        const char *path = dbus_message_get_path(message);
        debugp("path = %s\n", path);
        const char *ifaceName = dbus_message_get_interface(message);
        debugp("interface name = %s\n", ifaceName);
        const char *methodName = dbus_message_get_member(message);
        debugp("method name = %s\n", methodName);

        DBusProxy_Object *matchedProxy = nullptr;
        for(auto *p : proxies) {
            if(p->matchObject(srv, path)) {
                matchedProxy = p;
                break;
            }
        }
        if(matchedProxy == nullptr) {
            debugp("not found matched proxy\n");
            unrefMessage(message);
            continue;
        }

        FunctionType *handlerType = matchedProxy->lookupHandler(ctx, ifaceName, methodName);
        if(handlerType != nullptr) {
            // invoke signal handler.
            std::vector<std::shared_ptr<DSObject>> values;
            decodeAndUnrefMessage(values, ctx, handlerType->getParamTypes(), message);

            // push to stack
            unsigned int size = values.size();
            for(unsigned int i = 0; i < size; i++) {
                ctx.push(std::move(values[i]));
            }

            // apply handler
            if(ctx.applyFuncObject(0, true, size) != EvalStatus::SUCCESS) {
                return false;
            }
        }
    }

    return true;
}

bool DBus_ObjectImpl::getServiceFromProxy(RuntimeContext &ctx, const std::shared_ptr<DSObject> &proxy) {
    ctx.push(TYPE_AS(DBusProxy_Object, proxy)->getService());
    return true;
}

bool DBus_ObjectImpl::getObjectPathFromProxy(RuntimeContext &ctx, const std::shared_ptr<DSObject> &proxy) {
    ctx.push(TYPE_AS(DBusProxy_Object, proxy)->getObjectPath());
    return true;
}

bool DBus_ObjectImpl::getIfaceListFromProxy(RuntimeContext &ctx, const std::shared_ptr<DSObject> &proxy) {
    ctx.push(TYPE_AS(DBusProxy_Object, proxy)->createIfaceList(ctx));
    return true;
}

bool DBus_ObjectImpl::introspectProxy(RuntimeContext &ctx, const std::shared_ptr<DSObject> &proxy) {
    DBusProxy_Object *obj = TYPE_AS(DBusProxy_Object, proxy);
    auto msg = dbus_message_new_method_call(
            obj->getService()->getServiceName(), obj->getObjectPath()->getValue().c_str(),
            "org.freedesktop.DBus.Introspectable", "Introspect");
    bool status;
    auto reply = sendAndUnrefMessage(ctx, obj->getService()->getConnection(), msg, status);
    if(!status) {
        return false;
    }

    // decode result
    auto result(decodeAndUnrefMessage(ctx, ctx.getPool().getStringType(), reply));
    if(!result) {
        return false;
    }
    ctx.push(std::move(result));
    return true;
}


bool SignalSelectorComparator::operator() (const SignalSelector &x,
                                           const SignalSelector &y) const {
    return strcmp(x.first, y.first) == 0 && strcmp(x.second, y.second) == 0;
}

std::size_t SignalSelectorHash::operator() (const SignalSelector &key) const {
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

DBusProxy_Object::DBusProxy_Object(DSType *type, const std::shared_ptr<DSObject> &srcObj,
                                   const std::shared_ptr<String_Object> &objectPath) :
        ProxyObject(type), srv(std::dynamic_pointer_cast<Service_ObjectImpl>(srcObj)),
        objectPath(objectPath), ifaceSet(), handerMap() {
    assert(this->srv);
}

std::string DBusProxy_Object::toString(RuntimeContext &ctx) {
    std::string str("[dest=");
    str += this->srv->getServiceName();
    str += ", path=";
    str += this->objectPath->getValue();
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
    DBusMessage *msg = this->newMethodCallMsg("org.freedesktop.DBus.Introspectable", "Introspect");
    bool status;
    DBusMessage *ret = this->sendMessage(ctx, msg, status);
    if(!status) {
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
    default:
        break;
    }
    unrefMessage(ret);

    if(this->ifaceSet.empty()) {
        std::string msg = ("illegal object path: ");
        msg += this->objectPath->getValue();
        reportDBusError(ctx, DBUS_ERROR_UNKNOWN_OBJECT, std::move(msg));
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

    DBusMessage *msg = this->newMethodCallMsg(ctx.getPool().getTypeName(*handle->getRecvType()), methodName);

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);

    unsigned int paramSize = handle->getParamTypes().size();
    for(unsigned int i = 0; i < paramSize; i++) {
        appendArg(ctx, &iter, handle->getParamTypes()[i], i + 1);
    }

    // send message
    bool status;
    DBusMessage *retMsg = this->sendMessage(ctx, msg, status);
    if(!status) {
        return false;
    }

    // decode result
    if(retMsg != nullptr) {
        std::shared_ptr<DSObject> result(nullptr);
        if(handle->hasMultipleReturnType()) {
            result = decodeAndUnrefMessage(ctx, static_cast<TupleType *>(handle->getReturnType())->getTypes(), retMsg);
        } else {
            DSType *type = handle->getReturnType();
            if(*type == *ctx.getPool().getVoidType()) {
                unrefMessage(retMsg);
                return true;
            }
            result = decodeAndUnrefMessage(ctx, handle->getReturnType(), retMsg);
        }
        if(!result) {
            return false;
        }
        ctx.push(std::move(result));
    }
    return true;
}

bool DBusProxy_Object::invokeGetter(RuntimeContext &ctx,DSType *recvType,
                                    const std::string &fieldName, DSType *fieldType) {
    DBusMessage *msg = this->newMethodCallMsg("org.freedesktop.DBus.Properties", "Get");

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);

    const char *ifaceName = ctx.getPool().getTypeName(*recvType).c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifaceName);

    const char *propertyName = fieldName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &propertyName);

    // call getter
    bool status;
    DBusMessage *ret = this->sendMessage(ctx, msg, status);
    if(!status) {
        return false;
    }

    // decode result
    auto result(decodeAndUnrefMessage(ctx, fieldType, ret));
    if(!result) {
        return false;
    }
    ctx.push(std::move(result));
    return true;
}

bool DBusProxy_Object::invokeSetter(RuntimeContext &ctx,DSType *recvType,
                                    const std::string &fieldName, DSType *fieldType) {
    DBusMessage *msg = this->newMethodCallMsg("org.freedesktop.DBus.Properties", "Set");

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);

    const char *ifaceName = ctx.getPool().getTypeName(*recvType).c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifaceName);

    const char *propertyName = fieldName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &propertyName);

    appendArg(ctx, &iter, ctx.getPool().getVariantType(), ctx.peek());

    // call setter
    bool status;
    DBusMessage *ret = this->sendMessage(ctx, msg, status);
    if(status) {
        unrefMessage(ret);
    }
    return status;
}

const std::shared_ptr<Service_ObjectImpl> &DBusProxy_Object::getService() {
    return this->srv;
}

const std::shared_ptr<String_Object> &DBusProxy_Object::getObjectPath() {
    return this->objectPath;
}

std::shared_ptr<Array_Object> DBusProxy_Object::createIfaceList(RuntimeContext &ctx) {
    std::vector<std::shared_ptr<DSObject>> list(this->ifaceSet.size());
    unsigned int i = 0;
    for(auto &value : this->ifaceSet) {
        list[i++] = std::make_shared<String_Object>(ctx.getPool().getStringType(), value);
    }
    return std::make_shared<Array_Object>(ctx.getPool().getStringArrayType(), std::move(list));
}

FunctionType  *DBusProxy_Object::lookupHandler(RuntimeContext &ctx,
                                    const char *ifaceName, const char *methodName) {
    auto iter = this->handerMap.find(std::make_pair(ifaceName, methodName));
    if(iter == this->handerMap.end()) {
        return nullptr;
    }
    ctx.push(iter->second);
    return iter->second->getFuncType();
}

bool DBusProxy_Object::matchObject(const char *serviceName, const char *objectPath) {
    return strcmp(serviceName, this->srv->getUniqueName()) == 0 &&
           strcmp(objectPath, this->objectPath->getValue().c_str()) == 0;
}

static inline void quote(std::string &str, const std::string &value) {
    str += "'";
    str += value;
    str += "'";
}

static inline void quote(std::string &str, const char *value) {
    str += "'";
    str += value;
    str += "'";
}

void DBusProxy_Object::createSignalMatchRule(std::vector<std::string> &ruleList) {
    for(auto &pair : this->handerMap) {
        std::string rule("type="); quote(rule, "signal");
        rule += ", sender="; quote(rule, this->srv->getServiceName());
        rule += ", path="; quote(rule, this->objectPath->getValue());
        rule += ", interface="; quote(rule, pair.first.first);
        rule += ", member="; quote(rule, pair.first.second);

        ruleList.push_back(std::move(rule));
    }
}

bool DBusProxy_Object::isBelongToSystemBus() {
    return this->srv->getBus()->isSystemBus();
}

DBusMessage *DBusProxy_Object::newMethodCallMsg(const char *ifaceName, const char *methodName) {
    return dbus_message_new_method_call(
            this->srv->getServiceName(), this->objectPath->getValue().c_str(), ifaceName, methodName);
}

DBusMessage *DBusProxy_Object::newMethodCallMsg(const std::string &ifaceName, const std::string &methodName) {
    return this->newMethodCallMsg(ifaceName.c_str(), methodName.c_str());
}

DBusMessage *DBusProxy_Object::sendMessage(RuntimeContext &ctx, DBusMessage *sendMsg, bool &status) {
    return sendAndUnrefMessage(ctx, this->srv->getConnection(), sendMsg, status);
}

void DBusProxy_Object::addHandler(const std::string &ifaceName,
                                  const std::string &methodName, const std::shared_ptr<DSObject> &obj) {
    this->handerMap[std::make_pair(ifaceName.c_str(), methodName.c_str())] =
            std::dynamic_pointer_cast<FuncObject>(obj);
}

} // namespace core
} // namespace ydsh