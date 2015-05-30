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

#include <memory>
#include "../ast/Node.h"
#include "DSType.h"

namespace ydsh {
namespace  core {

using namespace ydsh::ast;

class RuntimeContext;
class String_Object;
struct ObjectVisitor;

class DSObject {
protected:
    DSType *type;

public:
    explicit DSObject(DSType *type);
    virtual ~DSObject() = default;

    /**
     * get object type
     */
    DSType *getType();

    /**
     * for FuncObject.
     */
    virtual void setType(DSType *type);

    virtual std::shared_ptr<DSObject> *getFieldTable();

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

    /**
     * check if this type is instance of targetType.
     */
    virtual bool introspect(RuntimeContext &ctx, DSType *targetType);

    virtual void accept(ObjectVisitor *visitor);
};

class Int_Object : public DSObject {
private:
    int value;

public:
    Int_Object(DSType *type, int value);
    ~Int_Object() = default;

    int getValue();

    std::string toString(RuntimeContext &ctx); // override
    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
    void accept(ObjectVisitor *visitor); // override
};

class Long_Object : public DSObject {
private:
    long value;

public:
    Long_Object(DSType *type, long value);
    ~Long_Object() = default;

    long getValue();

    std::string toString(RuntimeContext &ctx); // override
    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
    void accept(ObjectVisitor *visitor); // override
};

class Float_Object : public DSObject {
private:
    double value;

public:
    Float_Object(DSType *type, double value);
    ~Float_Object() = default;

    double getValue();

    std::string toString(RuntimeContext &ctx); // override
    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
    void accept(ObjectVisitor *visitor); // override
};

class Boolean_Object : public DSObject {
private:
    bool value;

public:
    Boolean_Object(DSType *type, bool value);
    ~Boolean_Object() = default;

    bool getValue();

    std::string toString(RuntimeContext &ctx); // override
    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
    void accept(ObjectVisitor *visitor); // override
};

class String_Object : public DSObject {
private:
    std::string value;

public:
    String_Object(DSType *type, std::string &&value);

    String_Object(DSType *type, const std::string &value);

    explicit String_Object(DSType *type);
    ~String_Object() = default;

    const std::string &getValue();

    std::string toString(RuntimeContext &ctx); // override

    bool equals(const std::shared_ptr<DSObject> &obj);  // override
    size_t hash();  // override
    void accept(ObjectVisitor *visitor); // override
};

class Array_Object : public DSObject {
private:
    unsigned int curIndex;
    std::vector<std::shared_ptr<DSObject>> values;

public:
    explicit Array_Object(DSType *type);
    Array_Object(DSType *type, std::vector<std::shared_ptr<DSObject>> &&values);
    ~Array_Object() = default;

    const std::vector<std::shared_ptr<DSObject>> &getValues();

    std::string toString(RuntimeContext &ctx); // override
    void append(std::shared_ptr<DSObject> &&obj);
    void append(const std::shared_ptr<DSObject> &obj);
    void set(unsigned int index, const std::shared_ptr<DSObject> &obj);

    void initIterator();
    const std::shared_ptr<DSObject> &nextElement();
    bool hasNext();

    std::shared_ptr<String_Object> interp(RuntimeContext &ctx); // override
    std::shared_ptr<DSObject> commandArg(RuntimeContext &ctx); // override
    void accept(ObjectVisitor *visitor); // override
};

struct KeyCompare {
    bool operator() (const std::shared_ptr<DSObject> &x,
                     const std::shared_ptr<DSObject> &y) const;
};

struct GenHash {
    std::size_t operator() (const std::shared_ptr<DSObject> &key) const;
};

typedef std::unordered_map<std::shared_ptr<DSObject>, std::shared_ptr<DSObject>, GenHash, KeyCompare> HashMap;

class Map_Object : public DSObject {
private:
    HashMap valueMap;
    HashMap::const_iterator iter;

public:
    explicit Map_Object(DSType *type);
    ~Map_Object() = default;

    const HashMap &getValueMap();

    void set(const std::shared_ptr<DSObject> &key, const std::shared_ptr<DSObject> &value);
    void add(std::pair<std::shared_ptr<DSObject>, std::shared_ptr<DSObject>> &&entry);
    void initIterator();
    std::shared_ptr<DSObject> nextElement(RuntimeContext &ctx);
    bool hasNext();

    std::string toString(RuntimeContext &ctx); // override
    void accept(ObjectVisitor *visitor); // override
};

class BaseObject : public DSObject {
protected:
    std::shared_ptr<DSObject> *fieldTable;

public:
    explicit BaseObject(DSType *type);
    virtual ~BaseObject();

    std::shared_ptr<DSObject> *getFieldTable(); // override
};

struct Tuple_Object : public BaseObject {
    explicit Tuple_Object(DSType *type);
    ~Tuple_Object() = default;

    std::string toString(RuntimeContext &ctx); // override
    unsigned int getElementSize();

    void set(unsigned int elementIndex, const std::shared_ptr<DSObject> &obj);

    const std::shared_ptr<DSObject> &get(unsigned int elementIndex);

    std::shared_ptr<String_Object> interp(RuntimeContext &ctx); // override
    std::shared_ptr<DSObject> commandArg(RuntimeContext &ctx); // override
    void accept(ObjectVisitor *visitor); // override
};

class Error_Object : public DSObject {
private:
    std::shared_ptr<DSObject> message;
    std::shared_ptr<DSObject> name;
    std::vector<std::string> stackTrace;

public:
    Error_Object(DSType *type, const std::shared_ptr<DSObject> &message);
    Error_Object(DSType *type, std::shared_ptr<DSObject> &&message);
    ~Error_Object() = default;

    std::string toString(RuntimeContext &ctx); // override

    const std::shared_ptr<DSObject> &getMessage();

    /**
     * print stack trace to stderr
     */
    void printStackTrace(RuntimeContext &ctx);

    const std::shared_ptr<DSObject> &getName(RuntimeContext &ctx);

    void accept(ObjectVisitor *visitor); // override

    /**
     * create new Error_Object and create stack trace
     */
    static Error_Object *newError(RuntimeContext &ctx, DSType *type,
                                  const std::shared_ptr<DSObject> &message);

    static Error_Object *newError(RuntimeContext &ctx, DSType *type,
                                  std::shared_ptr<DSObject> &&message);

private:
    void createStackTrace(RuntimeContext &ctx);
};

struct DummyObject : public DSObject {
    DummyObject() : DSObject(0) {
    }

    ~DummyObject() = default;

    void setType(DSType *type) { // override.
        this->type = type;
    }
};

struct FuncObject : public DSObject {
    FuncObject();

    virtual ~FuncObject() = default;

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
class UserFuncObject : public FuncObject {
private:
    FunctionNode *funcNode;

public:
    explicit UserFuncObject(FunctionNode *funcNode);

    ~UserFuncObject();

    FunctionNode *getFuncNode();

    std::string toString(RuntimeContext &ctx); // override
    bool invoke(RuntimeContext &ctx); // override
    void accept(ObjectVisitor *visitor); // override
};

/**
 * for builtin(native) function
 */
class BuiltinFuncObject : public FuncObject {
private:
    /**
     * bool func(RuntimeContext &ctx)
     */
    native_func_t func_ptr;

public:
    explicit BuiltinFuncObject(native_func_t func_ptr);

    ~BuiltinFuncObject() = default;

    native_func_t getFuncPointer();

    std::string toString(RuntimeContext &ctx); // override
    bool invoke(RuntimeContext &ctx); // override

    /**
     * for builtin func obejct creation
     */
    static std::shared_ptr<DSObject> newFuncObject(native_func_t func_ptr);
    void accept(ObjectVisitor *visitor); // override
};

/**
 * reference of method. for method call, constructor call.
 */
struct MethodRef {
    MethodRef() = default;
    virtual ~MethodRef() = default;

    virtual bool invoke(RuntimeContext &ctx) = 0;
};

class NativeMethodRef : public MethodRef {
private:
    native_func_t func_ptr;

public:
    explicit NativeMethodRef(native_func_t func_ptr);
    ~NativeMethodRef() = default;

    bool invoke(RuntimeContext &ctx);   // override
};

struct ProxyObject : public DSObject {
    explicit ProxyObject(DSType *type) : DSObject(type) {
    }

    virtual ~ProxyObject() = default;

    /**
     * invoke method and set return value.
     */
    virtual bool invokeMethod(RuntimeContext &ctx, const std::string &methodName, MethodHandle *handle) = 0;

    /**
     * push got value to stack top.
     * return false, if error happened.
     */
    virtual bool invokeGetter(RuntimeContext &ctx, DSType *recvType,
                              const std::string &fieldName, DSType *fieldType) = 0;

    /**
     * pop stack top value and set to field.
     * return false, if error happened.
     */
    virtual bool invokeSetter(RuntimeContext &ctx,DSType *recvType,
                              const std::string &fieldName, DSType *fieldType) = 0;
};

struct DBus_Object : public DSObject {
    explicit DBus_Object(TypePool *typePool);
    virtual ~DBus_Object() = default;

    /**
     * init and get Bus_ObjectImpl representing for system bus.
     * return false, if error happened
    */
    virtual bool getSystemBus(RuntimeContext &ctx);

    /**
     * init and get Bus_ObjectImpl representing for session bus.
     * return false, if error happened
     */
    virtual bool getSessionBus(RuntimeContext &ctx);

    virtual bool waitSignal(RuntimeContext &ctx);

    bool supportDBus();

    static DBus_Object *newDBus_Object(TypePool *typePool);
};

struct Bus_Object : public DSObject {
    explicit Bus_Object(DSType *type);
    virtual ~Bus_Object() = default;

    virtual bool service(RuntimeContext &ctx, std::string &&serviceName);
    virtual bool listNames(RuntimeContext &ctx, bool activeName);
};

struct Service_Object : public DSObject {
    explicit Service_Object(DSType *type);
    virtual ~Service_Object() = default;

    virtual bool object(RuntimeContext &ctx, std::string &&objectPath);
};

struct ObjectVisitor {
    virtual ~ObjectVisitor() = default;

    virtual void visitDefault(DSObject *obj) = 0;
    virtual void visitInt_Object(Int_Object *obj) = 0;
    virtual void visitLong_Object(Long_Object *obj) = 0;
    virtual void visitFloat_Object(Float_Object *obj) = 0;
    virtual void visitBoolean_Object(Boolean_Object *obj) = 0;
    virtual void visitString_Object(String_Object *obj) = 0;
    virtual void visitArray_Object(Array_Object *obj) = 0;
    virtual void visitMap_Object(Map_Object *obj) = 0;
    virtual void visitTuple_Object(Tuple_Object *obj) = 0;
    virtual void visitError_Object(Error_Object *obj) = 0;
    virtual void visitUserFuncObject(UserFuncObject *obj) = 0;
    virtual void visitBuiltinFuncObject(BuiltinFuncObject *obj) = 0;
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
#define TYPE_AS(t, s_obj) static_cast<t*>((s_obj).get())
#endif

#endif /* CORE_DSOBJECT_H_ */
