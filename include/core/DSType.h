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

struct native_type_info_t;
struct DSObject;
struct FuncObject;

typedef unsigned short type_id_t;

class DSType {
protected:
    const type_id_t id;
    flag8_set_t attributeSet;

public:
    const static flag8_t EXTENDABLE = 1 << 0;
    const static flag8_t VOID_TYPE  = 1 << 1;
    const static flag8_t FUNC_TYPE  = 1 << 2;

    DSType(type_id_t id, bool extendable, bool isVoid = false);
    virtual ~DSType();

    /**
     * get unique type id.
     */
    type_id_t getTypeId() const;

    /**
     * if true, can extend this type
     */
    bool isExtendable() const;

    /**
     * if this type is VoidType, return true.
     */
    bool isVoidType() const;

    /**
     * if this type is FunctionType, return true.
     */
    bool isFuncType() const;

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

    bool operator==(const DSType &type);

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
    /**
     * may be null if type is AnyType or VoidType.
     */
    DSType *superType;

public:
    BaseType(type_id_t id, bool extendable, DSType *superType, bool isVoid);
    virtual ~BaseType();

    DSType *getSuperType(); // override
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
    ClassType(type_id_t id, bool extendable, DSType *superType);
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
    FunctionType(type_id_t id, DSType *superType, DSType *returnType, const std::vector<DSType*> &paramTypes);
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
};

/**
 * for BuiltinType creation.
 */
DSType *newBuiltinType(type_id_t id, bool extendable,
        DSType *superType, native_type_info_t *info, bool isVoid = false);

/**
 * for ReifiedType creation.
 * reified type is not public class.
 */
DSType *newReifiedType(type_id_t id, native_type_info_t *info,
        DSType *superType, const std::vector<DSType*> &elementTypes);

/**
 * for TupleType creation
 */
DSType *newTupleType(type_id_t id, DSType *superType, const std::vector<DSType*> &elementTypes);

#endif /* CORE_DSTYPE_H_ */
