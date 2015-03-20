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

#ifndef CORE_DSTYPE_H_
#define CORE_DSTYPE_H_

#include <unordered_map>
#include <vector>
#include <string>
#include <utility>
#include <memory>

#include <core/FieldHandle.h>
#include <core/TypeTemplate.h>

struct DSObject;
struct FuncObject;

class DSType {
public:
    DSType();
    virtual ~DSType();

    /**
     * string representation of this type
     */
    virtual std::string getTypeName() const = 0;	// must not reference value

    /**
     * if true, can extend this type
     */
    virtual bool isExtendable() = 0;

    /**
     * get super type of this type.
     * return null, if has no super type(ex. AnyType, VoidType).
     */
    virtual DSType *getSuperType() = 0;

    /**
     * return null, if has no constructor
     */
    virtual FunctionHandle *getConstructorHandle(TypePool *typePool) = 0;

    /**
     * get size of the all fields(include superType fieldSize).
     */
    virtual unsigned int getFieldSize() = 0;

    /**
     * return null, if has no field
     */
    virtual FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName) = 0;

    /**
     * equivalent to dynamic_cast<FunctionHandle*>(lookupFieldHandle())
     */
    FunctionHandle *lookupMethodHandle(TypePool *typePool, const std::string &funcName);

    /**
     * return null if handle not found.
     * not directly use it
     */
    virtual FieldHandle *findHandle(const std::string &fieldName) = 0;

    /**
     * check equality
     */
    virtual bool equals(DSType *targetType) = 0;

    /**
     * check inheritance of target type.
     * if this type is equivalent to target type or
     * the super type of target type, return true.
     */
    virtual bool isAssignableFrom(DSType *targetType);
};

/**
 * base class for ClassType, BuiltinType.
 */
class BaseType: public DSType {
protected:
    std::string typeName;
    bool extendable;

    /**
     * may be null if type is AnyType or VoidType.
     */
    DSType *superType;

public:
    BaseType(std::string &&typeName, bool extendable, DSType *superType);
    virtual ~BaseType();

    virtual std::string getTypeName() const; // override
    bool isExtendable(); // override
    DSType *getSuperType(); // override
    virtual bool equals(DSType *targetType); // override
};

class ClassType: public BaseType {	//TODO: add field index map
private:
    /**
     * handleTable base index
     */
    const int baseIndex;

    /**
     * may be null, if has no constructor.
     */
    FunctionHandle *constructorHandle;

    std::unordered_map<std::string, FieldHandle*> handleMap;

    std::vector<std::shared_ptr<DSObject*>> fieldTable;

public:
    ClassType(std::string &&className, bool extendable, DSType *superType);
    ~ClassType();

    FunctionHandle *getConstructorHandle(TypePool *typePool);	// override
    unsigned int getFieldSize();	// override
    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName);	// override
    FieldHandle *findHandle(const std::string &fieldName);  // override

    /**
     * return false, found duplicated field.
     */
    bool addNewFieldHandle(const std::string &fieldName, bool readOnly, DSType *fieldType);

    /**
     * return created function handle.
     * return null, found duplicated field.
     */
    FunctionHandle *addNewFunctionHandle(const std::string &funcName, DSType *returnType, const std::vector<DSType*> &paramTypes);

    /**
     * return created constructor handle
     */
    FunctionHandle *setNewConstructorHandle(const std::vector<DSType*> &paramTypes);

    /**
     * add function entity to ClassType. the order of calling this method must be
     *  equivalent to addNewFieldHandle or addNewFunctionHandle.
     *  func is null if reserve field
     */
    void addFunction(FuncObject *func);

    /**
     * set constructor entity to ClassType.
     */
    void setConstructor(FuncObject *func);
};

class FunctionType: public DSType {
private:
    /**
     * always BaseFuncType.
     */
    DSType *superType;

    DSType *returnType;

    /**
     * may be empty vector, if has no parameter
     */
    std::vector<DSType*> paramTypes;

public:
    FunctionType(DSType *superType, DSType *returnType, const std::vector<DSType*> &paramTypes);
    ~FunctionType();

    DSType *getReturnType();

    /**
     * may be empty vector, if has no parameter (getParamSize() == 0)
     */
    const std::vector<DSType*> &getParamTypes();

    /**
     * may be null, if has no parameter
     */
    DSType *getFirstParamType();

    /**
     * equivalent to this->getFirstParamType()->isAssignableFrom(targetType)
     */
    bool treatAsMethod(DSType *targetType);

    std::string getTypeName() const;	// override
    bool isExtendable();	// override

    /**
     * return always BaseFuncType
     */
    DSType *getSuperType();	// override

    /**
     * return always null
     */
    FunctionHandle *getConstructorHandle(TypePool *typePool);	// override

    unsigned int getFieldSize();	// override

    /**
     * lookup from super type
     */
    FieldHandle *lookupFieldHandle(TypePool *typePool, const std::string &fieldName);	// override

    FieldHandle *findHandle(const std::string &fieldName);  // override

    bool equals(DSType *targetType);	// override
};

/**
 * create reified type name
 * equivalent to toReifiedTypeName(typeTemplate->getName(), elementTypes)
 */
std::string toReifiedTypeName(TypeTemplate *typeTemplate, const std::vector<DSType*> &elementTypes);

std::string toReifiedTypeName(const std::string &name, const std::vector<DSType*> &elementTypes);

std::string toTupleTypeName(const std::vector<DSType*> &elementTypes);

/**
 * create function type name
 */
std::string toFunctionTypeName(DSType *returnType, const std::vector<DSType*> &paramTypes);

/**
 * for BuiltinType creation.
 */
DSType *newBuiltinType(std::string &&typeName, bool extendable,
        DSType *superType, native_type_info_t *info);

/**
 * for ReifiedType creation.
 * reified type is not public class.
 */
DSType *newReifiedType(TypeTemplate *t, DSType *superType, const std::vector<DSType*> &elementTypes);

/**
 * for TupleType creation
 */
DSType *newTupleType(DSType *superType, const std::vector<DSType*> &elementTypes);

#endif /* CORE_DSTYPE_H_ */
