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

#include "DBusUtil.h"
#include "../misc/debug.h"

namespace ydsh {
namespace core {

// ###################################
// ##     BaseTypeDescriptorMap     ##
// ###################################

BaseTypeDescriptorMap::BaseTypeDescriptorMap(TypePool *pool) : map() {
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
        pool(pool), typeMap(typeMap), buf() {
}

DescriptorBuilder::~DescriptorBuilder() {
}

const char * DescriptorBuilder::buildDescriptor(DSType *type) {
    this->buf.clear();
    type->accept(this);
    return this->buf.c_str();
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
    this->buf += ch;
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
    fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
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

            DBusMessageIter subIter;
            DBusMessageIter *curIter = this->openContainerIter(DBUS_TYPE_VARIANT, desc, &subIter);

            // append value
            this->append(actualType, this->peek());

            this->closeContainerIter(curIter, &subIter);
            return;
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

        DBusMessageIter subIter;
        DBusMessageIter *curIter = this->openContainerIter(DBUS_TYPE_ARRAY, desc, &subIter);

        // append element
        Array_Object *arrayObj = (Array_Object *) this->peek();
        for(auto &e : arrayObj->values) {
            this->append(elementType, e.get());
        }

        this->closeContainerIter(curIter, &subIter);
    } else if(elementSize == 2) {   // Map
        DSType *keyType = type->getElementTypes()[0];
        DSType *valueType = type->getElementTypes()[1];

        const char *desc = (this->getBuilder()->buildDescriptor(type) + 1);

        DBusMessageIter subIter;
        DBusMessageIter *curIter = this->openContainerIter(DBUS_TYPE_ARRAY, desc, &subIter);

        // append entry
        Map_Object *mapObj = (Map_Object *) this->peek();
        for(auto &pair : mapObj->valueMap) {
            DBusMessageIter entryIter;
            DBusMessageIter *curIter = this->openContainerIter(DBUS_TYPE_DICT_ENTRY, NULL, &entryIter);

            // append key, value
            this->append(keyType, pair.first.get());
            this->append(valueType, pair.second.get());

            this->closeContainerIter(curIter, &entryIter);
        }

        this->closeContainerIter(curIter, &subIter);
    } else {
        fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
    }
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
    fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
}

void MessageBuilder::visitErrorType(ErrorType *type) {
    fatal("unsupported type: %s\n", this->pool->getTypeName(*type).c_str());
}

DSObject *MessageBuilder::peek() {
    return this->objStack.back();
}

void MessageBuilder::append(DSType *type, DSObject *value) {
    this->objStack.push_back(value);
    type->accept(this);
    this->objStack.pop_back();
}

DescriptorBuilder *MessageBuilder::getBuilder() {
    if(this->descBuilder == nullptr) {
        this->descBuilder = new DescriptorBuilder(this->pool, this->typeMap);
    }
    return this->descBuilder;
}

DBusMessageIter *MessageBuilder::openContainerIter(int dbusType, const char *desc, DBusMessageIter *subIter) {
    dbus_message_iter_open_container(this->iter, dbusType, desc, subIter);
    DBusMessageIter *old = this->iter;
    this->iter = subIter;
    return old;
}

void MessageBuilder::closeContainerIter(DBusMessageIter *parentIter, DBusMessageIter *subIter) {
    this->iter = parentIter;
    dbus_message_iter_close_container(this->iter, subIter);
}

} // namespace core
} // namespace ydsh