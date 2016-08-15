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

#ifndef YDSH_STATE_H
#define YDSH_STATE_H

#include <unistd.h>

#include <vector>
#include <iostream>

#include <ydsh/ydsh.h>

#include "node.h"
#include "object.h"
#include "symbol_table.h"
#include "proc.h"
#include "misc/buffer.hpp"
#include "misc/hash.hpp"
#include "misc/noncopyable.h"

namespace ydsh {

struct DebugHook;

/**
 * enum order is corresponding to builtin variable declaration order.
 */
enum class BuiltinVarOffset : unsigned int {
    DBUS,           // DBus
    OSTYPE,         // OSTYPE (utsname.sysname)
    VERSION,        // YDSH_VERSION (equivalent to ps_intrp '\V')
    REPLY,          // REPLY (for read command)
    REPLY_VAR,      // reply (fo read command)
    PID,            // PID (current process)
    PPID,           // PPID
    EXIT_STATUS,    // ?
    SHELL_PID,      // $
    ARGS,           // @
    ARGS_SIZE,      // #
    POS_0,          // 0 (for script name)
    POS_1,          // 1 (for argument)
    /*POS_2, POS_3, POS_4, POS_5, POS_6, POS_7, POS_8, POS_9, */
};

inline unsigned int toIndex(BuiltinVarOffset offset) {
    return static_cast<unsigned int>(offset);
}

class FilePathCache {
private:
    /**
     * contains previously resolved path (for directive search)
     */
    std::string prevPath;

    CStringHashMap<std::string> map;

    static constexpr unsigned int MAX_CACHE_SIZE = 100;

public:
    FilePathCache() = default;

    ~FilePathCache();

    static constexpr unsigned char USE_DEFAULT_PATH = 1 << 0;
    static constexpr unsigned char DIRECT_SEARCH    = 1 << 1;

    /**
     * search file path by using PATH
     * if cannot resolve path (file not found), return null.
     */
    const char *searchPath(const char *cmdName, unsigned char option = 0);

    void removePath(const char *cmdName);

    bool isCached(const char *cmdName) const;

    /**
     * clear all cache
     */
    void clear();

    /**
     * get begin iterator of map
     */
    CStringHashMap<std::string>::const_iterator cbegin() const {
        return this->map.cbegin();
    }

    /**
     * get end iterator of map
     */
    CStringHashMap<std::string>::const_iterator cend() const {
        return this->map.cend();
    }
};

using CStrBuffer = FlexBuffer<char *>;

// for exception handling
struct DSExcepton {};

} // namespace ydsh

using namespace ydsh;

struct DSState {
private:
    TypePool pool;
    SymbolTable symbolTable;

    /**
     * must be Boolean_Object
     */
    DSValue trueObj;

    /**
     * must be Boolean_Object
     */
    DSValue falseObj;

    /**
     * must be String_Object
     */
    DSValue emptyStrObj;

    /**
     * for pseudo object allocation (used for builtin constructor call)
     */
    DSValue dummy;

    /**
     * if not null ptr, thrown exception.
     */
    DSValue thrownObject;

    /**
     * contains operand, global variable(may be function) or local variable.
     *
     * stack grow ==>
     * +--------+   +--------+-------+   +-------+
     * | gvar 1 | ~ | gvar N | var 1 | ~ | var N | ~
     * +--------+   +--------+-------+   +-------+
     * |   global variable   |   local variable  | operand stack
     *
     *
     * stack layout within callable
     *
     * stack grow ==>
     *   +-----------------+   +-----------------+-----------+   +-------+----+---------------+------------------+----------------+
     * ~ | var 1 (param 1) | ~ | var M (param M) | var M + 1 | ~ | var N | PC | stackTopIndex | stackBottomIndex | localVarOffset | ~
     *   +-----------------+   +-----------------+-----------+   +-------+----+---------------+------------------+----------------+
     *   |                           local variable                      |                 caller state                           | operand stack
     */
    DSValue *callStack;

    unsigned int callStackSize;

    static constexpr unsigned int DEFAULT_STACK_SIZE = 256;
    static constexpr unsigned int MAXIMUM_STACK_SIZE = 2 * 1024 * 1024;

    unsigned int globalVarSize;

    /**
     * initial value is 0. increment index before push
     */
    unsigned int stackTopIndex;

    /**
     * indicate lower limit of stack top index (bottom <= top)
     */
    unsigned int stackBottomIndex;

    /**
     * offset of current local variable index.
     * initial value is equivalent to globalVarSize.
     */
    unsigned int localVarOffset;

    /**
     * indicate the index of currently evaluating op code.
     */
    unsigned int pc_;

    /**
     * see DS_OPTION_ * macro.
     */
    flag32_set_t option;

    /**
     * for field splitting (read command, command substitution)
     */
    unsigned int IFS_index;

    /**
     * contains currently evaluating code.
     */
    std::vector<const DSCode *> codeStack_;

    /**
     * for caching object.
     * must be PipelineEvaluator object.
     */
    DSValue pipelineEvaluator;

    /**
     * cache searched result.
     */
    FilePathCache pathCache;

    TerminationHook terminationHook;

    unsigned int lineNum;

    /**
     * previously computed prompt (DSState_prompt() )
     */
    std::string prompt;

    DebugHook *hook;

    static std::string logicalWorkingDir;

    static const char *configRootDir;
    static const char *typeDefDir;

public:
    NON_COPYABLE(DSState);

    DSState();

    ~DSState();

    static std::string getConfigRootDir();
    static std::string getIfaceDir();

    static const char *getLogicalWorkingDir() {
        return logicalWorkingDir.c_str();
    }

    TypePool &getPool() {
        return this->pool;
    }

    SymbolTable &getSymbolTable() {
        return this->symbolTable;
    }

    const DSValue &getTrueObj() const {
        return this->trueObj;
    }

    const DSValue &getFalseObj() const {
        return this->falseObj;
    }

    const DSValue &getEmptyStrObj() const {
        return this->emptyStrObj;
    }

    DSValue &getPipeline() {
        return this->pipelineEvaluator;
    }

    int getExitStatus() const {
        return typeAs<Int_Object>(this->getGlobal(toIndex(BuiltinVarOffset::EXIT_STATUS)))->getValue();
    }

    /**
     * abort symbol table and TypePool when error happened
     */
    void recover(bool abortType = true);

    const DSValue &getDBus() {
        return this->getGlobal(toIndex(BuiltinVarOffset::DBUS));
    }

    flag32_set_t getOption() const {
        return this->option;
    }

    void setOption(flag32_set_t option) {
        this->option = option;
    }

    const DSValue &getThrownObject() const {
        return this->thrownObject;
    }

    void setThrownObject(DSValue &&value) {
        this->thrownObject = std::move(value);
    }

    unsigned int getStackTopIndex() {
        return this->stackTopIndex;
    }

    unsigned int getLocalVarOffset() {
        return this->localVarOffset;
    }

    unsigned int &pc() noexcept {
        return this->pc_;
    }


    /**
     * reserve global variable entry and set local variable offset.
     */
    void reserveGlobalVar(unsigned int size);

    /**
     * set stackTopIndex.
     * if this->localStackSize < size, expand callStack.
     */
    void reserveLocalVar(unsigned int size);

    /**
     * create and push error.
     */
    DSValue newError(DSType &errorType, std::string &&message);

    /**
     * pop stack top and store to thrownObject.
     */
    void throwException(DSValue &&except);

    /**
     * for internal error reporting.
     */
    void throwError(DSType &errorType, const char *message);

    void throwError(DSType &errorType, std::string &&message);

    /**
     * convert errno to SystemError.
     * errorNum must not be 0.
     * format message '%s: %s', message, strerror(errorNum)
     */
    void throwSystemError(int errorNum, std::string &&message);

    /**
     * get thrownObject and push to callStack
     */
    void loadThrownObject() {
        this->push(std::move(this->thrownObject));
    }

    void storeThrowObject() {
        this->thrownObject = this->pop();
    }

    /**
     * expand local stack size to stackTopIndex
     */
    void expandLocalStack();

    void clearOperandStack();

    /**
     * callable may be null, when call native method.
     */
    void windStackFrame(unsigned int stackTopOffset, unsigned int paramSize, const DSCode *code);

    void unwindStackFrame();

    void skipHeader();

    // operand manipulation
    void push(const DSValue &value) {
        if(++this->stackTopIndex >= this->callStackSize) {
            this->expandLocalStack();
        }
        this->callStack[this->stackTopIndex] = value;
    }

    void push(DSValue &&value) {
        if(++this->stackTopIndex >= this->callStackSize) {
            this->expandLocalStack();
        }
        this->callStack[this->stackTopIndex] = std::move(value);
    }

    DSValue pop() {
        return std::move(this->callStack[this->stackTopIndex--]);
    }

    void popNoReturn() {
        this->callStack[this->stackTopIndex--].reset();
    }

    const DSValue &peek() {
        return this->callStack[this->stackTopIndex];
    }

    void dup() {
        if(++this->stackTopIndex >= this->callStackSize) {
            this->expandLocalStack();
        }
        this->callStack[this->stackTopIndex] = this->callStack[this->stackTopIndex - 1];
    }

    void dup2() {
        this->stackTopIndex += 2;
        if(this->stackTopIndex >= this->callStackSize) {
            this->expandLocalStack();
        }
        this->callStack[this->stackTopIndex] = this->callStack[this->stackTopIndex - 2];
        this->callStack[this->stackTopIndex - 1] = this->callStack[this->stackTopIndex - 3];
    }

    void swap() {
        this->callStack[this->stackTopIndex].swap(this->callStack[this->stackTopIndex - 1]);
    }

    // variable manipulation
    void storeGlobal(unsigned int index) {
        this->callStack[index] = this->pop();
    }

    void loadGlobal(unsigned int index) {
        auto v(this->callStack[index]);
        this->push(std::move(v));   // callStack may be expanded.
    }

    void setGlobal(unsigned int index, const DSValue &obj) {
        this->callStack[index] = obj;
    }

    void setGlobal(unsigned int index, DSValue &&obj) {
        this->callStack[index] = std::move(obj);
    }

    const DSValue &getGlobal(unsigned int index) const {
        return this->callStack[index];
    }

    void storeLocal(unsigned int index) {
        this->callStack[this->localVarOffset + index] = this->pop();
    }

    void loadLocal(unsigned int index) {
        auto v(this->callStack[this->localVarOffset + index]); // callStack may be expanded.
        this->push(std::move(v));
    }

    void setLocal(unsigned int index, const DSValue &obj) {
        this->callStack[this->localVarOffset + index] = obj;
    }

    void setLocal(unsigned int index, DSValue &&obj) {
        this->callStack[this->localVarOffset + index] = std::move(obj);
    }

    const DSValue &getLocal(unsigned int index) {
        return this->callStack[this->localVarOffset + index];
    }

    // field manipulation

    void storeField(unsigned int index) {
        DSValue value(this->pop());
        this->pop()->getFieldTable()[index] = std::move(value);
    }

    /**
     * get field from stack top value.
     */
    void loadField(unsigned int index) {
        this->callStack[this->stackTopIndex] =
                this->callStack[this->stackTopIndex]->getFieldTable()[index];
    }

    std::vector<const DSCode *> &codeStack() noexcept {
        return this->codeStack_;
    }

    void applyFuncObject(unsigned int paramSize);

    /**
     * not directy use it
     *
     * @param index
     * @param paramSize
     */
    void callMethod(unsigned short index, unsigned short paramSize);

    /**
     * allocate new DSObject on stack top.
     * if type is builtin type, not allocate it.
     */
    void newDSObject(DSType *type);

    /**
     * not directly use it.
     * @param paramSize
     */
    void callConstructor(unsigned short paramSize);

    /**
     * invoke interface method.
     * constPoolIndex indicates the constant pool index of descriptor object.
     */
    void invokeMethod(unsigned short constPoolIndex);

    /**
     * invoke interface getter.
     * constPoolIndex indicates the constant pool index of descriptor object.
     */
    void invokeGetter(unsigned short constPoolIndex);

    /**
     * invoke interface setter.
     * constPoolIndex indicates the constant pool index of descriptor object.
     */
    void invokeSetter(unsigned short constPoolIndex);

    // some runtime api
    void fillInStackTrace(std::vector<StackTraceElement> &stackTrace);

    /**
     * reset call stack, local var offset, thrown object.
     */
    void resetState();

    /**
     * change current working directory and update OLDPWD, PWD.
     * if dest is null, do nothing and return true.
     */
    bool changeWorkingDir(const char *dest, const bool useLogical);

    const char *getIFS();

    void updateExitStatus(unsigned int status) {
        unsigned int index = toIndex(BuiltinVarOffset::EXIT_STATUS);
        this->setGlobal(index, DSValue::create<Int_Object>(this->pool.getInt32Type(), status));
    }

    void exitShell(unsigned int status);

    FilePathCache &getPathCache() {
        return this->pathCache;
    }

    /**
     * if not found, return null
     */
    FuncObject *lookupUserDefinedCommand(const char *commandName);

    /**
     * obj must indicate user-defined command.
     */
    void callUserDefinedCommand(const FuncObject *obj, DSValue *argv);

    /**
     * n is 1 or 2
     */
    void interpretPromptString(const char *ps, std::string &output) const;

    /**
     * after fork, reset signal setting in child process.
     */
    pid_t xfork();

    /**
     * waitpid wrapper.
     */
    pid_t xwaitpid(pid_t pid, int &status, int options);

    /**
     * return completion candidates.
     * line must be terminate newline.
     */
    CStrBuffer completeLine(const std::string &line);

    // for command invocation

    /**
     * first element of argv is command name.
     * last element of argv is null.
     * if execute exit command, throw InternalError.
     */
    void execBuiltinCommand(char *const argv[]);

    void setTerminationHook(TerminationHook hook) {
        this->terminationHook = hook;
    }

    TerminationHook getTerminationHook() const {
        return this->terminationHook;
    }

    void setLineNum(unsigned int lineNum) {
        this->lineNum = lineNum;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    std::string &refPrompt() {
        return this->prompt;
    }

    DebugHook *getHook() const {
        return this->hook;
    }

    void setHook(DebugHook *hook) {
        this->hook = hook;
    }
};

/**
 * entry point
 */
bool vmEval(DSState &state, CompiledCode &code);

/**
 * call method.
 * @param state
 * @param handle
 * must not be null
 * @param recv
 * @param args
 * @return
 * return value of method (if no return value, return null).
 */
DSValue callMethod(DSState &state, const MethodHandle *handle, DSValue &&recv, std::vector<DSValue> &&args);

// some system util

/**
 * path is starts with tilde.
 */
std::string expandTilde(const char *path);

namespace ydsh {

struct DebugHook {
    virtual void vmFetchHook(DSState &st, OpCode op) = 0;
};

} // namespace ydsh

#endif //YDSH_STATE_H
