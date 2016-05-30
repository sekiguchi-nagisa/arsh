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

#ifndef YDSH_CORE_CONTEXT_H
#define YDSH_CORE_CONTEXT_H

#include <unistd.h>

#include <vector>
#include <iostream>

#include <ydsh/ydsh.h>

#include "../ast/node.h"
#include "object.h"
#include "symbol_table.h"
#include "proc.h"
#include "../misc/buffer.hpp"

namespace ydsh {

enum class EvalStatus : unsigned int {
    SUCCESS,
    BREAK,
    CONTINUE,
    THROW,
    RETURN,
    REMOVE,
};

/**
 * enum order is corresponding to builtin variable declaration order.
 */
enum class BuiltinVarOffset : unsigned int {
    DBUS,           // DBus
    OSTYPE,         // OSTYPE (utsname.sysname)
    VERSION,        // YDSH_VERSION (equivalent to ps_intrp '\V')
    REPLY,          // REPLY (for read command)
    REPLY_VAR,      // reply (fo read command)
    ARGS,           // @
    ARGS_SIZE,      // #
    EXIT_STATUS,    // ?
    PID,            // $
    POS_0,          // 0 (for script name)
    POS_1,          // 1 (for argument)
    /*POS_2, POS_3, POS_4, POS_5, POS_6, POS_7, POS_8, POS_9, */
};

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

using CStrBuffer = misc::FlexBuffer<char *>;

class RuntimeContext {
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
    DSValue *localStack;

    unsigned int localStackSize;

    static constexpr unsigned int DEFAULT_LOCAL_SIZE = 256;
    static constexpr unsigned int MAXIMUM_LOCAL_SIZE = 2 * 1024 * 1024;

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
     */
    unsigned int localVarOffset;

    /**
     * indicate the index of currently evaluating op code.
     */
    unsigned int pc_;

    /**
     * if true, enable assertion.
     */
    bool assertion;

    /**
     * if true, print stack trace of builtin exit command.
     */
    bool traceExit;

    /**
     * for string cast
     */
    MethodHandle *handle_STR;

    /**
     * for field splitting (read command, command substitution)
     */
    unsigned int IFS_index;

    /**
     * contains currently evaluating callable.
     */
    std::vector<const Callable *> callableStack_;

    /**
     * for caching object.
     * must be PipelineEvaluator object.
     */
    DSValue pipelineEvaluator;

    /**
     * contains user defined command node.
     * must delete it's contents
     */
    CStringHashMap<UserDefinedCmdNode *> udcMap;

    /**
     * cache searched result.
     */
    FilePathCache pathCache;

    TerminationHook terminationHook;

    static std::string logicalWorkingDir;

    static const char *configRootDir;
    static const char *typeDefDir;

public:
    NON_COPYABLE(RuntimeContext);

    RuntimeContext();

    ~RuntimeContext();

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

    unsigned int getBuiltinVarIndex(BuiltinVarOffset offset) const {
        return static_cast<unsigned int>(offset);
    }

    const DSValue &getScriptName() {
        return this->getGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::POS_0));
    }

    const DSValue &getScriptArgs() {
        return this->getGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::ARGS));
    }

    void addScriptArg(const char *arg);

    /**
     * clear current script arg
     */
    void initScriptArg();

    /**
     * set argument to positional parameter
     */
    void finalizeScriptArg();

    int getExitStatus() const {
        return typeAs<Int_Object>(this->getGlobal(
                this->getBuiltinVarIndex(BuiltinVarOffset::EXIT_STATUS)))->getValue();
    }

    void updateScriptName(const char *name);

    const DSValue &getDBus() {
        return this->getGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::DBUS));
    }

    bool isAssertion() {
        return this->assertion;
    }

    void setAssertion(bool assertion) {
        this->assertion = assertion;
    }

    bool isTraceExit() const {
        return this->traceExit;
    }

    void setTraceExit(bool traceExit) {
        this->traceExit = traceExit;
    }

    const DSValue &getThrownObject() {
        return this->thrownObject;
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
     * if this->localStackSize < size, expand localStack.
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
     * create StackOverflowError and throw NativeMethodError
     */
    void raiseCircularReferenceError();

    /**
     * get thrownObject and push to localStack
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

    void unwindOperandStack();

    void saveAndSetStackState(unsigned int stackTopOffset,
                              unsigned int paramSize, unsigned int maxVarSize);
    void restoreStackState();

    // operand manipulation
    void push(const DSValue &value) {
        if(++this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack();
        }
        this->localStack[this->stackTopIndex] = value;
    }

    void push(DSValue &&value) {
        if(++this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack();
        }
        this->localStack[this->stackTopIndex] = std::move(value);
    }

    DSValue pop() {
        return std::move(this->localStack[this->stackTopIndex--]);
    }

    void popNoReturn() {
        this->localStack[this->stackTopIndex--].reset();
    }

    const DSValue &peek() {
        return this->localStack[this->stackTopIndex];
    }

    void dup() {
        if(++this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack();
        }
        this->localStack[this->stackTopIndex] = this->localStack[this->stackTopIndex - 1];
    }

    void dup2() {
        this->stackTopIndex += 2;
        if(this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack();
        }
        this->localStack[this->stackTopIndex] = this->localStack[this->stackTopIndex - 2];
        this->localStack[this->stackTopIndex - 1] = this->localStack[this->stackTopIndex - 3];
    }

    void swap() {
        this->localStack[this->stackTopIndex].swap(this->localStack[this->stackTopIndex - 1]);
    }

    // variable manipulation
    void storeGlobal(unsigned int index) {
        this->localStack[index] = this->pop();
    }

    void loadGlobal(unsigned int index) {
        auto v(this->localStack[index]);
        this->push(std::move(v));   // localStack may be expanded.
    }

    void setGlobal(unsigned int index, const DSValue &obj) {
        this->localStack[index] = obj;
    }

    void setGlobal(unsigned int index, DSValue &&obj) {
        this->localStack[index] = std::move(obj);
    }

    const DSValue &getGlobal(unsigned int index) const {
        return this->localStack[index];
    }

    void storeLocal(unsigned int index) {
        this->localStack[this->localVarOffset + index] = this->pop();
    }

    void loadLocal(unsigned int index) {
        auto v(this->localStack[this->localVarOffset + index]); // localStack may be expanded.
        this->push(std::move(v));
    }

    void setLocal(unsigned int index, DSValue &&obj) {
        this->localStack[this->localVarOffset + index] = std::move(obj);
    }

    const DSValue &getLocal(unsigned int index) {
        return this->localStack[this->localVarOffset + index];
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
        this->localStack[this->stackTopIndex] =
                this->localStack[this->stackTopIndex]->getFieldTable()[index];
    }

    /**
     * dup stack top value and get field from it.
     */
    void dupAndLoadField(unsigned int index) {
        auto v(this->peek()->getFieldTable()[index]);
        this->push(std::move(v));
    }

    std::vector<const Callable *> &callableStack() noexcept {
        return this->callableStack_;
    }

    void applyFuncObject(unsigned int paramSize);

    void callMethod(unsigned short index, unsigned short paramSize);
    void callToString();

    /**
     * allocate new DSObject on stack top.
     * if type is builtin type, not allocate it.
     */
    void newDSObject(DSType *type);

    bool callConstructor(unsigned short paramSize);

    void handleUncaughtException(DSValue &&except);

    // some runtime api
    void fillInStackTrace(std::vector<StackTraceElement> &stackTrace);

    void printStackTop(DSType *stackTopType);

    void checkCast(DSType *targetType);


    void instanceOf(DSType *targetType);

    void checkAssertion();

    void importEnv(bool hasDefault);
    void storeEnv();
    void loadEnv();

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

    void updateExitStatus(unsigned int status);

    void exitShell(unsigned int status);

    FilePathCache &getPathCache() {
        return this->pathCache;
    }

    const CStringHashMap<UserDefinedCmdNode *> &getUdcMap() const {
        return this->udcMap;
    }

    /**
     * if not found, return null
     */
    UserDefinedCmdNode *lookupUserDefinedCommand(const char *commandName);

    /**
     * must call in child process.
     * argv represents array.
     * the first element of ptr is command name(equivalent to node->getCommandName()).
     * the last element of ptr is null.
     */
    int execUserDefinedCommand(UserDefinedCmdNode *node, DSValue *argv);

    /**
     * n is 1 or 2
     */
    void interpretPromptString(const char *ps, std::string &output);

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

    void pushNewPipeline();

    /**
     * after call pipeline, pop stack.
     */
    PipelineEvaluator &activePipeline();

    /**
     * stack top value must be String_Object and it represents command name.
     */
    void openProc();

    void closeProc();

    /**
     * stack top value must be String_Object or Array_Object.
     */
    void addArg(bool skipEmptyString);

    /**
     * stack top value must be String_Object.
     */
    void addRedirOption(RedirectOP op);

    void callPipeline();

    void setTerminationHook(TerminationHook hook) {
        this->terminationHook = hook;
    }
};

// some system util

/**
 * path is starts with tilde.
 */
std::string expandTilde(const char *path);

/**
 * after fork, reset signal setting in child process.
 */
pid_t xfork();

// for exception handling
struct DSExcepton {};

} // namespace ydsh

#endif //YDSH_CORE_CONTEXT_H
