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

#ifndef YDSH_OBJECT_H
#define YDSH_OBJECT_H

#include <memory>
#include <unordered_set>
#include <tuple>
#include <cxxabi.h>

#include "type.h"
#include <config.h>
#include "misc/fatal.h"
#include "misc/buffer.hpp"
#include "lexer.h"
#include "opcode.h"
#include "regex_wrapper.h"
#include "job.h"

namespace ydsh {

class DSValue;

using VisitedSet = std::unordered_set<unsigned long>;

class DSObject {
protected:
    DSType *type;
    unsigned int refCount{0};

    friend class DSValue;

public:
    NON_COPYABLE(DSObject);

    explicit DSObject(DSType &type) : type(&type) { }
    explicit DSObject(DSType *type) : type(type) { }

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

    virtual DSValue *getFieldTable();

    /**
     * for printing
     */
    virtual std::string toString(DSState &ctx, VisitedSet *visitedSet);

    /**
     * OP_STR method implementation. write result to `toStrBuf'
     * @param state
     * @return
     * if has error, return false
     */
    virtual bool opStr(DSState &state) const;

    virtual std::string toString() const;

    /**
     * EQ method implementation.
     */
    virtual bool equals(const DSValue &obj) const;

    /**
     * for Array#sort
     * @param obj
     * @return
     */
    virtual bool compare(const DSValue &obj) const;

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
    virtual size_t hash() const;

    /**
     * check if this type is instance of targetType.
     */
    bool introspect(DSState &, DSType *targetType) const {
        return targetType->isSameOrBaseTypeOf(*this->type);
    }
};

enum class DSValueKind : unsigned char {
    OBJECT = 0,
    NUMBER = 129,
    INVALID = 130,
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
         * otherwise, represents native pointer, number ... etc.
         *
         *               DSValue format
         * +-------+---------------------------------+
         * |  tag  |    DSObject pointer or value    |
         * +-------+---------------------------------+
         *   8bit                   56bit
         *
         * significant 8bit represents tag (DSValueKind).
         *
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

    constexpr DSValue(std::nullptr_t) noexcept: obj(nullptr) { }    //NOLINT

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
        this->swap(value);
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

    bool operator==(std::nullptr_t) const noexcept {
        return this->obj == nullptr;
    }

    bool operator!=(std::nullptr_t) const noexcept {
        return this->obj != nullptr;
    }

    bool operator==(const DSValue &v) const noexcept {
        return this->val == v.val;
    }

    bool operator!=(const DSValue &v) const noexcept {
        return this->val != v.val;
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

    bool isInvalid() const noexcept {
        return this->kind() == DSValueKind::INVALID;
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

    static DSValue createInvalid() {
        return DSValue(static_cast<unsigned long>(DSValueKind::INVALID) << 56);
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
            fatal("must be represent DSObject\n");
        }
        auto *r = dynamic_cast<T*>(value.get());
        if(r == nullptr) {
            DSObject &v = *value;
            int status;
            char *target = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);
            char *actual = abi::__cxa_demangle(typeid(v).name(), nullptr, nullptr, &status);

            fatal("target type is: %s, but actual is: %s\n", target, actual);
        }
        return r;
    }

    return static_cast<T*>(value.get());

}

class Int_Object : public DSObject {
protected:
    int value;

public:
    Int_Object(DSType &type, int value) : DSObject(type), value(value) { }

    ~Int_Object() override = default;

    int getValue() const {
        return this->value;
    }

    std::string toString() const override;
    bool equals(const DSValue &obj) const override;
    bool compare(const DSValue &obj) const override;
    size_t hash() const override;
};

struct UnixFD_Object : public Int_Object {
    UnixFD_Object(DSType &type, int fd) : Int_Object(type, fd) {}
    ~UnixFD_Object() override;

    int tryToClose(bool forceClose) {
        if(!forceClose && this->value < 0) {
            return 0;
        }
        int s = close(this->value);
        this->value = -1;
        return s;
    }

    std::string toString() const override;
};

class Long_Object : public DSObject {
private:
    long value;

public:
    Long_Object(DSType &type, long value) : DSObject(type), value(value) { }

    ~Long_Object() override = default;

    long getValue() const {
        return this->value;
    }

    std::string toString() const override;
    bool equals(const DSValue &obj) const override;
    bool compare(const DSValue &obj) const override;
    size_t hash() const override;
};

class Float_Object : public DSObject {
private:
    double value;

public:
    Float_Object(DSType &type, double value) : DSObject(type), value(value) { }

    ~Float_Object() override = default;

    double getValue() const {
        return this->value;
    }

    std::string toString() const override;
    bool equals(const DSValue &obj) const override;
    bool compare(const DSValue &obj) const override;
    size_t hash() const override;
};

class Boolean_Object : public DSObject {
private:
    bool value;

public:
    Boolean_Object(DSType &type, bool value) : DSObject(type), value(value) { }

    ~Boolean_Object() override = default;

    bool getValue() const {
        return this->value;
    }

    std::string toString() const override;
    bool equals(const DSValue &obj) const override;
    bool compare(const DSValue &obj) const override;
    size_t hash() const override;
};

class String_Object : public DSObject {
private:
    std::string value;

public:
    explicit String_Object(DSType &type) :
            DSObject(type) { }

    String_Object(DSType &type, std::string &&value) :
            DSObject(type), value(std::move(value)) { }

    String_Object(DSType &type, const std::string &value) :
            DSObject(type), value(value) { }

    ~String_Object() override = default;

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
    void append(DSValue &&obj) {
        this->value += typeAs<String_Object>(obj)->value;
    }

    std::string toString() const override;
    bool equals(const DSValue &obj) const override;
    bool compare(const DSValue &obj) const override;
    size_t hash() const override;
};

struct StringIter_Object : public DSObject {
    unsigned int curIndex;
    DSValue strObj;

    StringIter_Object(DSType &type, String_Object *str) :
            DSObject(type), curIndex(0), strObj(DSValue(str)) { }
};

class Regex_Object : public DSObject {
private:
    PCRE re;

public:
    Regex_Object(DSType &type, PCRE &&re) : DSObject(type), re(std::move(re)) {}

    ~Regex_Object() override = default;

    const PCRE &getRe() const {
        return this->re;
    }
};

class Array_Object : public DSObject {
private:
    unsigned int curIndex;
    std::vector<DSValue> values;

public:
    using IterType = std::vector<DSValue>::const_iterator;

    explicit Array_Object(DSType &type) : DSObject(type), curIndex(0) { }

    Array_Object(DSType &type, std::vector<DSValue> &&values) :
            DSObject(type), curIndex(0), values(std::move(values)) { }

    ~Array_Object() override = default;

    const std::vector<DSValue> &getValues() const {
        return this->values;
    }

    std::vector<DSValue> &refValues() {
        return this->values;
    }

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
    std::string toString() const override;
    bool opStr(DSState &state) const override;

    void append(DSValue &&obj) {
        this->values.push_back(std::move(obj));
    }

    void append(const DSValue &obj) {
        this->values.push_back(obj);
    }

    void set(unsigned int index, DSValue &&obj) {
        this->values[index] = std::move(obj);
    }

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

/**
 *
 * @param v
 * must be Array_Object
 */
inline void eraseFirst(Array_Object &v) {
    auto &values = v.refValues();
    values.erase(values.begin());
}

inline const char *str(const DSValue &v) {
    return typeAs<String_Object>(v)->getValue();
}

struct KeyCompare {
    bool operator() (const DSValue &x, const DSValue &y) const {
        return x->equals(y);
    }
};

struct GenHash {
    std::size_t operator() (const DSValue &key) const {
        return key->hash();
    }
};

using HashMap = std::unordered_map<DSValue, DSValue, GenHash, KeyCompare>;

class Map_Object : public DSObject {
private:
    HashMap valueMap;
    HashMap::const_iterator iter;

public:
    explicit Map_Object(DSType &type) : DSObject(type) { }

    ~Map_Object() override = default;

    const HashMap &getValueMap() const {
        return this->valueMap;
    }

    void clear() {
        this->valueMap.clear();
        this->initIterator();
    }

    /**
     *
     * @param key
     * @param value
     * @return
     * old element. if not found (first time insertion), return invalid
     */
    DSValue set(DSValue &&key, DSValue &&value) {
        auto pair = this->valueMap.insert(std::make_pair(std::move(key), value));
        if(pair.second) {
            this->iter = ++pair.first;
            return DSValue::createInvalid();
        }
        std::swap(pair.first->second, value);
        return std::move(value);
    }

    DSValue setDefault(DSValue &&key, DSValue &&value) {
        auto pair = this->valueMap.insert(std::make_pair(std::move(key), std::move(value)));
        if(pair.second) {
            this->iter = pair.first;
            this->iter++;
        }
        return pair.first->second;
    }

    bool trySwap(const DSValue &key, DSValue &value) {
        auto iter = this->valueMap.find(key);
        if(iter != this->valueMap.end()) {
            std::swap(iter->second, value);
            return true;
        }
        return false;
    }

    bool remove(const DSValue &key) {
        auto iter = this->valueMap.find(key);
        if(iter == this->valueMap.end()) {
            return false;
        }
        this->iter = this->valueMap.erase(iter);
        return true;
    }

    void initIterator() {
        this->iter = this->valueMap.cbegin();
    }

    DSValue nextElement(DSState &ctx);

    bool hasNext() {
        return this->iter != this->valueMap.cend();
    }

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
    std::string toString() const override;
    bool opStr(DSState &state) const override;
};

class Job_Object : public DSObject {
private:
    Job entry;

    /**
     * writable file descriptor (connected to STDIN of Job). must be UnixFD_Object
     */
    DSValue inObj;

    /**
     * readable file descriptor (connected to STDOUT of Job). must be UnixFD_Object
     */
    DSValue outObj;

public:
    Job_Object(DSType &type, Job entry, DSValue inObj, DSValue outObj) :
            DSObject(type), entry(std::move(entry)), inObj(std::move(inObj)), outObj(std::move(outObj)) {}

    ~Job_Object() override = default;

    DSValue getInObj() const {
        return this->inObj;
    }

    DSValue getOutObj() const {
        return this->outObj;
    }

    const Job &getEntry() const {
        return this->entry;
    }

    bool poll() {
        this->entry->wait(Proc::NONBLOCKING);
        return this->entry->available();
    }

    int wait(JobTable &jobTable, bool jobctrl) {
        int s = jobTable.waitAndDetach(this->entry, jobctrl);
        if(!this->entry->available()) {
            typeAs<UnixFD_Object>(this->inObj)->tryToClose(false);
            typeAs<UnixFD_Object>(this->outObj)->tryToClose(false);
            jobTable.updateStatus();
        }
        return s;
    }

    std::string toString() const override;
};

class BaseObject : public DSObject {
protected:
    DSValue *fieldTable;

public:
    explicit BaseObject(DSType &type) :
            DSObject(type), fieldTable(new DSValue[type.getFieldSize()]) { }

    ~BaseObject() override;

    DSValue *getFieldTable() override;
};

struct Tuple_Object : public BaseObject {
    explicit Tuple_Object(DSType &type) : BaseObject(type) { }

    ~Tuple_Object() override = default;

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
    std::string toString() const override;
    bool opStr(DSState &state) const override;

    unsigned int getElementSize() const {
        return this->type->getFieldSize();
    }

    void set(unsigned int elementIndex, const DSValue &obj) {
        this->fieldTable[elementIndex] = obj;
    }

    const DSValue &get(unsigned int elementIndex) {
        return this->fieldTable[elementIndex];
    }

    DSValue interp(DSState &ctx, VisitedSet *visitedSet) override;
    DSValue commandArg(DSState &ctx, VisitedSet *visitedSet) override;
};

class StackTraceElement {
private:
    std::string sourceName;
    unsigned int lineNum;
    std::string callerName;

public:
    StackTraceElement(const char *sourceName, unsigned int lineNum, std::string &&callerName) :
            sourceName(sourceName), lineNum(lineNum), callerName(std::move(callerName)) { }

    ~StackTraceElement() = default;

    const std::string &getSourceName() const {
        return this->sourceName;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    const std::string &getCallerName() const {
        return this->callerName;
    }
};

/**
 * if stack trace elements is empty, return 0.
 */
inline unsigned int getOccurredLineNum(const std::vector<StackTraceElement> &elements) {
    return elements.empty() ? 0 : elements.front().getLineNum();
}

inline const char *getOccurredSourceName(const std::vector<StackTraceElement> &elements) {
    return elements.empty() ? nullptr : elements.front().getSourceName().c_str();
}

class Error_Object : public DSObject {
private:
    DSValue message;
    DSValue name;
    std::vector<StackTraceElement> stackTrace;

    Error_Object(DSType &type, DSValue &&message) :
            DSObject(type), message(std::move(message)) { }

public:
    ~Error_Object() override = default;

    std::string toString(DSState &ctx, VisitedSet *visitedSet) override;
    bool opStr(DSState &state) const override;

    const DSValue &getMessage() const {
        return this->message;
    }

    const DSValue &getName() const {
        return this->name;
    }

    /**
     * print stack trace to stderr
     */
    void printStackTrace(DSState &ctx);

    const std::vector<StackTraceElement> &getStackTrace() const {
        return this->stackTrace;
    }

    /**
     * create new Error_Object and create stack trace
     */
    static DSValue newError(const DSState &ctx, DSType &type, const DSValue &message) {
        return newError(ctx, type, DSValue(message));
    }

    static DSValue newError(const DSState &ctx, DSType &type, DSValue &&message);

private:
    std::string createHeader(const DSState &state) const;

    void createStackTrace(const DSState &ctx);
};

enum class CodeKind : unsigned char {
    TOPLEVEL         = 8,
    FUNCTION         = (1u << 4) + 8,
    USER_DEFINED_CMD = (2u << 4) + 8,
    NATIVE           = (3u << 4) + 1,
};

class DSCode {
protected:
    /**
     *
     * if indicate compiled code
     *
     * +----------------------+-------------------+-------------------------------+---------------------+
     * | CallableKind (1byte) | code size (4byte) | local variable number (1byte) | stack depth (2byte) |
     * +----------------------+-------------------+-------------------------------+---------------------+
     *
     * if indicate native
     *
     * +----------------------+
     * | CallableKind (1byte) |
     * +----------------------+
     */
    unsigned char *code;

    explicit DSCode(unsigned char *code) : code(code) {}

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
        return this->code[0] & 0xF;
    }
};

struct NativeCode : public DSCode {
    NativeCode() : DSCode(nullptr) {}

    NativeCode(native_func_t func, bool hasRet) :
            DSCode(static_cast<unsigned char *>(malloc(sizeof(unsigned char) * 11))) {
        this->code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
        this->code[1] = static_cast<unsigned char>(OpCode::CALL_NATIVE);
        write64(this->code + 2, reinterpret_cast<unsigned long>(func));
        this->code[10] = static_cast<unsigned char>(hasRet ? OpCode::RETURN_V : OpCode::RETURN);
    }

    explicit NativeCode(unsigned char *code) : DSCode(code) {}

    NativeCode(NativeCode &&o) noexcept : DSCode(o.code) {
        o.code = nullptr;
    }

    ~NativeCode() = default;

    NON_COPYABLE(NativeCode);

    NativeCode &operator=(NativeCode &&o) noexcept {
        std::swap(this->code, o.code);
        return *this;
    }
};

struct LineNumEntry {
    unsigned int address;
    unsigned int lineNum;
};

/**
 * entries must not be null
 */
unsigned int getLineNum(const LineNumEntry *entries, unsigned int index);

struct ExceptionEntry {
    /**
     * if null, indicate sentinel
     */
    const DSType *type;

    unsigned int begin; // inclusive
    unsigned int end;   // exclusive
    unsigned int dest;  // catch block address

    unsigned short localOffset;
    unsigned short localSize;
};

class CompiledCode : public DSCode {
private:
    char *sourceName{nullptr};

    /**
     * if CallableKind is toplevel, it is null
     */
    char *name{nullptr};

    /**
     * last element is sentinel (nullptr)
     */
    DSValue *constPool{nullptr};

    /**
     * last element is sentinel ({0, 0})
     */
    LineNumEntry *lineNumEntries{nullptr};

    /**
     * lats element is sentinel.
     */
    ExceptionEntry *exceptionEntries{nullptr};

public:
    NON_COPYABLE(CompiledCode);

    CompiledCode(const SourceInfo &srcInfo, const char *name, unsigned char *code,
                 DSValue *constPool, LineNumEntry *sourcePosEntries, ExceptionEntry *exceptionEntries) noexcept :
            DSCode(code), sourceName(strdup(srcInfo->getSourceName().c_str())), name(name == nullptr ? nullptr : strdup(name)),
            constPool(constPool), lineNumEntries(sourcePosEntries), exceptionEntries(exceptionEntries) { }

    CompiledCode(CompiledCode &&c) noexcept :
            DSCode(c.code), sourceName(c.sourceName), name(c.name),
            constPool(c.constPool), lineNumEntries(c.lineNumEntries), exceptionEntries(c.exceptionEntries) {
        c.sourceName = nullptr;
        c.name = nullptr;
        c.code = nullptr;
        c.constPool = nullptr;
        c.lineNumEntries = nullptr;
        c.exceptionEntries = nullptr;
    }

    CompiledCode() noexcept : DSCode(nullptr) {}

    ~CompiledCode() {
        free(this->sourceName);
        free(this->name);
        delete[] this->constPool;
        free(this->lineNumEntries);
        delete[] this->exceptionEntries;
    }

    CompiledCode &operator=(CompiledCode &&o) noexcept {
        this->swap(o);
        return *this;
    }

    void swap(CompiledCode &o) noexcept {
        std::swap(this->code, o.code);
        std::swap(this->sourceName, o.sourceName);
        std::swap(this->name, o.name);
        std::swap(this->constPool, o.constPool);
        std::swap(this->lineNumEntries, o.lineNumEntries);
        std::swap(this->exceptionEntries, o.exceptionEntries);
    }

    unsigned short getLocalVarNum() const {
        return read8(this->code, 5);
    }

    unsigned short getStackDepth() const {
        return read16(this->code, 6);
    }

    const char *getSourceName() const {
        return this->sourceName;
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

    const LineNumEntry *getLineNumEntries() const {
        return this->lineNumEntries;
    }

    const ExceptionEntry *getExceptionEntries() const {
        return this->exceptionEntries;
    }

    explicit operator bool() const noexcept {
        return this->code != nullptr;
    }
};

class FuncObject : public DSObject {
private:
    CompiledCode code;

public:
    FuncObject(DSType *funcType, CompiledCode &&callable) :
            DSObject(funcType), code(std::move(callable)) {}

    explicit FuncObject(CompiledCode &&callable) : FuncObject(nullptr, std::move(callable)) { }

    ~FuncObject() override = default;

    const CompiledCode &getCode() const {
        return this->code;
    }

    std::string toString() const override;
};

} // namespace ydsh

#endif //YDSH_OBJECT_H
