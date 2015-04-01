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
    DSType *type;

    /**
     * contains all of field (also method)
     */
    std::shared_ptr<DSObject> *fieldTable;

    DSObject(DSType *type);
    virtual ~DSObject();

    /**
     * get object type
     */
    DSType *getType();

    /**
     * for FuncObject.
     */
    virtual void setType(DSType *type);

    /**
     * for printing
     */
    virtual std::string toString() = 0;
};

struct Int_Object: public DSObject {
    int value;

    Int_Object(DSType *type, int value);

    int getValue();
    std::string toString(); // override
};

struct Float_Object: public DSObject {
    double value;

    Float_Object(DSType *type, double value);

    double getValue();
    std::string toString(); // override
};

struct Boolean_Object: public DSObject {
    bool value;

    Boolean_Object(DSType *type, bool value);

    bool getValue();
    std::string toString(); // override
};

struct String_Object: public DSObject {
    std::string value;

    String_Object(DSType *type, std::string &&value);

    const std::string &getValue();
    std::string toString(); // override
    void append(const String_Object &obj);
};

struct Array_Object: public DSObject {
    std::vector<std::shared_ptr<DSObject>> values;

    Array_Object(DSType *type);

    const std::vector<std::shared_ptr<DSObject>> &getValues();
    std::string toString(); // override
    void append(std::shared_ptr<DSObject> obj);
};

struct Tuple_Object : public DSObject {
    Tuple_Object(DSType *type);

    std::string toString(); // override
    unsigned int getActualIndex(unsigned int elementIndex);
    void set(unsigned int elementIndex, const std::shared_ptr<DSObject> &obj);
    const std::shared_ptr<DSObject> &get(unsigned int elementIndex);
};

struct FuncObject: public DSObject {
    FuncObject();
    virtual ~FuncObject();

    void setType(DSType *type); // override

    /**
     * equivalent to dynamic_cast<FunctionType*>(getType())
     * may be null, before call setType()
     */
    FunctionType *getFuncType();

    /**
     * invoke function.
     * return true, if invocation success.
     * return false, if thrown exception.
     */
    virtual bool invoke(RuntimeContext &ctx) = 0;
};

/*
 * for user defined function
 */
struct UserFuncObject: public FuncObject {
    FunctionNode *funcNode;

    UserFuncObject(FunctionNode *funcNode);
    ~UserFuncObject();

    FunctionNode *getFuncNode();
    std::string toString(); // override
    bool invoke(RuntimeContext &ctx); // override
};

/**
 * for builtin(native) function
 */
struct BuiltinFuncObject: public FuncObject {
    /**
     * DSObject *func(RuntimeContext *ctx)
     */
    void *func_ptr;

    BuiltinFuncObject(void *func_ptr);
    ~BuiltinFuncObject();

    int getParamSize();
    void *getFuncPointer();
    std::string toString(); // override
    bool invoke(RuntimeContext &ctx); // override

    static /**
     * for builtin func obejct creation
     */
    std::shared_ptr<DSObject> newFuncObject(void *func_ptr);
};


// helper macro for object manipulation
/**
 * get raw pointer from shared_ptr and cast it.
 */
#define TYPE_AS(t, s_obj) dynamic_cast<t*>(s_obj.get())

#endif /* CORE_DSOBJECT_H_ */
