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

#include "../ast/node.h"
#include "object.h"
#include "symbol_table.h"
#include "proc.h"
#include "../misc/buffer.hpp"

namespace ydsh {
namespace core {

using namespace ydsh::ast;
using namespace ydsh::ast;

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
     * +--------------+   +--------------+-------------+   +-------------+
     * | global var 1 | ~ | global var N | local var 1 | ~ | local var N | ~
     * +--------------+   +--------------+-------------+   +-------------+
     * |          global variable        |         local variable        | operand stack
     */
    DSValue *localStack;

    unsigned int localStackSize;

    static constexpr unsigned int DEFAULT_LOCAL_SIZE = 256;

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
     * if true, print top level evaluated value.
     */
    bool toplevelPrinting;

    /**
     * if true, enable assertion.
     */
    bool assertion;

    /**
     * for string cast
     */
    MethodHandle *handle_STR;

    /**
     * for error reporting
     */
    MethodHandle *handle_bt;

    /**
     * for field splitting (read command, command substitution)
     */
    unsigned int IFS_index;

    /**
     * contains currently evaluating CallableNode
     */
    std::vector<CallableNode *> callableContextStack;

    /**
     * contains startPos and callableContextStack index.
     */
    std::vector<unsigned long> callStack;

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
    void finalizeScritArg();

    const DSValue &getExitStatus() {
        return this->getGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::EXIT_STATUS));
    }

    void updateScriptName(const char *name);

    const DSValue &getDBus() {
        return this->getGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::DBUS));
    }

    bool isToplevelPrinting() {
        return this->toplevelPrinting;
    }

    void setToplevelPrinting(bool print) {
        this->toplevelPrinting = print;
    }

    bool isAssertion() {
        return this->assertion;
    }

    void setAssertion(bool assertion) {
        this->assertion = assertion;
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


    /**
     * revserve global variable entry and set local variable offset.
     */
    void reserveGlobalVar(unsigned int size);

    /**
     * set stackTopIndex.
     * if this->localStackSize < size, expand localStack.
     */
    void reserveLocalVar(unsigned int size);

    /**
     * pop stack top and store to thrownObject.
     */
    void throwError();

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

    void expandLocalStack(unsigned int needSize);

    void unwindOperandStack();

    void saveAndSetStackState(unsigned int stackTopOffset,
                              unsigned int paramSize, unsigned int maxVarSize);
    void restoreStackState();

    // operand manipulation
    void push(const DSValue &value) {
        if(++this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack(this->stackTopIndex);
        }
        this->localStack[this->stackTopIndex] = value;
    }

    void push(DSValue &&value) {
        if(++this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack(this->stackTopIndex);
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
            this->expandLocalStack(this->stackTopIndex);
        }
        this->localStack[this->stackTopIndex] = this->localStack[this->stackTopIndex - 1];
    }

    void dup2() {
        this->stackTopIndex += 2;
        if(this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack(this->stackTopIndex);
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
        this->push(this->localStack[index]);
    }

    void setGlobal(unsigned int index, const DSValue &obj) {
        this->localStack[index] = obj;
    }

    void setGlobal(unsigned int index, DSValue &&obj) {
        this->localStack[index] = std::move(obj);
    }

    const DSValue &getGlobal(unsigned int index) {
        return this->localStack[index];
    }

    void storeLocal(unsigned int index) {
        this->localStack[this->localVarOffset + index] = this->pop();
    }

    void loadLocal(unsigned int index) {
        this->push(this->localStack[this->localVarOffset + index]);
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

    EvalStatus storeField(DSType *recvType, const std::string &fieldName, DSType *fieldType) {
        bool status = typeAs<ProxyObject>(this->localStack[this->stackTopIndex - 1])->
                invokeSetter(*this, recvType, fieldName, fieldType);
        // pop receiver
        this->popNoReturn();
        return status ? EvalStatus::SUCCESS : EvalStatus::THROW;
    }

    /**
     * get field from stack top value.
     */
    void loadField(unsigned int index) {
        this->localStack[this->stackTopIndex] =
                this->localStack[this->stackTopIndex]->getFieldTable()[index];
    }

    EvalStatus loadField(DSType *recvType, const std::string &fieldName, DSType *fieldType) {
        bool status = typeAs<ProxyObject>(this->pop())->
                invokeGetter(*this, recvType, fieldName, fieldType);
        return status ? EvalStatus::SUCCESS : EvalStatus::THROW;
    }

    /**
     * dup stack top value and get field from it.
     */
    void dupAndLoadField(unsigned int index) {
        this->push(this->peek()->getFieldTable()[index]);
    }

    EvalStatus dupAndLoadField(DSType *recvType, const std::string &fieldName, DSType *fieldType) {
        bool status = typeAs<ProxyObject>(this->localStack[this->stackTopIndex])->
                invokeGetter(*this, recvType, fieldName, fieldType);
        return status ? EvalStatus::SUCCESS : EvalStatus::THROW;
    }

    void pushCallFrame(unsigned int startPos) {
        unsigned long index = (this->callableContextStack.size() - 1) << 32;
        this->callStack.push_back(index | (unsigned long) startPos);
    }

    void popCallFrame() {
        this->callStack.pop_back();
    }

    EvalStatus applyFuncObject(unsigned int startPos, bool returnTypeIsVoid, unsigned int paramSize);
    EvalStatus callMethod(unsigned int startPos, const std::string &methodName, MethodHandle *handle);

    /**
     * allocate new DSObject on stack top.
     * if type is builtin type, not allocate it.
     */
    void newDSObject(DSType *type);

    EvalStatus callConstructor(unsigned int startPos, unsigned int paramSize);

    /**
     * cast stack top value to String
     */
    EvalStatus toString(unsigned int startPos);

    /**
     * report thrown object error message.
     * after error reporting, clear thrown object
     */
    void reportError();


    // some runtime api
    void fillInStackTrace(std::vector<StackTraceElement> &stackTrace);

    void printStackTop(DSType *stackTopType);

    bool checkCast(unsigned int startPos, DSType *targetType);

    void instanceOf(DSType *targetType);

    EvalStatus checkAssertion(unsigned int startPos);

    /**
     * get environment variable and set to local variable
     */
    EvalStatus importEnv(unsigned int startPos, const std::string &envName,
                         unsigned int index, bool isGlobal, bool hasDefault);

    /**
     * put stack top value to environment variable.
     */
    void exportEnv(const std::string &envName, unsigned int index, bool isGlobal);

    /**
     * update environmental variable
     */
    void updateEnv(unsigned int index, bool isGlobal);

    void loadEnv(unsigned int index, bool isGlobal);

    void pushFuncContext(CallableNode *node) {
        this->callableContextStack.push_back(node);
    }

    void popFuncContext() {
        this->callableContextStack.pop_back();
    }

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

    void addUserDefinedCommand(UserDefinedCmdNode *node);

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

    EvalStatus callPipedCommand(unsigned int startPos);
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

// for internal status reporting
struct InternalError {};

// for native method exception (ex. StackOverflow)
struct NativeMethodError {};

} // namespace core
} // namespace ydsh

#endif //YDSH_CORE_CONTEXT_H
