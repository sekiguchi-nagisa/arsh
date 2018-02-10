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
#ifndef YDSH_DBUS_DBUS_UTIL_H
#define YDSH_DBUS_DBUS_UTIL_H

extern "C" {
#include <dbus/dbus.h>
}

#include "type.h"
#include "object.h"
#include "symbol_table.h"

namespace ydsh {

/**
 * for base type descriptor
 */
class BaseTypeDescriptorMap {
private:
    std::unordered_map<unsigned long, int> map;

public:
    explicit BaseTypeDescriptorMap(SymbolTable *pool);
    ~BaseTypeDescriptorMap() = default;

    /**
     * return DBUS_TYPE_INVALID, if not base type.
     */
    int getDescriptor(DSType &type);
};


/**
 * for container type descriptor generation.
 */
class DescriptorBuilder : public TypeVisitor {
private:
    SymbolTable *pool;
    BaseTypeDescriptorMap *typeMap;
    std::string buf;

public:
    DescriptorBuilder(SymbolTable *pool, BaseTypeDescriptorMap *typeMap) : pool(pool), typeMap(typeMap) { }
    ~DescriptorBuilder() override = default;

    const char *buildDescriptor(DSType &type);

    void visitFunctionType(FunctionType *type) override;
    void visitBuiltinType(BuiltinType *type) override;
    void visitReifiedType(ReifiedType *type) override;
    void visitTupleType(TupleType *type) override;
    void visitInterfaceType(InterfaceType *type) override;
    void visitErrorType(ErrorType *type) override;

private:
    void append(char ch) {
        this->buf += ch;
    }
};

class MessageBuilder : public TypeVisitor {
private:
    SymbolTable *pool;

    BaseTypeDescriptorMap *typeMap;
    DescriptorBuilder *descBuilder;

    std::vector<DSObject *> objStack;

    DBusMessageIter *iter;

public:
    explicit MessageBuilder(SymbolTable *pool) :
            pool(pool), typeMap(nullptr), descBuilder(nullptr), iter() { }

    ~MessageBuilder() override {
        delete this->typeMap;
        delete this->descBuilder;
    }

    void appendArg(DBusMessageIter *iter, DSType &argType, const DSValue &arg);

    void visitFunctionType(FunctionType *type) override;
    void visitBuiltinType(BuiltinType *type) override;
    void visitReifiedType(ReifiedType *type) override;
    void visitTupleType(TupleType *type) override;
    void visitInterfaceType(InterfaceType *type) override;
    void visitErrorType(ErrorType *type) override;

private:
    DSObject *peek() {
        return this->objStack.back();
    }

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

DSType &decodeTypeDescriptor(SymbolTable *pool, const char *desc);

} // namespace ydsh


#endif //YDSH_DBUS_DBUS_UTIL_H
