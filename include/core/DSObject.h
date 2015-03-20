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

#ifndef CORE_DSOBJECT_H_
#define CORE_DSOBJECT_H_

#include <core/DSType.h>
#include <memory>

class FunctionNode;
struct RuntimeContext;

struct DSObject {
    DSObject();
    virtual ~DSObject();

    /**
     * get object type
     */
    virtual DSType *getType() = 0;

    /**
     * return 0, if has no field
     */
    virtual int getFieldSize() = 0;

    /**
     * for field(or method) lookup
     * fieldIndex > -1 && fieldIndex < getFieldSize()
     * this method is not type-safe.
     */
    virtual DSObject *lookupField(int fieldIndex) = 0;

    /**
     * for printing
     */
    virtual std::string toString() = 0;
};

struct BaseObject: public DSObject {
    DSType *type;

    int fieldSize;

    /**
     * may be null, if has no field. (fieldSize == 0)
     */
    DSObject **fieldTable;

    BaseObject(DSType *type);
    virtual ~BaseObject();

    DSType *getType();	// override
    int getFieldSize();	// override
    DSObject *lookupField(int fieldIndex);	// override
};

struct Int_Object: public BaseObject {
    int value;

    Int_Object(DSType *type, int value);

    int getValue();
    std::string toString(); // override
};

struct Float_Object: public BaseObject {
    double value;

    Float_Object(DSType *type, double value);

    double getValue();
    std::string toString(); // override
};

struct Boolean_Object: public BaseObject {
    bool value;

    Boolean_Object(DSType *type, bool value);

    bool getValue();
    std::string toString(); // override
};

struct String_Object: public BaseObject {
    std::string value;

    String_Object(DSType *type, std::string &&value);

    const std::string &getValue();
    std::string toString(); // override
    void append(const String_Object &obj);
};

struct Array_Object: public BaseObject {
    std::vector<std::shared_ptr<DSObject>> values;

    Array_Object(DSType *type);

    const std::vector<std::shared_ptr<DSObject>> &getValues();
    std::string toString(); // override
    void append(std::shared_ptr<DSObject> obj);
};

struct Tuple_Object : public BaseObject {
    std::vector<std::shared_ptr<DSObject>> values;

    Tuple_Object(DSType *type, unsigned int size);

    const std::vector<std::shared_ptr<DSObject>> &getValues();
    std::string toString(); // override
    void set(unsigned int index, std::shared_ptr<DSObject> obj);
};

struct FuncObject: public DSObject {
    /**
     * may be null, but finally must be not null
     */
    FunctionType *funcType;

    FuncObject(FunctionType *funcType);
    virtual ~FuncObject();

    DSType *getType();	// override

    /**
     * return always 0
     */
    int getFieldSize();	// override

    /**
     * return always null
     */
    DSObject *lookupField(int fieldIndex);	// override

    /**
     * equivalent to dynamic_cast<FunctionType*>(getType())
     */
    FunctionType *getFuncType();
};

/*
 * for user defined function
 */
struct UserFuncObject: public FuncObject {
    FunctionNode *funcNode;

    UserFuncObject(FunctionType *funcType, FunctionNode *funcNode);
    ~UserFuncObject();

    FunctionNode *getFuncNode();
    std::string toString(); // override
};

/**
 * for builtin(native) function
 */
struct BuiltinFuncObject: public FuncObject {
    /**
     * size of actual parameter. exclude first parameter(RuntimeContext)
     */
    int paramSize;

    /**
     * DSObject *func(RuntimeContext *ctx, DSObject *arg1, DSObject *arg2, ....)
     */
    void *func_ptr;

    BuiltinFuncObject(FunctionType *funcType, int paramSize, void *func_ptr);
    ~BuiltinFuncObject();

    int getParamSize();
    void *getFuncPointer();
    std::string toString(); // override
};


// helper macro for object manipulation
/**
 * get raw pointer from shared_ptr and cast it.
 */
#define TYPE_AS(t, s_obj) dynamic_cast<t*>(s_obj.get())

#endif /* CORE_DSOBJECT_H_ */
