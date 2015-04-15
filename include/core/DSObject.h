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

namespace ydsh {
namespace ast {

class FunctionNode;

}
};

namespace ydsh {
namespace  core {

using namespace ydsh::ast;

struct RuntimeContext;
struct String_Object;

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
    virtual std::string toString(RuntimeContext &ctx);

    /**
     * EQ method implementation.
     */
    virtual bool equals(const std::shared_ptr<DSObject> &obj);

    /**
     * STR method implementation.
     */
    std::shared_ptr<String_Object> str(RuntimeContext &ctx);

    /**
     * for interpolation
     */
    virtual std::shared_ptr<String_Object> interp(RuntimeContext &ctx);

    /**
     * for command argument.
     */
    virtual std::shared_ptr<DSObject> commandArg(RuntimeContext &ctx);

    /**
     * for Map_Object
     */
    virtual size_t hash();
};

struct Int_Object : public DSObject {
    int value;

    Int_Object(DSType *type, int value);

    int getValue();

    std::string toString(RuntimeContext &ctx); // override
    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
};

struct Long_Object : public DSObject {
    long value;

    Long_Object(DSType *type, long value);

    long getValue();

    std::string toString(RuntimeContext &ctx); // override
    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
};

struct Float_Object : public DSObject {
    double value;

    Float_Object(DSType *type, double value);

    double getValue();

    std::string toString(RuntimeContext &ctx); // override
    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
};

struct Boolean_Object : public DSObject {
    bool value;

    Boolean_Object(DSType *type, bool value);

    bool getValue();

    std::string toString(RuntimeContext &ctx); // override
    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
};

struct String_Object : public DSObject {
    std::string value;

    String_Object(DSType *type, std::string &&value);

    String_Object(DSType *type, const std::string &value);

    String_Object(DSType *type);

    const std::string &getValue();

    std::string toString(RuntimeContext &ctx); // override
    void append(const String_Object &obj);

    void append(const std::shared_ptr<String_Object> &obj);

    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
};

struct Array_Object : public DSObject {
    unsigned int curIndex;
    std::vector<std::shared_ptr<DSObject>> values;

    Array_Object(DSType *type);

    const std::vector<std::shared_ptr<DSObject>> &getValues();

    std::string toString(RuntimeContext &ctx); // override
    void append(std::shared_ptr<DSObject> obj);

    std::shared_ptr<String_Object> interp(RuntimeContext &ctx); // override
    std::shared_ptr<DSObject> commandArg(RuntimeContext &ctx); // override
};

struct KeyCompare {
    bool operator() (const std::shared_ptr<DSObject> &x,
                     const std::shared_ptr<DSObject> &y) const;
};

struct GenHash {
    std::size_t operator() (const std::shared_ptr<DSObject> &key) const;
};

typedef std::unordered_map<std::shared_ptr<DSObject>, std::shared_ptr<DSObject>, GenHash, KeyCompare> HashMap;

struct Map_Object : public DSObject {
    HashMap valueMap;

    Map_Object(DSType *type);

    const HashMap &getValueMap();

    void set(const std::shared_ptr<DSObject> &key, const std::shared_ptr<DSObject> &value);

    /**
     * for Map_Object creation.
     */
    void add(const std::shared_ptr<DSObject> &value, const std::shared_ptr<DSObject> &key);

    std::string toString(RuntimeContext &ctx); // override
};

struct Tuple_Object : public DSObject {
    Tuple_Object(DSType *type);

    std::string toString(RuntimeContext &ctx); // override
    unsigned int getActualIndex(unsigned int elementIndex);
    unsigned int getElementSize();

    void set(unsigned int elementIndex, const std::shared_ptr<DSObject> &obj);

    const std::shared_ptr<DSObject> &get(unsigned int elementIndex);

    std::shared_ptr<String_Object> interp(RuntimeContext &ctx); // override
    std::shared_ptr<DSObject> commandArg(RuntimeContext &ctx); // override
};

struct Error_Object : public DSObject {
    std::shared_ptr<DSObject> message;
    std::vector<std::string> stackTrace;

    Error_Object(DSType *type, const std::shared_ptr<DSObject> &message);
    Error_Object(DSType *type, std::shared_ptr<DSObject> &&message);
    ~Error_Object();

    std::string toString(RuntimeContext &ctx); // override
    void createStackTrace(RuntimeContext &ctx);

    /**
     * print stack trace to stderr
     */
    void printStackTrace(RuntimeContext &ctx);

    /**
     * create new Error_Object and create stack trace
     */
    static Error_Object *newError(RuntimeContext &ctx, DSType *type,
                                  const std::shared_ptr<DSObject> &message);

    static Error_Object *newError(RuntimeContext &ctx, DSType *type,
                                  std::shared_ptr<DSObject> &&message);
};

struct DummyObject : public DSObject {
    DummyObject() : DSObject(0) {
    }

    ~DummyObject() {
    }

    void setType(DSType *type) { // override.
        this->type = type;
    }
};

struct FuncObject : public DSObject {
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
struct UserFuncObject : public FuncObject {
    FunctionNode *funcNode;

    UserFuncObject(FunctionNode *funcNode);

    ~UserFuncObject();

    FunctionNode *getFuncNode();

    std::string toString(RuntimeContext &ctx); // override
    bool invoke(RuntimeContext &ctx); // override
};

/**
 * for builtin(native) function
 */
struct BuiltinFuncObject : public FuncObject {
    /**
     * bool func(RuntimeContext &ctx)
     */
    native_func_t func_ptr;

    BuiltinFuncObject(native_func_t func_ptr);

    ~BuiltinFuncObject();

    int getParamSize();

    native_func_t getFuncPointer();

    std::string toString(RuntimeContext &ctx); // override
    bool invoke(RuntimeContext &ctx); // override

    /**
     * for builtin func obejct creation
     */
    static std::shared_ptr<DSObject> newFuncObject(native_func_t func_ptr);
};

} // namespace core
} // namespace ydsh


// helper macro for object manipulation
/**
 * get raw pointer from shared_ptr and cast it.
 */
#ifndef NDEBUG
#define TYPE_AS(t, s_obj) dynamic_cast<t*>((s_obj).get())
#else
#define TYPE_AS(t, s_obj) ((t*) (s_obj).get())
#endif

#endif /* CORE_DSOBJECT_H_ */
