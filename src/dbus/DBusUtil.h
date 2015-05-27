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
#ifndef YDSH_DBUSUTIL_H
#define YDSH_DBUSUTIL_H

#include "../core/DSType.h"
#include "../core/DSObject.h"
#include "../core/TypePool.h"

extern "C" {
#include <dbus/dbus.h>
}

namespace ydsh {
namespace core {

/**
 * for base type descriptor
 */
class BaseTypeDescriptorMap {
private:
    std::unordered_map<unsigned long, int> map;

public:
    explicit BaseTypeDescriptorMap(TypePool *pool);
    ~BaseTypeDescriptorMap() = default;

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
    ~DescriptorBuilder() = default;

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
    explicit MessageBuilder(TypePool *pool);
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


} // namespace core
} // namespace ydsh


#endif //YDSH_DBUSUTIL_H
