/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#ifndef YDSH_TYPE_H
#define YDSH_TYPE_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <utility>
#include <memory>

#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "handle_info.h"

struct DSState;

namespace ydsh {

struct TypeVisitor;
class TypePool;
class FieldHandle;
class MethodHandle;
class DSValue;
class DSCode;
typedef DSValue (*native_func_t)(DSState &);

class DSType {
protected:
    /**
     * if this type is Void or Any type, superType is null
     */
    DSType *superType;

    flag8_set_t attributeSet;

public:
    static constexpr flag8_t EXTENDIBLE   = 1 << 0;
    static constexpr flag8_t VOID_TYPE    = 1 << 1;  // Void
    static constexpr flag8_t FUNC_TYPE    = 1 << 2;  // function type
    static constexpr flag8_t IFACE_TYPE   = 1 << 3;  // interface
    static constexpr flag8_t RECORD_TYPE  = 1 << 4;  // indicate user defined type
    static constexpr flag8_t NOTHING_TYPE = 1 << 5;  // Nothing (a.k.a. Bottom type)
    static constexpr flag8_t OPTION_TYPE  = 1 << 6;  // Option<T>

    NON_COPYABLE(DSType);

    /**
     * not directly call it.
     */
    DSType(DSType *superType, flag8_set_t attribute) :
            superType(superType), attributeSet(attribute) { }

    virtual ~DSType() = default;

    /**
     * if true, can extend this type
     */
    bool isExtendible() const {
        return hasFlag(this->attributeSet, EXTENDIBLE);
    }

    /**
     * if this type is VoidType, return true.
     */
    bool isVoidType() const {
        return hasFlag(this->attributeSet, VOID_TYPE);
    }

    /**
     * if this type is FunctionType, return true.
     */
    bool isFuncType() const {
        return hasFlag(this->attributeSet, FUNC_TYPE);
    }

    /**
     * if this type is InterfaceType, return true.
     */
    bool isInterface() const {
        return hasFlag(this->attributeSet, IFACE_TYPE);
    }

    bool isRecordType() const {
        return hasFlag(this->attributeSet, RECORD_TYPE);
    }

    bool isNothingType() const {
        return hasFlag(this->attributeSet, NOTHING_TYPE);
    }

    bool isOptionType() const {
        return hasFlag(this->attributeSet, OPTION_TYPE);
    }

    /**
     * get super type of this type.
     * return null, if has no super type(ex. AnyType, VoidType).
     */
    DSType *getSuperType() const {
        return this->superType;
    }

    /**
     * return null, if has no constructor
     */
    virtual MethodHandle *getConstructorHandle(TypePool &typePool);

    /**
     * return null, if has no constructor
     */
    virtual const DSCode *getConstructor();

    /**
     * get size of the all fields(include superType fieldSize).
     */
    virtual unsigned int getFieldSize();

    /**
     * get size of the all methods(include superType method size)
     */
    virtual unsigned int getMethodSize();

    /**
     * return null, if has no field
     */
    virtual FieldHandle *lookupFieldHandle(TypePool &typePool, const std::string &fieldName);

    /**
     * return null, if has no field.
     */
    virtual MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName);

    /**
     * return null if handle not found.
     * not directly use it
     */
    virtual FieldHandle *findHandle(const std::string &fieldName) = 0;

    virtual void accept(TypeVisitor *visitor) = 0;

    bool operator==(const DSType &type) const {
        return (unsigned long) this == (unsigned long) &type;
    }

    bool operator!=(const DSType &type) const {
        return (unsigned long) this != (unsigned long) &type;
    }

    /**
     * check inheritance of target type.
     * if this type is equivalent to target type or
     * the super type of target type, return true.
     */
    bool isSameOrBaseTypeOf(const DSType &targetType) const;

    virtual const DSCode *getMethodRef(unsigned int methodIndex);
    virtual void copyAllMethodRef(std::vector<const DSCode *> &methodTable);
};

class FunctionType : public DSType {
private:
    DSType *returnType;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<DSType *> paramTypes;

public:
    FunctionType(DSType *superType, DSType *returnType, std::vector<DSType *> &&paramTypes) :
            DSType(superType, DSType::FUNC_TYPE),
            returnType(returnType), paramTypes(std::move(paramTypes)) {}

    ~FunctionType() override = default;

    DSType *getReturnType() const {
        return this->returnType;
    }

    /**
     * may be empty vector, if has no parameter (getParamSize() == 0)
     */
    const std::vector<DSType *> &getParamTypes() const {
        return this->paramTypes;
    }

    /**
     * lookup from super type
     */
    MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName) override;

    FieldHandle *findHandle(const std::string &fieldName) override;
    void accept(TypeVisitor *visitor) override;
};

/**
 * for method handle creation.
 */
struct NativeFuncInfo {
    /**
     * if empty string, treat as constructor.
     */
    const char *funcName;

    /**
     * serialized function handle
     */
    char handleInfo[24];

    /**
     * bool func(RuntimeContext &ctx)
     */
    native_func_t func_ptr;
};

struct native_type_info_t {
    const unsigned short offset;

    /**
     * may be 0.
     */
    const unsigned char constructorSize;

    const unsigned char methodSize;


    NativeFuncInfo &getMethodInfo(unsigned int index);

    /**
     * not call it if constructorSize is 0
     */
    NativeFuncInfo &getInitInfo();
};

/**
 * builtin type(any, void, value ...)
 * not support override. (if override method, must override DSObject's method)
 * so this->getFieldSize is equivalent to superType->getFieldSize() + infoSize
 */
class BuiltinType : public DSType {
protected:
    native_type_info_t info;

    /**
     * may be null, if has no constructor.
     */
    MethodHandle *constructorHandle;

    /**
     * may be null, if has no constructor
     */
    const DSCode *constructor;

    std::unordered_map<std::string, MethodHandle *> methodHandleMap;
    std::vector<const DSCode *> methodTable;

public:
    BuiltinType(DSType *superType, native_type_info_t info, flag8_set_t attribute);

    ~BuiltinType() override;

    MethodHandle *getConstructorHandle(TypePool &typePool) override;
    const DSCode *getConstructor() override;
    MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName) override;

    FieldHandle *findHandle(const std::string &fieldName) override;

    void accept(TypeVisitor *visitor) override;
    unsigned int getMethodSize() override;
    const DSCode *getMethodRef(unsigned int methodIndex) override;
    void copyAllMethodRef(std::vector<const DSCode *> &methodTable) override;

protected:
    virtual bool initMethodHandle(MethodHandle *handle, TypePool &typePool, NativeFuncInfo &info);
};

/**
 * not support override.
 */
class ReifiedType : public BuiltinType {
protected:
    /**
     * size is 1 or 2.
     */
    std::vector<DSType *> elementTypes;

public:
    /**
     * super type is AnyType or VariantType.
     */
    ReifiedType(native_type_info_t info, DSType *superType,
                std::vector<DSType *> &&elementTypes, flag8_set_t attribute = 0) :
            BuiltinType(superType, info, attribute), elementTypes(std::move(elementTypes)) { }

    ~ReifiedType() override = default;

    const std::vector<DSType *> &getElementTypes() const {
        return this->elementTypes;
    }

    void accept(TypeVisitor *visitor) override;
protected:
    bool initMethodHandle(MethodHandle *handle, TypePool &typePool, NativeFuncInfo &info) override;
};


class TupleType : public ReifiedType {
private:
    std::unordered_map<std::string, FieldHandle *> fieldHandleMap;

public:
    /**
     * superType is AnyType ot VariantType
     */
    TupleType(native_type_info_t info, DSType *superType, std::vector<DSType *> &&types);
    ~TupleType() override;

    /**
     * return types.size()
     */
    unsigned int getFieldSize() override;

    FieldHandle *lookupFieldHandle(TypePool &typePool, const std::string &fieldName) override;
    FieldHandle *findHandle(const std::string &fieldName) override;
    void accept(TypeVisitor *visitor) override;
};


/**
 * for D-Bus interface
 */
class InterfaceType : public DSType {
private:
    std::unordered_map<std::string, FieldHandle *> fieldHandleMap;
    std::unordered_map<std::string, MethodHandle *> methodHandleMap;

public:
    /**
     * superType is always AnyType.
     */
    explicit InterfaceType(DSType *superType) :
            DSType(superType, DSType::IFACE_TYPE) { }

    ~InterfaceType() override;

    /**
     * return null, if found duplicated field
     */
    FieldHandle *newFieldHandle(const std::string &fieldName, DSType &fieldType, bool readOnly);

    MethodHandle *newMethodHandle(const std::string &methodName);

    unsigned int getFieldSize() override;
    unsigned int getMethodSize() override;
    FieldHandle *lookupFieldHandle(TypePool &typePool, const std::string &fieldName) override;
    MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName) override;
    FieldHandle *findHandle(const std::string &fieldName) override;
    void accept(TypeVisitor *visitor) override;
};

class ErrorType : public DSType {
private:
    MethodHandle *constructorHandle;

    static NativeFuncInfo *funcInfo;
    static const DSCode *initRef;

public:
    explicit ErrorType(DSType *superType) :
            DSType(superType, DSType::EXTENDIBLE),
            constructorHandle() { }

    ~ErrorType() override;

    MethodHandle *getConstructorHandle(TypePool &typePool) override;
    const DSCode *getConstructor() override;

    /**
     * return types.size()
     */
    unsigned int getFieldSize() override;

    FieldHandle *lookupFieldHandle(TypePool &typePool, const std::string &fieldName) override;
    MethodHandle *lookupMethodHandle(TypePool &typePool, const std::string &methodName) override;
    FieldHandle *findHandle(const std::string &fieldName) override;
    void accept(TypeVisitor *visitor) override;
    /**
     * call only once.
     */
    static void registerFuncInfo(native_type_info_t info);
};

struct TypeVisitor {
    virtual ~TypeVisitor() = default;

    virtual void visitFunctionType(FunctionType *type) = 0;
    virtual void visitBuiltinType(BuiltinType *type) = 0;
    virtual void visitReifiedType(ReifiedType *type) = 0;
    virtual void visitTupleType(TupleType *type) = 0;
    virtual void visitInterfaceType(InterfaceType *type) = 0;
    virtual void visitErrorType(ErrorType *type) = 0;
};

/**
 * ReifiedType template.
 */
class TypeTemplate {
private:
    std::string name;

    std::vector<DSType*> acceptableTypes;

    native_type_info_t info;

public:
    TypeTemplate(std::string &&name, std::vector<DSType*> &&elementTypes, native_type_info_t info) :
            name(std::move(name)), acceptableTypes(std::move(elementTypes)), info(info) { }

    ~TypeTemplate() = default;

    const std::string &getName() const {
        return this->name;
    }

    unsigned int getElementTypeSize() const {
        return this->acceptableTypes.size();
    }

    native_type_info_t getInfo() const {
        return this->info;
    }

    const std::vector<DSType *> &getAcceptableTypes() const {
        return this->acceptableTypes;
    }
};


class TypeMap {
private:
    std::unordered_map<std::string, DSType *> typeMapImpl;
    std::unordered_map<unsigned long, const std::string *> typeNameMap;

    /**
     * cache generated type(interface).
     */
    std::vector<const std::string *> typeCache;

public:
    NON_COPYABLE(TypeMap);

    TypeMap() = default;
    ~TypeMap();

    /**
     * return added type. type must not be null.
     */
    DSType *addType(std::string &&typeName, DSType *type);

    /**
     * return null, if has no type.
     */
    DSType *getType(const std::string &typeName) const;

    /**
     * type must not be null.
     */
    const std::string &getTypeName(const DSType &type) const;

    /**
     * return false, if duplicated
     */
    bool setAlias(std::string &&alias, DSType &targetType);

    /**
     * clear typeCache.
     */
    void commit();

    /**
     * remove cached type
     */
    void abort();


private:
    /**
     * remove type and call destructor.
     */
    void removeType(const std::string &typeName);
};

class TypePool {
public:
    enum DS_TYPE : unsigned int {
        Root__, // pseudo top type of all throwable type(except for option types)
        Any,
        Void,
        Nothing,
        Variant,    // for base type of all of D-Bus related type.
        Value__,    // super type of value type(int, float, bool, string). not directly used it.
        Byte,       // unsigned int 8
        Int16,
        Uint16,
        Int32,
        Uint32,
        Int64,
        Uint64,
        Float,
        Boolean,
        String,
        StringArray,    // for command argument
        Regex,
        Signal,
        Signals,
        Error,
        Job,
        Func,
        StringIter__,
        ObjectPath, // for D-Bus object path
        UnixFD,     // for Unix file descriptor
        Proxy,
        DBus,       // for owner type of each bus object
        Bus,        // for message bus.
        Service,    // for service
        DBusObject, // for D-Bus proxy instance
        ArithmeticError,
        OutOfRangeError,
        KeyNotFoundError,
        TypeCastError,
        DBusError,
        SystemError,    // for errno
        StackOverflowError,
        RegexSyntaxError,
        UnwrapingError,

        /**
         * for internal status reporting.
         * they are pseudo type, so must not use it from shell
         */
        InternalStatus__,   // base type
        ShellExit__,
        AssertFail__,

        __SIZE_OF_DS_TYPE__,    // for enum size counting
    };

private:
    TypeMap typeMap;
    DSType **typeTable; //for builtin type lookup

    /**
     * for type template
     */
    std::unordered_map<std::string, TypeTemplate *> templateMap;

    // type template definition
    TypeTemplate *arrayTemplate;
    TypeTemplate *mapTemplate;
    TypeTemplate *tupleTemplate;
    TypeTemplate *optionTemplate;

public:
    NON_COPYABLE(TypePool);

    TypePool();

    ~TypePool();

    /**
     * get any type (root class of ydsh class)
     */
    DSType &getAnyType() const {
        return *this->typeTable[Any];
    }

    /**
     * get void type (pseudo class representing for void)
     */
    DSType &getVoidType() const {
        return *this->typeTable[Void];
    }

    DSType &getNothingType() const {
        return *this->typeTable[Nothing];
    }

    DSType &getVariantType() const {
        return *this->typeTable[Variant];
    }

    DSType &getValueType() const {
        return *this->typeTable[Value__];
    }

    /**
     * int is 32bit.
     */
    DSType &getIntType() const {
        return this->getInt32Type();
    }

    DSType &getByteType() const {
        return *this->typeTable[Byte];
    }

    DSType &getInt16Type() const {
        return *this->typeTable[Int16];
    }

    DSType &getUint16Type() const {
        return *this->typeTable[Uint16];
    }

    DSType &getInt32Type() const {
        return *this->typeTable[Int32];
    }

    DSType &getUint32Type() const {
        return *this->typeTable[Uint32];
    }

    DSType &getInt64Type() const {
        return *this->typeTable[Int64];
    }

    DSType &getUint64Type() const {
        return *this->typeTable[Uint64];
    }

    /**
     * float is 64bit.
     */
    DSType &getFloatType() const {
        return *this->typeTable[Float];
    }

    DSType &getBooleanType() const {
        return *this->typeTable[Boolean];
    }

    DSType &getStringType() const {
        return *this->typeTable[String];
    }

    DSType &getErrorType() const {
        return *this->typeTable[Error];
    }

    DSType &getJobType() const {
        return *this->typeTable[Job];
    }

    DSType &getBaseFuncType() const {
        return *this->typeTable[Func];
    }

    DSType &getStringIterType() const {
        return *this->typeTable[StringIter__];
    }

    DSType &getRegexType() const {
        return *this->typeTable[Regex];
    }

    DSType &getSignalType() const {
        return *this->typeTable[Signal];
    }

    DSType &getSignalsType() const {
        return *this->typeTable[Signals];
    }

    DSType &getObjectPathType() const {
        return *this->typeTable[ObjectPath];
    }

    DSType &getUnixFDType() const {
        return *this->typeTable[UnixFD];
    }

    DSType &getProxyType() const {
        return *this->typeTable[Proxy];
    }

    DSType &getDBusType() const {
        return *this->typeTable[DBus];
    }

    DSType &getBusType() const {
        return *this->typeTable[Bus];
    }

    DSType &getServiceType() const {
        return *this->typeTable[Service];
    }

    DSType &getDBusObjectType() const {
        return *this->typeTable[DBusObject];
    }

    DSType &getStringArrayType() const {
        return *this->typeTable[StringArray];
    }

    // for error
    DSType &getArithmeticErrorType() const {
        return *this->typeTable[ArithmeticError];
    }

    DSType &getOutOfRangeErrorType() const {
        return *this->typeTable[OutOfRangeError];
    }

    DSType &getKeyNotFoundErrorType() const {
        return *this->typeTable[KeyNotFoundError];
    }

    DSType &getTypeCastErrorType() const {
        return *this->typeTable[TypeCastError];
    }

    DSType &getDBusErrorType() const {
        return *this->typeTable[DBusError];
    }

    DSType &getSystemErrorType() const {
        return *this->typeTable[SystemError];
    }

    DSType &getStackOverflowErrorType() const {
        return *this->typeTable[StackOverflowError];
    }

    DSType &getRegexSyntaxErrorType() const {
        return *this->typeTable[RegexSyntaxError];
    }

    DSType &getUnwrappingErrorType() const {
        return *this->typeTable[UnwrapingError];
    }

    // for internal status reporting
    DSType &getRoot() const {
        return *this->typeTable[Root__];
    }

    DSType &getInternalStatus() const {
        return *this->typeTable[InternalStatus__];
    }

    DSType &getShellExit() const {
        return *this->typeTable[ShellExit__];
    }

    DSType &getAssertFail() const {
        return *this->typeTable[AssertFail__];
    }

    // for reified type.
    const TypeTemplate &getArrayTemplate() const {
        return *this->arrayTemplate;
    }

    const TypeTemplate &getMapTemplate() const {
        return *this->mapTemplate;
    }

    const TypeTemplate &getTupleTemplate() const {
        return *this->tupleTemplate;
    }

    const TypeTemplate &getOptionTemplate() const {
        return *this->optionTemplate;
    }

    // for type lookup

    /**
     * return null, if type is not defined.
     */
    DSType *getType(const std::string &typeName) const {
        return this->typeMap.getType(typeName);
    }

    /**
     * get type except template type.
     * if type is undefined, throw exception
     */
    DSType &getTypeAndThrowIfUndefined(const std::string &typeName) const;

    /**
     * get template type.
     * if template type is not found, throw exception
     */
    const TypeTemplate &getTypeTemplate(const std::string &typeName) const;

    /**
     * if type template is Tuple, call createTupleType()
     */
    DSType &createReifiedType(const TypeTemplate &typeTemplate, std::vector<DSType *> &&elementTypes);

    DSType &createTupleType(std::vector<DSType *> &&elementTypes);

    FunctionType &createFuncType(DSType *returnType, std::vector<DSType *> &&paramTypes);

    InterfaceType &createInterfaceType(const std::string &interfaceName);

    DSType &createErrorType(const std::string &errorName, DSType &superType);

    /**
     * if not found type, search directory /etc/ydsh/dbus/
     */
    DSType &getDBusInterfaceType(const std::string &typeName);

    /**
     * set type name alias. if alias name has already defined, report error.
     */
    void setAlias(const std::string &alias, DSType &targetType) {
        this->setAlias(alias.c_str(), targetType);
    }

    void setAlias(const char *alias, DSType &targetType);

    const char *getTypeName(const DSType &type) const {
        return this->typeMap.getTypeName(type).c_str();
    }

    /**
     * create reified type name
     * equivalent to toReifiedTypeName(typeTemplate->getName(), elementTypes)
     */
    std::string toReifiedTypeName(const TypeTemplate &typeTemplate, const std::vector<DSType *> &elementTypes) const {
        return this->toReifiedTypeName(typeTemplate.getName(), elementTypes);
    }

    std::string toReifiedTypeName(const std::string &name, const std::vector<DSType *> &elementTypes) const;

    std::string toTupleTypeName(const std::vector<DSType *> &elementTypes) const {
        return this->toReifiedTypeName("Tuple", elementTypes);
    }

    /**
     * create function type name
     */
    std::string toFunctionTypeName(DSType *returnType, const std::vector<DSType *> &paramTypes) const;

    static constexpr int INT64_PRECISION = 50;
    static constexpr int INT32_PRECISION = 40;
    static constexpr int INT16_PRECISION = 30;
    static constexpr int BYTE_PRECISION = 20;
    static constexpr int INVALID_PRECISION = 1;

    /**
     * get integer precision. if type is not int type, return INVALID_PRECISION.
     */
    int getIntPrecision(const DSType &type) const;

    /**
     * if type is not number type, return -1.
     */
    int getNumTypeIndex(const DSType &type) const;

    /**
     * if not found, return null.
     */
    DSType *getByNumTypeIndex(unsigned int index) const;

    /**
     * commit changed state(type)
     */
    void commit() {
        this->typeMap.commit();
    }

    /**
     * abort changed state(type)
     */
    void abort() {
        this->typeMap.abort();
    }

private:
    void setToTypeTable(DS_TYPE TYPE, DSType *type);

    void initBuiltinType(DS_TYPE TYPE, const char *typeName, bool extendible, native_type_info_t info);

    void initBuiltinType(DS_TYPE TYPE, const char *typeName, bool extendible,
                         DSType &superType, native_type_info_t info);

    TypeTemplate *initTypeTemplate(const char *typeName,
                                   std::vector<DSType*> &&elementTypes, native_type_info_t info);

    void initErrorType(DS_TYPE TYPE, const char *typeName, DSType &superType);

    void checkElementTypes(const std::vector<DSType *> &elementTypes) const;
    void checkElementTypes(const TypeTemplate &t, const std::vector<DSType *> &elementTypes) const;
    bool asVariantType(const std::vector<DSType *> &elementTypes) const;

    /**
     * add standard dbus error type and alias. must call only once.
     */
    void registerDBusErrorTypes();
};


} // namespace ydsh

#endif //YDSH_TYPE_H
