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
#include "misc/rtti.hpp"
#include "lexer.h"
#include "opcode.h"
#include "regex_wrapper.h"

namespace ydsh {

class DSValue;

class DSObject {
public:
    /**
     * for LLVM-style RTTI
     * see. https://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html
     */
    enum ObjectKind {
        Dummy,
        Long,
        Float,
        String,
        UnixFd,
        Regex,
        Array,
        Map,
        Base,
        Error,
        Func,
        JobImpl,
        Pipeline,
        Redir,
    };

protected:
    unsigned int refCount{0};

    const ObjectKind kind;

    const unsigned int typeID;

    friend class DSValue;

    NON_COPYABLE(DSObject);

    DSObject(ObjectKind kind, unsigned int typeID) : kind(kind), typeID(typeID) {}

public:
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

template <DSObject::ObjectKind K>
struct ObjectWithRtti : public DSObject {
protected:
    ObjectWithRtti(const DSType &type) : ObjectWithRtti(type.getTypeID()) {}
    ObjectWithRtti(TYPE type) : ObjectWithRtti(static_cast<unsigned int>(type)) {}
    ObjectWithRtti(unsigned int id) : DSObject(K, id) {}

public:
    static constexpr auto value = K;

    static bool classof(const DSObject *obj) {
        return obj->getKind() == K;
    }
};

struct DummyObject : public ObjectWithRtti<DSObject::Dummy> {
    explicit DummyObject(const DSType &type) : ObjectWithRtti(type) {}
};

class UnixFdObject : public ObjectWithRtti<DSObject::UnixFd> {
private:
    int fd;

public:
    explicit UnixFdObject(int fd) : ObjectWithRtti(TYPE::UnixFD), fd(fd) {}
    ~UnixFdObject() override;

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

class LongObject : public ObjectWithRtti<DSObject::Long> {
private:
    long value;

public:
    explicit LongObject(long value) : ObjectWithRtti(TYPE::Int64), value(value) { }

    ~LongObject() override = default;

    long getValue() const {
        return this->value;
    }
};

class FloatObject : public ObjectWithRtti<DSObject::Float> {
private:
    double value;

public:
    explicit FloatObject(double value) : ObjectWithRtti(TYPE::Float), value(value) { }

    ~FloatObject() override = default;

    double getValue() const {
        return this->value;
    }
};

class StringObject : public ObjectWithRtti<DSObject::String> {
private:
    std::string value;

public:
    static constexpr size_t MAX_SIZE = INT32_MAX;

    explicit StringObject(std::string &&value) :
            ObjectWithRtti(TYPE::String), value(std::move(value)) { }

    explicit StringObject(const StringRef &ref) : StringObject(ref.toString()) {}

    ~StringObject() override = default;

    const char *getValue() const {
        return this->value.c_str();
    }

    unsigned int size() const {
        return this->value.size();
    }

    void append(StringRef v) {
        this->value.append(v.data(), v.size());
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

    StringRef asStrRef() const;

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

    // for string construction
    static DSValue createStr() {
        return createStr(StringRef());
    }

    static DSValue createStr(const char *str) {
        return createStr(StringRef(str));
    }

    static DSValue createStr(StringRef ref) {
        return DSValue::create<StringObject>(ref);
    }

    static DSValue createStr(std::string &&value) {
        return DSValue::create<StringObject>(std::move(value));
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
        auto *r = checked_cast<T>(value.get());
        if(r == nullptr) {
            DSObject &v = *value.get();
            int status;
            char *target = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status);
            char *actual = abi::__cxa_demangle(typeid(v).name(), nullptr, nullptr, &status);

            fatal("target type is: %s, but actual is: %s\n", target, actual);
        }
        return r;
    }
    return cast<T>(value.get());
}

/**
 * concat string values. concatenation result is assigned to 'left'
 * @param left
 * if RefCount is 1,
 * @param right
 * @return
 * if success, return true.
 */
bool concatAsStr(DSValue &left, StringRef right);

inline bool concatAsStr(DSValue &left, const DSValue &right) {
    if(left.asStrRef().empty()) {
        left = right;
        return true;
    }
    return concatAsStr(left, right.asStrRef());
}

inline bool appendAsStr(DSValue &left, StringRef right) {
    assert(left.get()->getRefcount() == 1);
    return concatAsStr(left, right);
}

class RegexObject : public ObjectWithRtti<DSObject::Regex> {
private:
    std::string str; // for string representation
    PCRE re;

public:
    RegexObject(std::string str, PCRE &&re) :
            ObjectWithRtti(TYPE::Regex), str(std::move(str)), re(std::move(re)) {}

    ~RegexObject() override = default;

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

class ArrayObject : public ObjectWithRtti<DSObject::Array> {
private:
    unsigned int curIndex{0};
    std::vector<DSValue> values;

public:
    static constexpr size_t MAX_SIZE = INT32_MAX;

    using IterType = std::vector<DSValue>::const_iterator;

    explicit ArrayObject(const DSType &type) : ObjectWithRtti(type) { }

    ArrayObject(const DSType &type, std::vector<DSValue> &&values) :
            ArrayObject(type.getTypeID(), std::move(values)) {}

    ArrayObject(unsigned int typeID, std::vector<DSValue> &&values) :
            ObjectWithRtti(typeID), values(std::move(values)) { }

    ~ArrayObject() override = default;

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

    DSValue takeFirst() {
        auto v = this->values.front();
        this->values.erase(values.begin());
        return v;
    }

    void sortAsStrArray() {
        std::sort(values.begin(), values.end(), [](const DSValue &x, const DSValue &y) {
            return x.asStrRef() < y.asStrRef();
        });
    }
};

inline const char *str(const DSValue &v) {
    return v.asStrRef().data();
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

class MapObject : public ObjectWithRtti<DSObject::Map> {
private:
    HashMap valueMap;
    HashMap::const_iterator iter;

public:
    explicit MapObject(const DSType &type) : ObjectWithRtti(type) { }

    MapObject(const DSType &type, HashMap &&map) : MapObject(type.getTypeID(), std::move(map)) {}

    MapObject(unsigned int typeID, HashMap &&map) : ObjectWithRtti(typeID), valueMap(std::move(map)) {}

    ~MapObject() override = default;

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
        auto pair = this->valueMap.emplace(std::move(key), value);
        if(pair.second) {
            this->iter = ++pair.first;
            return DSValue::createInvalid();
        }
        std::swap(pair.first->second, value);
        return std::move(value);
    }

    DSValue setDefault(DSValue &&key, DSValue &&value) {
        auto pair = this->valueMap.emplace(std::move(key), std::move(value));
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

class BaseObject : public ObjectWithRtti<DSObject::Base> {
private:
    unsigned int fieldSize;
    DSValue *fieldTable;

public:
    BaseObject(const DSType &type, unsigned int size) :
            ObjectWithRtti(type), fieldSize(size), fieldTable(new DSValue[this->fieldSize]) { }

    /**
     * for tuple object construction
     * @param type
     * must be tuple type
     */
    BaseObject(const DSType &type) : BaseObject(type, type.getFieldSize()) {}

    ~BaseObject() override;

    DSValue &operator[](unsigned int index) {
        return this->fieldTable[index];
    }

    unsigned int getFieldSize() const {
        return this->fieldSize;
    }

    // for tuple type
    bool opStrAsTuple(DSState &state) const;
    bool opInterpAsTuple(DSState &state) const;
    DSValue opCmdArgAsTuple(DSState &state) const;
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

class ErrorObject : public ObjectWithRtti<DSObject::Error> {
private:
    DSValue message;
    DSValue name;
    std::vector<StackTraceElement> stackTrace;

    ErrorObject(const DSType &type, DSValue &&message) :
            ObjectWithRtti(type), message(std::move(message)) { }

public:
    ~ErrorObject() override = default;

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

class FuncObject : public ObjectWithRtti<DSObject::Func> {
private:
    CompiledCode code;

public:
    FuncObject(const DSType &funcType, CompiledCode &&callable) :
            ObjectWithRtti(funcType), code(std::move(callable)) {}

    ~FuncObject() override = default;

    const CompiledCode &getCode() const {
        return this->code;
    }

    std::string toString() const;
};

} // namespace ydsh

#endif //YDSH_OBJECT_H
