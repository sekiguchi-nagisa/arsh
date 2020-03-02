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

#include <unistd.h>

#include <memory>
#include <tuple>
#include <cxxabi.h>

#include "type.h"
#include <config.h>
#include "misc/fatal.h"
#include "misc/buffer.hpp"
#include "misc/string_ref.hpp"
#include "lexer.h"
#include "opcode.h"
#include "regex_wrapper.h"

namespace ydsh {

class DSValue;

enum ObjectKind {
    DUMMY,
    LONG,
    FLOAT,
    STRING,
    STRITER,
    FD,
    REGEX,
    ARRAY,
    MAP,
    BASE,
    TUPLE,
    ERROR,
    FUNC_OBJ,
    JOB,
    PIPESTATE,
    REDIR,
};

class DSObject {
protected:
    unsigned int refCount{0};

    const ObjectKind kind;

    const unsigned int typeID;

    friend class DSValue;

public:
    NON_COPYABLE(DSObject);

    DSObject(ObjectKind kind, const DSType &type) : DSObject(kind, type.getTypeID()){ }
    DSObject(ObjectKind kind, TYPE type) : DSObject(kind, static_cast<unsigned int>(type)) { }
    DSObject(ObjectKind kind, unsigned int typeID) : kind(kind), typeID(typeID) {}

    virtual ~DSObject() = default;

    unsigned int getTypeID() const {
        return this->typeID;
    }

    unsigned int getRefcount() const {
        return this->refCount;
    }

    ObjectKind getKind() const {
        return this->kind;
    }
};

enum class DSValueKind : unsigned char {
    OBJECT = 0,
    NUMBER = 130,   // uint32_t
    INVALID = 132,
    BOOL = 134,
    SIG = 136,  // int32_t
    INT = 138,  // int32_t
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
         *   7bit                   57bit
         *
         * significant 7bit represents tag (DSValueKind).
         *
         */
        int64_t val;
    };

public:
    /**
     * obj may be null
     */
    explicit DSValue(DSObject *obj) noexcept : DSValue(reinterpret_cast<int64_t>(obj)) { }

    explicit DSValue(uint64_t val) noexcept : val(val) {
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
    int64_t value() const noexcept {
        return this->val & 0x1FFFFFFFFFFFFFF;
    }

    DSValueKind kind() const noexcept {
        return static_cast<DSValueKind>((this->val & 0xFE00000000000000) >> 56);
    }

    DSObject *get() const noexcept {
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

    bool isValidObject() const noexcept {
        return this->isObject() && static_cast<bool>(*this);
    }

    bool isInvalid() const noexcept {
        return this->kind() == DSValueKind::INVALID;
    }

    unsigned int getTypeID() const;

    bool hasType(TYPE t) const {
        return hasType(static_cast<unsigned int>(t));
    }

    bool hasType(unsigned int id) const {
        return this->getTypeID() == id;
    }

    void swap(DSValue &value) noexcept {
        std::swap(this->obj, value.obj);
    }

    bool asBool() const {
        assert(this->kind() == DSValueKind::BOOL);
        return this->value() == 1;
    }

    int asSig() const {
        assert(this->kind() == DSValueKind::SIG);
        unsigned int v = this->value();
        return v;
    }

    int asInt() const {
        assert(this->kind() == DSValueKind::INT);
        unsigned int v = this->value();
        return v;
    }

    std::string toString() const;

    /**
     * OP_STR method implementation. write result to `toStrBuf'
     * @param state
     * @return
     * if has error, return false
     */
    bool opStr(DSState &state) const;

    /**
     * OP_INTERP method implementation. write result to 'toStrBuf'
     * @param state
     * @return
     * if has error, return false
     */
    bool opInterp(DSState &state) const;

    /**
     * for HashMap
     */
    bool equals(const DSValue &o) const;

    /**
     * for HashMap
     * @return
     */
    size_t hash() const;

    /**
     * for Array#sort
     * @param o
     * @return
     */
    bool compare(const DSValue &o) const;

    template <typename T, typename ...A>
    static DSValue create(A &&...args) {
        static_assert(std::is_base_of<DSObject, T>::value, "must be subtype of DSObject");

        return DSValue(new T(std::forward<A>(args)...));
    }

    static DSValue createNum(unsigned int v) {
        auto mask = static_cast<uint64_t>(DSValueKind::NUMBER) << 56;
        return DSValue(mask | v);
    }

    static DSValue createInvalid() {
        return DSValue(static_cast<uint64_t>(DSValueKind::INVALID) << 56);
    }

    static DSValue createBool(bool v) {
        auto mask = static_cast<uint64_t>(DSValueKind::BOOL) << 56;
        return DSValue(mask | (v ? 1 : 0));
    }

    static DSValue createSig(int num) {
        auto mask = static_cast<uint64_t>(DSValueKind::SIG) << 56;
        unsigned int v = num;
        return DSValue(mask | v);
    }

    static DSValue createInt(int num) {
        auto mask = static_cast<uint64_t>(DSValueKind::INT) << 56;
        unsigned int v = num;
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
            fatal("must be represent DSObject\n");
        }
        auto *r = dynamic_cast<T*>(value.get());
        if(r == nullptr) {
            DSObject &v = *value.get();
            int status;
            char *target = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);
            char *actual = abi::__cxa_demangle(typeid(v).name(), nullptr, nullptr, &status);

            fatal("target type is: %s, but actual is: %s\n", target, actual);
        }
        return r;
    }

    return static_cast<T*>(value.get());

}

class UnixFD_Object : public DSObject {
private:
    int fd;

public:
    UnixFD_Object(const DSType &type, int fd) : DSObject(ObjectKind::FD, type), fd(fd) {}
    ~UnixFD_Object() override;

    int tryToClose(bool forceClose) {
        if(!forceClose && this->fd < 0) {
            return 0;
        }
        int s = close(this->fd);
        this->fd = -1;
        return s;
    }

    /**
     * set close-on-exec flag to file descriptor.
     * if fd is STDIN, STDOUT or STDERR, not set flag.
     * @param close
     * @return
     * if failed, return false
     */
    bool closeOnExec(bool close);

    int getValue() const {
        return this->fd;
    }
};

class Long_Object : public DSObject {
private:
    long value;

public:
    explicit Long_Object(long value) : DSObject(ObjectKind::LONG, TYPE::Int64), value(value) { }

    ~Long_Object() override = default;

    long getValue() const {
        return this->value;
    }
};

class Float_Object : public DSObject {
private:
    double value;

public:
    explicit Float_Object(double value) : DSObject(ObjectKind::FLOAT, TYPE::Float), value(value) { }

    ~Float_Object() override = default;

    double getValue() const {
        return this->value;
    }
};

class String_Object : public DSObject {
private:
    std::string value;

public:
    explicit String_Object(const DSType &type) :
            DSObject(ObjectKind::STRING, type) { }

    String_Object(const DSType &type, std::string &&value) :
            DSObject(ObjectKind::STRING, type), value(std::move(value)) { }

    explicit String_Object(std::string &&value) :
            DSObject(ObjectKind::STRING, TYPE::String), value(std::move(value)) { }

    String_Object(const DSType &type, const std::string &value) :
            DSObject(ObjectKind::STRING, type), value(value) { }

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

    DSValue slice(unsigned int begin, unsigned int end) const {
        return DSValue::create<String_Object>(std::string(this->getValue() + begin, end - begin));
    }
};

/**
 * create StringRef
 * @param value
 * msut be String_Object
 * @return
 */
inline StringRef createStrRef(const DSValue &value) {
    auto *obj = typeAs<String_Object>(value);
    return StringRef(obj->getValue(), obj->size());
}

struct StringIter_Object : public DSObject {
    unsigned int curIndex;
    DSValue strObj;

    StringIter_Object(const DSType &type, String_Object *str) :
            DSObject(ObjectKind::STRITER, type), curIndex(0), strObj(DSValue(str)) { }
};

class Regex_Object : public DSObject {
private:
    std::string str; // for string representation
    PCRE re;

public:
    Regex_Object(const DSType &type, std::string str, PCRE &&re) :
            DSObject(ObjectKind::REGEX, type), str(std::move(str)), re(std::move(re)) {}

    ~Regex_Object() override = default;

    bool search(StringRef ref) const {
        int ovec[1];
        int match = pcre_exec(this->re.get(), nullptr, ref.data(), ref.size(), 0, 0, ovec, arraySize(ovec));
        return match >= 0;
    }

    int match(StringRef ref, FlexBuffer<int> &ovec) const {
        int captureSize;
        pcre_fullinfo(this->re.get(), nullptr, PCRE_INFO_CAPTURECOUNT, &captureSize);
        ovec = FlexBuffer<int>((captureSize + 1) * 3, 0);
        return pcre_exec(this->re.get(), nullptr, ref.data(), ref.size(), 0, 0, ovec.get(), (captureSize + 1) * 3);
    }

    const std::string &getStr() const {
        return this->str;
    }
};

class Array_Object : public DSObject {
private:
    unsigned int curIndex;
    std::vector<DSValue> values;

public:
    using IterType = std::vector<DSValue>::const_iterator;

    explicit Array_Object(const DSType &type) : DSObject(ObjectKind::ARRAY, type), curIndex(0) { }

    Array_Object(const DSType &type, std::vector<DSValue> &&values) :
            Array_Object(type.getTypeID(), std::move(values)) {}

    Array_Object(unsigned int typeID, std::vector<DSValue> &&values) :
            DSObject(ObjectKind::ARRAY, typeID), curIndex(0), values(std::move(values)) { }

    ~Array_Object() override = default;

    const std::vector<DSValue> &getValues() const {
        return this->values;
    }

    std::vector<DSValue> &refValues() {
        return this->values;
    }

    unsigned int size() const {
        return this->values.size();
    }

    std::string toString() const;
    bool opStr(DSState &state) const;
    bool opInterp(DSState &state) const;
    DSValue opCmdArg(DSState &state) const;

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

    const DSValue &nextElement() {
        unsigned int index = this->curIndex++;
        return this->values[index];
    }

    bool hasNext() const {
        return this->curIndex < this->values.size();
    }

    DSValue slice(unsigned int begin, unsigned int end) const {
        auto b = this->getValues().begin() + begin;
        auto e = this->getValues().begin() + end;
        return DSValue::create<Array_Object>(this->getTypeID(), std::vector<DSValue>(b, e));
    }

    DSValue takeFirst() {
        auto v = this->values.front();
        this->values.erase(values.begin());
        return v;
    }

    void sortAsStrArray() {
        std::sort(values.begin(), values.end(), [](const DSValue &x, const DSValue &y) {
            return createStrRef(x) < createStrRef(y);
        });
    }
};

inline const char *str(const DSValue &v) {
    return createStrRef(v).data();
}

struct KeyCompare {
    bool operator()(const DSValue &x, const DSValue &y) const {
        return x.equals(y);
    }
};

struct GenHash {
    std::size_t operator()(const DSValue &key) const {
        return key.hash();
    }
};

using HashMap = std::unordered_map<DSValue, DSValue, GenHash, KeyCompare>;

class Map_Object : public DSObject {
private:
    HashMap valueMap;
    HashMap::const_iterator iter;

public:
    explicit Map_Object(const DSType &type) : DSObject(ObjectKind::MAP, type) { }

    Map_Object(const DSType &type, HashMap &&map) : Map_Object(type.getTypeID(), std::move(map)) {}

    Map_Object(unsigned int typeID, HashMap &&map) : DSObject(ObjectKind::MAP, typeID), valueMap(std::move(map)) {}

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

    std::string toString() const;
    bool opStr(DSState &state) const;
};

class BaseObject : public DSObject {
protected:
    unsigned int fieldSize;
    DSValue *fieldTable;

    BaseObject(ObjectKind kind, const DSType &type) :
        DSObject(kind, type), fieldSize(type.getFieldSize()), fieldTable(new DSValue[this->fieldSize]) { }

public:
    explicit BaseObject(const DSType &type) : BaseObject(ObjectKind::BASE, type) {}

    ~BaseObject() override;

    DSValue &operator[](unsigned int index) {
        return this->fieldTable[index];
    }

    unsigned int getFieldSize() const {
        return this->fieldSize;
    }
};

struct Tuple_Object : public BaseObject {
    explicit Tuple_Object(const DSType &type) : BaseObject(ObjectKind::TUPLE, type) { }

    ~Tuple_Object() override = default;

    std::string toString() const;
    bool opStr(DSState &state) const;
    bool opInterp(DSState &state) const;
    DSValue opCmdArg(DSState &state) const;
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
 *
 * @param elements
 * @return
 * if stack trace elements is empty, return 0.
 */
inline unsigned int getOccurredLineNum(const std::vector<StackTraceElement> &elements) {
    return elements.empty() ? 0 : elements.front().getLineNum();
}

/**
 *
 * @param elements
 * @return
 * fi stack trace elements is empty, return empty string
 */
inline const char *getOccurredSourceName(const std::vector<StackTraceElement> &elements) {
    return elements.empty() ? "" : elements.front().getSourceName().c_str();
}

class Error_Object : public DSObject {
private:
    DSValue message;
    DSValue name;
    std::vector<StackTraceElement> stackTrace;

    Error_Object(const DSType &type, DSValue &&message) :
            DSObject(ObjectKind::ERROR, type), message(std::move(message)) { }

public:
    ~Error_Object() override = default;

    bool opStr(DSState &state) const;

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
    static DSValue newError(const DSState &ctx, const DSType &type, const DSValue &message) {
        return newError(ctx, type, DSValue(message));
    }

    static DSValue newError(const DSState &ctx, const DSType &type, DSValue &&message);

private:
    std::string createHeader(const DSState &state) const;
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

    DSCode() : code(nullptr) {}

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

class NativeCode : public DSCode {
private:
    std::string value;

public:
    NativeCode() : DSCode(nullptr) {}

    NativeCode(unsigned int index, bool hasRet) : value(8, '\0'){
        this->value[0] = static_cast<char>(CodeKind::NATIVE);
        this->value[1] = static_cast<char>(OpCode::CALL_NATIVE);
        this->value[2] = index;
        this->value[3] = static_cast<char>(hasRet ? OpCode::RETURN_V : OpCode::RETURN);
        this->setCode();
    }

    explicit NativeCode(std::string &&value) {
        std::swap(this->value, value);
        this->setCode();
    }

    NativeCode(NativeCode &&o) noexcept {
        std::swap(this->value, o.value);
        o.code = nullptr;
        this->setCode();
    }

    NON_COPYABLE(NativeCode);

    NativeCode &operator=(NativeCode &&o) noexcept {
        this->swap(o);
        return *this;
    }

    void swap(NativeCode &o) noexcept {
        std::swap(this->value, o.value);
        this->setCode();
        o.setCode();
    }

private:
    void setCode() {
        this->code = reinterpret_cast<unsigned char *>(const_cast<char *>(this->value.c_str()));
    }
};

struct LineNumEntry {
    unsigned int address;
    unsigned int lineNum;
};

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
        free(this->code);
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

    unsigned int getLineNum(unsigned int index) const;

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
    FuncObject(const DSType &funcType, CompiledCode &&callable) :
            DSObject(ObjectKind::FUNC_OBJ, funcType), code(std::move(callable)) {}

    ~FuncObject() override = default;

    const CompiledCode &getCode() const {
        return this->code;
    }

    std::string toString() const;
};

} // namespace ydsh

#endif //YDSH_OBJECT_H
