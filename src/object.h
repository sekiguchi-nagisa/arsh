/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_OBJECT_H
#define YDSH_OBJECT_H

#include <ostream>
#include <memory>
#include <iostream>
#include <unordered_set>
#include <tuple>

#include "type.h"
#include <config.h>
#include "misc/demangle.hpp"
#include "misc/buffer.hpp"
#include "lexer.h"
#include "opcode.h"

namespace ydsh {

class FunctionNode;
class DSValue;
class String_Object;
struct ObjectVisitor;

typedef std::unordered_set<unsigned long> VisitedSet;

class DSObject {
protected:
    DSType *type;
    unsigned int refCount;

    friend class DSValue;

public:
    NON_COPYABLE(DSObject);

    explicit DSObject(DSType &type) : type(&type), refCount(0) { }
    explicit DSObject(DSType *type) : type(type), refCount(0) { }

    virtual ~DSObject() = default;

    /**
     * get object type
     */
    DSType *getType() const {
        return this->type;
    }

    unsigned int getRefcount() const {
        return this->refCount;
    }

    /**
     * for FuncObject.
     */
    virtual void setType(DSType *type);

    virtual DSValue *getFieldTable();

    /**
     * for printing
     */
    virtual std::string toString(DSState &ctx, VisitedSet *visitedSet);

    /**
     * EQ method implementation.
     */
    virtual bool equals(const DSValue &obj);

    /**
     * STR method implementation.
     * return String_Object
     */
    DSValue str(DSState &ctx);

    /**
     * for interpolation
     * return String_Object
     */
    virtual DSValue interp(DSState &ctx, VisitedSet *visitedSet);

    /**
     * for command argument.
     */
    virtual DSValue commandArg(DSState &ctx, VisitedSet *visitedSet);

    /**
     * for Map_Object
     */
    virtual size_t hash();

    /**
     * check if this type is instance of targetType.
     */
    virtual bool introspect(DSState &ctx, DSType *targetType);
};

enum class DSValueKind : unsigned char {
    OBJECT = 0,
    NUMBER = 129,
};

class DSValue {
private:
    union {
        /**
         * may be null
         */
        DSObject *obj;

        /**
         * if most significant bit is 0, represents DSObject' pointer(may be nullptr).
         * otherwise, represents native pointer, number ... etc
         */
        long val;
    };

public:
    /**
     * obj may be null
     */
    explicit DSValue(DSObject *obj) noexcept : DSValue(reinterpret_cast<long>(obj)) { }

    explicit DSValue(long val) noexcept : val(val) {
        if(this->val > 0) {
            this->obj->refCount++;
        }
    }

    /**
     * equivalent to DSValue(nullptr)
     */
    constexpr DSValue() noexcept: obj(nullptr) { }

    constexpr DSValue(std::nullptr_t) noexcept: obj(nullptr) { }

    DSValue(const DSValue &value) noexcept : DSValue(value.obj) { }

    /**
     * not increment refCount
     */
    DSValue(DSValue &&value) noexcept : obj(value.obj) { value.obj = nullptr; }

    ~DSValue() {
        if(this->val > 0) {
            if(--this->obj->refCount == 0) {
                delete this->obj;
            }
            this->obj = nullptr;
        }
    }

    DSValue &operator=(const DSValue &value) noexcept {
        DSValue tmp(value);
        this->swap(tmp);
        return *this;
    }

    DSValue &operator=(DSValue &&value) noexcept {
        DSValue tmp(std::move(value));
        this->swap(tmp);
        return *this;
    }

    /**
     * release current pointer.
     */
    void reset() noexcept {
        DSValue tmp;
        this->swap(tmp);
    }

    /**
     * mask tag and get actual value.
     */
    long value() const noexcept {
        return this->val & 0xFFFFFFFFFFFFFF;
    }

    DSValueKind kind() const noexcept {
        return static_cast<DSValueKind>((this->val & 0xFF00000000000000) >> 56);
    }

    DSObject *get() const noexcept {
        return this->obj;
    }

    DSObject &operator*() const noexcept {
        return *this->obj;
    }

    DSObject *operator->() const noexcept {
        return this->obj;
    }

    explicit operator bool() const noexcept {
        return this->obj != nullptr;
    }

    /**
     * if represents DSObject(may be nullptr), return true.
     */
    bool isObject() const noexcept {
        return this->val >= 0;
    }

    void swap(DSValue &value) noexcept {
        std::swap(this->obj, value.obj);
    }

    template <typename T, typename ...A>
    static DSValue create(A &&...args) {
        static_assert(std::is_base_of<DSObject, T>::value, "must be subtype of DSObject");

        return DSValue(new T(std::forward<A>(args)...));
    }

    static DSValue createNum(unsigned int v) {
        unsigned long mask = static_cast<unsigned long>(DSValueKind::NUMBER) << 56;
        return DSValue(mask | v);
    }
};

template <typename T>
inline T *typeAs(const DSValue &value) noexcept {
    static_assert(std::is_base_of<DSObject, T>::value, "must be subtype of DSObject");

#ifdef USE_SAFE_CAST
    constexpr bool useSafeCast = true;
#else
    constexpr bool useSafeCast = false;
#endif

    if(useSafeCast) {
        if(!value.isObject()) {
            std::cerr << "must be represent DSObject" << std::endl;
            abort();
        }
        auto *r = dynamic_cast<T*>(value.get());
        if(r == nullptr) {
            DSObject &v = *value;
            std::cerr << "target type is: " << Demangle()(typeid(T))
            << ", but actual is: " << Demangle()(typeid(v)) << std::endl;
            abort();
        }
        return r;
    } else {
        return static_cast<T*>(value.get());
    }
}

class Int_Object : public DSObject {
private:
    int value;

public:
    Int_Object(DSType &type, int value) : DSObject(type), value(value) { }

    ~Int_Object() = default;

    int getValue() const {
        return this->value;
    }

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
    bool equals(const DSValue &obj) override;
    size_t hash() override;
};

class Long_Object : public DSObject {
private:
    long value;

public:
    Long_Object(DSType &type, long value) : DSObject(type), value(value) { }

    ~Long_Object() = default;

    long getValue() const {
        return this->value;
    }

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
    bool equals(const DSValue &obj) override;
    size_t hash() override;
};

class Float_Object : public DSObject {
private:
    double value;

public:
    Float_Object(DSType &type, double value) : DSObject(type), value(value) { }

    ~Float_Object() = default;

    double getValue() const {
        return this->value;
    }

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
    bool equals(const DSValue &obj) override;
    size_t hash() override;
};

class Boolean_Object : public DSObject {
private:
    bool value;

public:
    Boolean_Object(DSType &type, bool value) : DSObject(type), value(value) { }

    ~Boolean_Object() = default;

    bool getValue() const {
        return this->value;
    }

    std::string toString(DSState &ctx, VisitedSet *visitedSe) override;
    bool equals(const DSValue &obj) override;
    size_t hash() override;
};

class String_Object : public DSObject {
private:
    std::string value;

public:
    explicit String_Object(DSType &type) :
            DSObject(type), value() { }

    String_Object(DSType &type, std::string &&value) :
            DSObject(type), value(std::move(value)) { }

    String_Object(DSType &type, const std::string &value) :
            DSObject(type), value(value) { }

    ~String_Object() = default;

    const char *getValue() const {
        return this->value.c_str();
    }

    /**
     * equivalent to strlen(this->getValue())
     */
    unsigned int size() const {
        return this->value.size();
    }

    bool empty() const {
        return this->size() == 0;
    }

    /**
     * for string expression
     */
    void append(DSValue &&obj);

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;

    bool equals(const DSValue &obj) override;
    size_t hash() override;
};

struct StringIter_Object : public DSObject {
    unsigned int curIndex;
    DSValue strObj;

    StringIter_Object(DSType &type, String_Object *str) :
            DSObject(type), curIndex(0), strObj(DSValue(str)) { }
};

class Array_Object : public DSObject {
private:
    unsigned int curIndex;
    std::vector<DSValue> values;

public:
    explicit Array_Object(DSType &type) : DSObject(type), curIndex(0), values() { }

    Array_Object(DSType &type, std::vector<DSValue> &&values) :
            DSObject(type), curIndex(0), values(std::move(values)) { }

    ~Array_Object() = default;

    const std::vector<DSValue> &getValues() const {
        return this->values;
    }

    std::vector<DSValue> &refValues() {
        return this->values;
    }

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
    void append(DSValue &&obj);
    void append(const DSValue &obj);
    void set(unsigned int index, const DSValue &obj);

    void initIterator() {
        this->curIndex = 0;
    }

    const DSValue &nextElement();

    bool hasNext() const {
        return this->curIndex < this->values.size();
    }

    DSValue interp(DSState &ctx, VisitedSet *visitedSet) override;
    DSValue commandArg(DSState &ctx, VisitedSet *visitedSet) override;
};

struct KeyCompare {
    bool operator() (const DSValue &x, const DSValue &y) const;
};

struct GenHash {
    std::size_t operator() (const DSValue &key) const;
};

typedef std::unordered_map<DSValue, DSValue, GenHash, KeyCompare> HashMap;

class Map_Object : public DSObject {
private:
    HashMap valueMap;
    HashMap::const_iterator iter;

public:
    explicit Map_Object(DSType &type) : DSObject(type), valueMap() { }

    ~Map_Object() = default;

    const HashMap &getValueMap() const {
        return this->valueMap;
    }

    HashMap &refValueMap() {
        return this->valueMap;
    }

    void set(const DSValue &key, const DSValue &value);
    void add(std::pair<DSValue, DSValue> &&entry);
    void initIterator();
    DSValue nextElement(DSState &ctx);
    bool hasNext();

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
};

class BaseObject : public DSObject {
protected:
    DSValue *fieldTable;

public:
    explicit BaseObject(DSType &type) :
            DSObject(type), fieldTable(new DSValue[type.getFieldSize()]) { }

    virtual ~BaseObject();

    DSValue *getFieldTable() override;
};

struct Tuple_Object : public BaseObject {
    explicit Tuple_Object(DSType &type) : BaseObject(type) { }

    ~Tuple_Object() = default;

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;

    unsigned int getElementSize() const {
        return this->type->getFieldSize();
    }

    void set(unsigned int elementIndex, const DSValue &obj);

    const DSValue &get(unsigned int elementIndex);

    DSValue interp(DSState &ctx, VisitedSet *visitedSet) override;
    DSValue commandArg(DSState &ctx, VisitedSet *visitedSet) override;
};

class StackTraceElement {
private:
    const char *sourceName;
    unsigned int lineNum;
    std::string callerName;

public:
    StackTraceElement() :
            sourceName(), lineNum(0), callerName() { }

    StackTraceElement(const char *sourceName, unsigned int lineNum, std::string &&callerName) :
            sourceName(sourceName), lineNum(lineNum), callerName(std::move(callerName)) { }

    ~StackTraceElement() = default;

    const char *getSourceName() const {
        return this->sourceName;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    const std::string &getCallerName() const {
        return this->callerName;
    }
};

std::ostream &operator<<(std::ostream &stream, const StackTraceElement &e);

/**
 * if stack trace elements is empty, return 0.
 */
unsigned int getOccuredLineNum(const std::vector<StackTraceElement> &elements);


class Error_Object : public DSObject {
private:
    DSValue message;
    DSValue name;
    std::vector<StackTraceElement> stackTrace;

public:
    Error_Object(DSType &type, const DSValue &message) :
            DSObject(type), message(message), name(), stackTrace() { }

    Error_Object(DSType &type, DSValue &&message) :
            DSObject(type), message(std::move(message)), name(), stackTrace() { }

    ~Error_Object() = default;

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;

    const DSValue &getMessage() const {
        return this->message;
    }

    /**
     * print stack trace to stderr
     */
    void printStackTrace(DSState &ctx);

    const DSValue &getName(DSState &ctx);

    const std::vector<StackTraceElement> &getStackTrace() const {
        return this->stackTrace;
    }

    /**
     * create new Error_Object and create stack trace
     */
    static DSValue newError(DSState &ctx, DSType &type, const DSValue &message);

    static DSValue newError(DSState &ctx, DSType &type, DSValue &&message);

private:
    void createStackTrace(DSState &ctx);
};

struct DummyObject : public DSObject {
    DummyObject() : DSObject(nullptr) { }

    ~DummyObject() = default;

    void setType(DSType *type) override {
        this->type = type;
    }
};

enum class CodeKind : unsigned char {
    TOPLEVEL,
    FUNCTION,
    USER_DEFINED_CMD,
    NATIVE,
};

class DSCode {
protected:
    /**
     * +----------------------+-------------------+-------------------------------+
     * | CallableKind (1byte) | code size (4byte) | local variable number (2byte) |
     * +----------------------+-------------------+-------------------------------+
     *
     * if indicate toplevel
     *
     * +----------------------+-------------------+-------------------------------+--------------------------------+
     * | CallableKind (1byte) | code size (4byte) | local variable number (2byte) | global variable number (2byte) |
     * +----------------------+-------------------+-------------------------------+--------------------------------+
     *
     * if indicate native
     *
     * +----------------------+
     * | CallableKind (1byte) |
     * +----------------------+
     */
    unsigned char *code;

public:
    DSCode(unsigned char *code) : code(code) {}

protected:
    ~DSCode() {
        free(this->code);
    }

public:
    const unsigned char *getCode() const {
        return this->code;
    }

    CodeKind getKind() const {
        return static_cast<CodeKind>(this->code[0]);
    }

    bool is(CodeKind kind) const {
        return this->getKind() == kind;
    }

    unsigned int getCodeSize() const {
        return read32(this->code, 1);
    }

    unsigned int getCodeOffset() const {
        return this->is(CodeKind::NATIVE) ? 1 : this->is(CodeKind::TOPLEVEL) ? 9 : 7;
    }
};

struct NativeCode : public DSCode {
    NativeCode() : DSCode(nullptr) {}

    NativeCode(native_func_t func, bool hasRet) :
            DSCode(reinterpret_cast<unsigned char *>(malloc(sizeof(unsigned char) * 11))) {
        this->code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
        this->code[1] = static_cast<unsigned char>(OpCode::CALL_NATIVE);
        write64(this->code + 2, reinterpret_cast<unsigned long>(func));
        this->code[10] = static_cast<unsigned char>(hasRet ? OpCode::RETURN_V : OpCode::RETURN);
    }

    NativeCode(NativeCode &&o) : DSCode(o.code) {
        o.code = nullptr;
    }

    ~NativeCode() = default;

    NON_COPYABLE(NativeCode);

    NativeCode &operator=(NativeCode &&o) noexcept {
        NativeCode tmp(std::move(o));
        std::swap(this->code, tmp.code);
        return *this;
    }
};

struct SourcePosEntry {
    unsigned int address;

    /**
     * indicating source position.
     */
    unsigned int pos;
};

/**
 * entries must not be null
 */
unsigned int getSourcePos(const SourcePosEntry *const entries, unsigned int index);

struct ExceptionEntry {
    /**
     * if null, indicate sentinel
     */
    const DSType *type;

    unsigned int begin; // inclusive
    unsigned int end;   // exclusive
    unsigned int dest;  // catch block address
};

class CompiledCode : public DSCode {
private:
    SourceInfoPtr srcInfo;

    /**
     * if CallableKind is toplevel, it is null
     */
    char *name;

    /**
     * last element is sentinel (nullptr)
     */
    DSValue *constPool;

    /**
     * last element is sentinel ({0, 0})
     */
    SourcePosEntry *sourcePosEntries;

    /**
     * lats element is sentinel.
     */
    ExceptionEntry *exceptionEntries;

public:
    NON_COPYABLE(CompiledCode);

    CompiledCode(const SourceInfoPtr &srcInfo, const char *name, unsigned char *code,
             DSValue *constPool, SourcePosEntry *sourcePosEntries, ExceptionEntry *exceptionEntries) :
            DSCode(code), srcInfo(srcInfo), name(name == nullptr ? nullptr : strdup(name)),
            constPool(constPool), sourcePosEntries(sourcePosEntries), exceptionEntries(exceptionEntries) { }

    CompiledCode(CompiledCode &&c) :
            DSCode(c.code), srcInfo(c.srcInfo), name(c.name),
            constPool(c.constPool), sourcePosEntries(c.sourcePosEntries), exceptionEntries(c.exceptionEntries) {
        c.name = nullptr;
        c.code = nullptr;
        c.constPool = nullptr;
        c.sourcePosEntries = nullptr;
        c.exceptionEntries = nullptr;
    }

    ~CompiledCode() {
        free(this->name);
        delete[] this->constPool;
        delete[] this->sourcePosEntries;
        delete[] this->exceptionEntries;
    }

    unsigned short getLocalVarNum() const {
        return read16(this->code, 5);
    }

    unsigned short getGlobalVarNum() const {
        assert(this->getKind() == CodeKind::TOPLEVEL);
        return read16(this->code, 7);
    }

    const SourceInfoPtr &getSrcInfo() const {
        return this->srcInfo;
    }

    /**
     * may be null.
     */
    const char *getName() const {
        return this->name;
    }

    const DSValue *getConstPool() const {
        return this->constPool;
    }

    const SourcePosEntry *getSourcePosEntries() const {
        return this->sourcePosEntries;
    }

    const ExceptionEntry *getExceptionEntries() const {
        return this->exceptionEntries;
    }
};

class FuncObject : public DSObject {
private:
    CompiledCode code;

public:
    explicit FuncObject(CompiledCode &&callable) :
            DSObject(nullptr), code(std::move(callable)) { }

    ~FuncObject() = default;

    const CompiledCode &getCode() const {
        return this->code;
    }

    void setType(DSType *type) override;

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
};

std::string encodeMethodDescriptor(const char *methodName, const MethodHandle *handle);
std::pair<const char *, const MethodHandle *> decodeMethodDescriptor(const char *desc);

std::string encodeFieldDescriptor(const DSType &recvType, const char *fieldName, const DSType &fieldType);
std::tuple<const DSType *, const char *, const DSType *> decodeFieldDescriptor(const char *desc);

struct ProxyObject : public DSObject {
    explicit ProxyObject(DSType &type) : DSObject(type) { }

    virtual ~ProxyObject() = default;

    /**
     * invoke method and return result.
     */
    virtual DSValue invokeMethod(DSState &ctx, const char *methodName, const MethodHandle *handle) = 0;

    /**
     * return got value
     */
    virtual DSValue invokeGetter(DSState &ctx, const DSType *recvType,
                                 const char *fieldName, const DSType *fieldType) = 0;

    /**
     * pop stack top value and set to field.
     */
    virtual void invokeSetter(DSState &ctx, const DSType *recvType,
                              const char *fieldName, const DSType *fieldType) = 0;
};

DSValue newDBusObject(TypePool &pool);

} // namespace ydsh

#endif //YDSH_OBJECT_H
