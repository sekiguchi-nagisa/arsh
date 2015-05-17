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

#include <string.h>

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

static void unrefMessage(DBusMessage *msg) {
    if(msg != nullptr) {
        dbus_message_unref(msg);
    }
}

// ###################################
// ##     BaseTypeDescriptorMap     ##
// ###################################

BaseTypeDescriptorMap::BaseTypeDescriptorMap(TypePool *pool) :
    map() {
#define ADD(type, desc) this->map.insert(std::make_pair((unsigned long) (type), desc))
    ADD(pool->getInt64Type(), DBUS_TYPE_INT64);
    ADD(pool->getUint64Type(), DBUS_TYPE_UINT64);
    ADD(pool->getInt32Type(), DBUS_TYPE_INT32);
    ADD(pool->getUint32Type(), DBUS_TYPE_UINT32);
    ADD(pool->getInt16Type(), DBUS_TYPE_INT16);
    ADD(pool->getUint16Type(), DBUS_TYPE_UINT16);
    ADD(pool->getByteType(), DBUS_TYPE_BYTE);

    ADD(pool->getFloatType(), DBUS_TYPE_DOUBLE);

    ADD(pool->getBooleanType(), DBUS_TYPE_BOOLEAN);

    ADD(pool->getStringType(), DBUS_TYPE_STRING);
    ADD(pool->getObjectPathType(), DBUS_TYPE_OBJECT_PATH);

    ADD(pool->getUnixFDType(), DBUS_TYPE_UNIX_FD);
#undef ADD
}

/**
 * return DBUS_TYPE_INVALID, if not base type.
 */
int BaseTypeDescriptorMap::getDescriptor(DSType *type) {
    auto iter = this->map.find((unsigned long) type);
    if(iter != this->map.end()) {
        return iter->second;
    }
    return DBUS_TYPE_INVALID;
}

// ###############################
// ##     DescriptorBuilder     ##
// ###############################

DescriptorBuilder::DescriptorBuilder(TypePool *pool, BaseTypeDescriptorMap *typeMap) :
        pool(pool), typeMap(typeMap), usedSize(0), size(128), buf(new char[this->size]) {
}

DescriptorBuilder::~DescriptorBuilder() {
    delete[] this->buf;
    this->buf = 0;
}

const char * DescriptorBuilder::buildDescriptor(DSType *type) {
    this->usedSize = 0;
    type->accept(this);

    this->append('\0'); // null terminate
    return this->buf;
}

void DescriptorBuilder::visitFunctionType(FunctionType *type) {
    fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
}

void DescriptorBuilder::visitBuiltinType(BuiltinType *type) {
    int dbusType = this->typeMap->getDescriptor(type);
    if(dbusType == DBUS_TYPE_INVALID) {
        if(*type == *this->pool->getVariantType()) {
            this->append(DBUS_TYPE_VARIANT);
            return;
        }
        fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
    }
    this->append(dbusType);
}

void DescriptorBuilder::visitReifiedType(ReifiedType *type) {
    unsigned int elementSize = type->getElementTypes().size();
    if(elementSize == 1) {  // Array
        this->append(DBUS_TYPE_ARRAY);
        type->getElementTypes()[0]->accept(this);
    } else if(elementSize == 2) {   // Map
        this->append(DBUS_TYPE_ARRAY);
        this->append(DBUS_DICT_ENTRY_BEGIN_CHAR);
        type->getElementTypes()[0]->accept(this);
        type->getElementTypes()[1]->accept(this);
        this->append(DBUS_DICT_ENTRY_END_CHAR);
    } else {
        fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
    }
}

void DescriptorBuilder::visitTupleType(TupleType *type) {
    this->append(DBUS_STRUCT_BEGIN_CHAR);
    for(DSType *elementType : type->getTypes()) {
        elementType->accept(this);
    }
    this->append(DBUS_STRUCT_END_CHAR);
}

void DescriptorBuilder::visitInterfaceType(InterfaceType *type) {
    fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
}

void DescriptorBuilder::visitErrorType(ErrorType *type) {
    fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
}

void DescriptorBuilder::append(char ch) {
    if(this->usedSize == this->size) {  // expand buffer
        unsigned int newSize = this->size * 2;
        char newBuf[newSize];
        memcpy(newBuf, this->buf, this->usedSize);
        delete[] this->buf;
        this->buf = newBuf;
        this->size = newSize;
    }
    this->buf[this->usedSize++] = ch;
}

// ############################
// ##     MessageBuilder     ##
// ############################

MessageBuilder::MessageBuilder(TypePool *pool) :
        pool(pool), typeMap(0), descBuilder(0), objStack(), iter() {
}

MessageBuilder::~MessageBuilder() {
    delete this->typeMap;
    this->typeMap = 0;

    delete this->descBuilder;
    this->descBuilder = 0;
}


void MessageBuilder::appendArg(DBusMessageIter *iter, DSType *argType, const std::shared_ptr<DSObject> &arg) {
    this->iter = iter;
    if(this->typeMap == nullptr) {
        this->typeMap = new BaseTypeDescriptorMap(this->pool);
    }

    this->append(argType, arg.get());

    this->iter = 0;
}

void MessageBuilder::visitFunctionType(FunctionType *type) {
    fatal("unsupported type\n");
}

void MessageBuilder::visitBuiltinType(BuiltinType *type) {
    int dbusType = this->typeMap->getDescriptor(type);
    switch(dbusType) {
    case DBUS_TYPE_INT64: {
        dbus_int64_t value = (dbus_int64_t) ((Long_Object *) this->peek())->value;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_UINT64: {
        dbus_uint64_t value = ((Long_Object *) this->peek())->value;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_INT32: {
        dbus_int32_t value = ((Int_Object *) this->peek())->value;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t value = ((Int_Object *) this->peek())->value;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_INT16: {
        dbus_int16_t value = ((Int_Object *) this->peek())->value;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_UINT16: {
        dbus_uint16_t value = ((Int_Object *) this->peek())->value;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_BYTE: {
        unsigned char value = ((Int_Object *) this->peek())->value;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_DOUBLE: {
        double value = ((Float_Object *) this->peek())->value;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t value = ((Boolean_Object *) this->peek())->value ? TRUE : FALSE;
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_STRING: {
        const char *value = ((String_Object *) this->peek())->value.c_str();
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
    case DBUS_TYPE_OBJECT_PATH: {
        const char *value = ((String_Object *) this->peek())->value.c_str();
        dbus_message_iter_append_basic(this->iter, dbusType, &value);
        return;
    };
//    case DBUS_TYPE_UNIX_FD: {
//        break;
//    };
    default:
        if(*type == *this->pool->getVariantType()) {    //variant
            DSType *actualType = this->peek()->getType();
            const char *desc = this->getBuilder()->buildDescriptor(actualType);

            DBusMessageIter *curIter = this->iter;
            DBusMessageIter subIter;
            dbus_message_iter_open_container(this->iter, DBUS_TYPE_VARIANT, desc, &subIter);

            // append value
            this->iter = &subIter;

            this->append(actualType, this->peek());

            this->iter = curIter;
            dbus_message_iter_close_container(this->iter, &subIter);
            return;;
        }

        fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
        break;
    }
}

void MessageBuilder::visitReifiedType(ReifiedType *type) {
    unsigned int elementSize = type->getElementTypes().size();
    if(elementSize == 1) {  // Array
        DSType *elementType = type->getElementTypes()[0];
        const char *desc = this->getBuilder()->buildDescriptor(elementType);

        DBusMessageIter *curIter = this->iter;
        DBusMessageIter subIter;
        dbus_message_iter_open_container(this->iter, DBUS_TYPE_ARRAY, desc, &subIter);

        // append element
        this->iter = &subIter;

        Array_Object *arrayObj = (Array_Object *) this->peek();
        for(auto &e : arrayObj->values) {
            this->append(elementType, e.get());
        }

        this->iter = curIter;
        dbus_message_iter_close_container(this->iter, &subIter);
    } else if(elementSize == 2) {   // Map
        DSType *keyType = type->getElementTypes()[0];
        DSType *valueType = type->getElementTypes()[1];

        const char *desc = (this->getBuilder()->buildDescriptor(type) + 1);

        DBusMessageIter *curIter = this->iter;
        DBusMessageIter subIter;
        dbus_message_iter_open_container(this->iter, DBUS_TYPE_ARRAY, desc, &subIter);

        // append entry
        this->iter = &subIter;

        Map_Object *mapObj = (Map_Object *) this->peek();
        for(auto &pair : mapObj->valueMap) {
            DBusMessageIter *curIter = this->iter;
            DBusMessageIter entryIter;
            dbus_message_iter_open_container(this->iter, DBUS_TYPE_DICT_ENTRY, NULL, &entryIter);

            // append key, value
            this->iter = &entryIter;

            this->append(keyType, pair.first.get());
            this->append(valueType, pair.second.get());

            this->iter = curIter;
            dbus_message_iter_close_container(this->iter, &entryIter);
        }

        this->iter = curIter;
        dbus_message_iter_close_container(this->iter, &subIter);
    } else {
        fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
    }
    fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
}

void MessageBuilder::visitTupleType(TupleType *type) {
    DBusMessageIter *curIter = this->iter;
    DBusMessageIter subIter;
    dbus_message_iter_open_container(this->iter, DBUS_TYPE_STRUCT, NULL, &subIter);

    // append element
    this->iter = &subIter;

    Tuple_Object *tupleObj = (Tuple_Object *) this->peek();
    unsigned int size = tupleObj->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        this->append(type->getTypes()[i], tupleObj->get(i).get());
    }

    this->iter = curIter;
    dbus_message_iter_close_container(this->iter, &subIter);
}

void MessageBuilder::visitInterfaceType(InterfaceType *type) {
    fatal("unsupported type\n");
}

void MessageBuilder::visitErrorType(ErrorType *type) {
    fatal("unsupported type\n");
}

void MessageBuilder::push(DSObject *obj) {
    this->objStack.push_back(obj);
}

DSObject *MessageBuilder::pop() {
    auto value = this->objStack.back();
    this->objStack.pop_back();
    return value;
}

DSObject *MessageBuilder::peek() {
    return this->objStack.back();
}

void MessageBuilder::append(DSType *type, DSObject *value) {
    this->push(value);
    type->accept(this);
    this->pop();
}

DescriptorBuilder *MessageBuilder::getBuilder() {
    if(this->descBuilder == nullptr) {
        this->descBuilder = new DescriptorBuilder(this->pool, this->typeMap);
    }
    return this->descBuilder;
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

DBus_ObjectImpl::DBus_ObjectImpl(TypePool *typePool) :
        DBus_Object(typePool), systemBus(), sessionBus(), builder(typePool) {
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
    unrefMessage(msg);

    if(dbus_error_is_set(&error)) {
        reportError(ctx, error);

        dbus_error_free(&error);
        unrefMessage(ret);

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
    unrefMessage(ret);

    return true;
}

//FIXME: empty array
static std::shared_ptr<DSObject> decodeMessageIterImpl(RuntimeContext &ctx, DBusMessageIter *iter) {
    int dbusType = dbus_message_iter_get_arg_type(iter);
    switch(dbusType) {
    case DBUS_TYPE_BYTE: {
        unsigned char value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.pool.getByteType(), value);
    };
    case DBUS_TYPE_INT16: {
        dbus_int16_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.pool.getInt16Type(), value);
    };
    case DBUS_TYPE_UINT16: {
        dbus_uint16_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.pool.getUint16Type(), value);
    };
    case DBUS_TYPE_INT32: {
        dbus_int32_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.pool.getInt32Type(), value);
    };
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Int_Object>(ctx.pool.getUint32Type(), value);
    };
    case DBUS_TYPE_INT64: {
        dbus_int64_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Long_Object>(ctx.pool.getInt64Type(), value);
    };
    case DBUS_TYPE_UINT64: {
        dbus_uint64_t value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Long_Object>(ctx.pool.getUint64Type(), value);
    };
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t value;
        dbus_message_iter_get_basic(iter, &value);
        return value == TRUE ? ctx.trueObj : ctx.falseObj;
    };
    case DBUS_TYPE_DOUBLE: {
        double value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<Float_Object>(ctx.pool.getFloatType(), value);
    };
    case DBUS_TYPE_STRING: {
        const char *value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<String_Object>(ctx.pool.getStringType(), std::string(value));
    };
    case DBUS_TYPE_OBJECT_PATH: {
        const char *value;
        dbus_message_iter_get_basic(iter, &value);
        return std::make_shared<String_Object>(ctx.pool.getObjectPathType(), std::string(value));
    };
//    case DBUS_TYPE_UNIX_FD: {
//        fatal("unsupported dbus type: UNIX_FD");
//        return std::shared_ptr<DSObject>(nullptr);
//    };

    case DBUS_TYPE_ARRAY: {
        DBusMessageIter subIter;
        dbus_message_iter_recurse(iter, &subIter);
        int elementType = dbus_message_iter_get_arg_type(&subIter);
        if(elementType == DBUS_TYPE_DICT_ENTRY) {   // map
            std::vector<std::pair<std::shared_ptr<DSObject>, std::shared_ptr<DSObject>>> entries;
            do {
                DBusMessageIter entryIter;
                dbus_message_iter_recurse(&subIter, &entryIter);

                auto key(decodeMessageIterImpl(ctx, &entryIter));
                dbus_message_iter_next(&entryIter);
                auto value(decodeMessageIterImpl(ctx, &entryIter));
                dbus_message_iter_next(&entryIter);
                elementType = dbus_message_iter_get_arg_type(&entryIter);
                entries.push_back(std::make_pair(std::move(key), std::move(value)));
            } while(elementType != DBUS_TYPE_INVALID);  //FIXME: support empty map
            std::vector<DSType *> types(2);
            types[0] = entries.back().first->getType();
            types[1] = entries.back().second->getType();

            auto map = std::make_shared<Map_Object>(
                   ctx.pool.createAndGetReifiedTypeIfUndefined(ctx.pool.getMapTemplate(), types));
            unsigned int size = entries.size();
            for(unsigned int i = 0; i < size; i++) {
                map->add(std::move(entries[i]));
            }
            return std::move(map);
        } else {    // array
            std::vector<std::shared_ptr<DSObject>> values;
            do {
                values.push_back(decodeMessageIterImpl(ctx, &subIter));
                dbus_message_iter_next(&subIter);
                elementType = dbus_message_iter_get_arg_type(&subIter);
            } while(elementType != DBUS_TYPE_INVALID);    //FIXME: support empty array
            std::vector<DSType *> types(1);
            types[0] = values[0]->getType();

            return std::make_shared<Array_Object>(
                    ctx.pool.createAndGetReifiedTypeIfUndefined(ctx.pool.getArrayTemplate(), types), std::move(values));
        }

    };
    case DBUS_TYPE_STRUCT: {
        DBusMessageIter subIter;
        dbus_message_iter_recurse(iter, &subIter);

        int elementType;
        std::vector<DSType *> types;
        std::vector<std::shared_ptr<DSObject>> values;
        do {
            values.push_back(decodeMessageIterImpl(ctx, &subIter));
            types.push_back(values.back()->getType());
            dbus_message_iter_next(&subIter);
            elementType = dbus_message_iter_get_arg_type(&subIter);
        } while(elementType != DBUS_TYPE_INVALID);
        DSType *tupleType = ctx.pool.createAndGetTupleTypeIfUndefined(types);
        std::shared_ptr<Tuple_Object> tuple(new Tuple_Object(tupleType));
        unsigned int size = types.size();
        for(unsigned int i = 0; i < size; i++) {
            tuple->set(i, values[i]);
        }
        return std::move(tuple);
    };
    case DBUS_TYPE_VARIANT: {
        DBusMessageIter subIter;
        dbus_message_iter_recurse(iter, &subIter);

        return decodeMessageIterImpl(ctx, &subIter);
    };
    default:
        fatal("unsupported dbus type: %c\n", (char)dbusType);
        return std::shared_ptr<DSObject>(nullptr);
    }
}

static std::shared_ptr<DSObject> decodeMessageIter(RuntimeContext &ctx, DBusMessageIter *iter) {
    std::vector<DSType *> valueTypes;
    std::vector<std::shared_ptr<DSObject>> values;  // contains decoded value;

    do {
        values.push_back(decodeMessageIterImpl(ctx, iter));
        valueTypes.push_back(values.back()->getType());
    } while(dbus_message_iter_next(iter));

    unsigned int size = values.size();
    if(size == 1) {
        return std::move(values[0]);
    } else if(size == 0) {
        fatal("broken message, must need more than one argument");
    }

    std::shared_ptr<Tuple_Object> tuple(
            new Tuple_Object(ctx.pool.createAndGetTupleTypeIfUndefined(valueTypes)));
    for(unsigned int i = 0; i < size; i++) {
        tuple->set(i, values[i]);
    }

    return std::move(tuple);
}

static void appendArg(RuntimeContext &ctx, DBusMessageIter *iter,
                      DSType *argType, const std::shared_ptr<DSObject> &arg) {
    DBus_ObjectImpl *dbus = (DBus_ObjectImpl *)  ctx.dbus.get();
    dbus->builder.appendArg(iter, argType, arg);
}

static void appendArg(RuntimeContext &ctx, DBusMessageIter *iter,
                      DSType *argType, unsigned int index) {
    appendArg(ctx, iter, argType, ctx.localStack[ctx.localVarOffset + index]);
}

bool DBusProxy_Object::invokeMethod(RuntimeContext &ctx, const std::string &methodName, MethodHandle *handle) {
    DBusError error;
    dbus_error_init(&error);

    DBusMessage *msg = this->newMethodCallMsg(ctx.pool.getTypeName(*handle->getRecvType()), methodName);

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);

    unsigned int paramSize = handle->getParamTypes().size();
    for(unsigned int i = 0; i < paramSize; i++) {
        appendArg(ctx, &iter, handle->getParamTypes()[i], i);
    }

    // send message
    DBusMessage *retMsg = dbus_connection_send_with_reply_and_block(this->conn, msg, DBUS_TIMEOUT_USE_DEFAULT, &error);
    unrefMessage(msg);

    if(dbus_error_is_set(&error)) {
        reportError(ctx, error);

        dbus_error_free(&error);
        unrefMessage(retMsg);

        return false;
    }

    // decode result
    DBusMessageIter resultIter;
    dbus_message_iter_init(retMsg, &resultIter);
    auto result(decodeMessageIter(ctx, &resultIter));

    unrefMessage(retMsg);
    ctx.returnObject = std::move(result);
    return true;
}

bool DBusProxy_Object::invokeGetter(RuntimeContext &ctx,DSType *recvType,
                                    const std::string &fieldName, DSType *fieldType) {
    DBusError error;
    dbus_error_init(&error);

    DBusMessage *msg = this->newMethodCallMsg("org.freedesktop.DBus.Properties", "Get");

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);

    const char *ifaceName = ctx.pool.getTypeName(*recvType).c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifaceName);

    const char *propertyName = fieldName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &propertyName);

    DBusMessage *ret = dbus_connection_send_with_reply_and_block(this->conn, msg, DBUS_TIMEOUT_USE_DEFAULT, &error);
    unrefMessage(msg);

    if(dbus_error_is_set(&error)) {
        reportError(ctx, error);

        dbus_error_free(&error);
        unrefMessage(ret);

        return false;
    }

    // decode result
    DBusMessageIter resultIter;
    dbus_message_iter_init(ret, &resultIter);
    auto result(decodeMessageIter(ctx, &resultIter));

    unrefMessage(ret);
    ctx.push(result);
    return true;
}

bool DBusProxy_Object::invokeSetter(RuntimeContext &ctx,DSType *recvType,
                                    const std::string &fieldName, DSType *fieldType) {
    DBusError error;
    dbus_error_init(&error);

    DBusMessage *msg = this->newMethodCallMsg("org.freedesktop.DBus.Properties", "Set");

    // append arg
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);

    const char *ifaceName = ctx.pool.getTypeName(*recvType).c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ifaceName);

    const char *propertyName = fieldName.c_str();
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &propertyName);

    appendArg(ctx, &iter, ctx.pool.getVariantType(), ctx.localStack[ctx.stackTopIndex]);


    DBusMessage *ret = dbus_connection_send_with_reply_and_block(this->conn, msg, DBUS_TIMEOUT_USE_DEFAULT, &error);
    unrefMessage(msg);

    if(dbus_error_is_set(&error)) {
        reportError(ctx, error);

        dbus_error_free(&error);
        unrefMessage(ret);

        return false;
    }
    return true;
}

DBusMessage *DBusProxy_Object::newMethodCallMsg(const char *ifaceName, const char *methodName) {
    return dbus_message_new_method_call(
            this->destination.c_str(), this->objectPath.c_str(), ifaceName, methodName);
}

DBusMessage *DBusProxy_Object::newMethodCallMsg(const std::string &ifaceName, const std::string &methodName) {
    return this->newMethodCallMsg(ifaceName.c_str(), methodName.c_str());
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