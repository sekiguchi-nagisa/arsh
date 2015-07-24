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

#ifndef CORE_RUNTIMECONTEXT_H_
#define CORE_RUNTIMECONTEXT_H_

#include <vector>
#include <iostream>

#include "DSObject.h"
#include "SymbolTable.h"
#include "ProcInvoker.h"

namespace ydsh {
namespace core {

enum class EvalStatus : unsigned int {
    SUCCESS,
    BREAK,
    CONTINUE,
    THROW,
    RETURN,
    REMOVE,
};

class RuntimeContext {
private:
    TypePool pool;
    SymbolTable symbolTable;

    std::shared_ptr<Boolean_Object> trueObj;
    std::shared_ptr<Boolean_Object> falseObj;

    /**
     * for pseudo object allocation (used for builtin constructor call)
     */
    std::shared_ptr<DSObject> dummy;

    /**
     * represent shell or shell script name. ($0)
     */
    std::shared_ptr<String_Object> scriptName;

    /**
     * contains script argument(exclude script name). ($@)
     */
    std::shared_ptr<Array_Object> scriptArgs;

    /**
     * contains exit status of most recent executed process. ($?)
     */
    std::shared_ptr<Int_Object> exitStatus;

    /**
     * management object for dbus related function
     */
    std::shared_ptr<DBus_Object> dbus;

    /**
     * contains global variables(or function)
     */
    std::shared_ptr<DSObject> *globalVarTable;

    /**
     * size of global variable table.
     */
    unsigned int tableSize;

    /**
     * if not null ptr, thrown exception.
     */
    std::shared_ptr<DSObject> thrownObject;

    /**
     * contains operand or local variable
     */
    std::shared_ptr<DSObject> *localStack;

    unsigned int localStackSize;

    /**
     * initial value is 0. increment index before push
     */
    unsigned int stackTopIndex;

    /**
     * offset of current local variable index.
     */
    unsigned int localVarOffset;

    /**
     * for function call. save localVarOffset.
     */
    std::vector<unsigned int> offsetStack;

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
     * for string interpolation
     */
    MethodHandle *handle_INTERP;

    /**
     * for command argument
     */
    MethodHandle *handle_CMD_ARG;

    /**
     * for error reporting
     */
    MethodHandle *handle_bt;

    /**
     * for builtin cd command
     */
    FieldHandle *handle_OLDPWD;

    /**
     * for builtin cd command
     */
    FieldHandle *handle_PWD;

    static const unsigned int defaultFileNameIndex = 0;
    std::vector<std::string> readFiles;

    /**
     * contains currently evaluating FunctionNode or RootNode
     */
    std::vector<ast::Node *> funcContextStack;

    /**
     * contains line number and funcContextStack index.
     */
    std::vector<unsigned long> callStack;

    ProcInvoker procInvoker;

    static const char *configRootDir;
    static const char *typeDefDir;

public:
    RuntimeContext();

    ~RuntimeContext();

    static std::string getConfigRootDir();
    static std::string getIfaceDir();

    TypePool &getPool() {
        return this->pool;
    }

    SymbolTable &getSymbolTable() {
        return this->symbolTable;
    }

    const std::shared_ptr<Boolean_Object> &getTrueObj() {
        return this->trueObj;
    }

    const std::shared_ptr<Boolean_Object> &getFalseObj() {
        return this->falseObj;
    }

    const std::shared_ptr<String_Object> &getScriptName() {
        return this->scriptName;
    }

    const std::shared_ptr<Array_Object> &getScriptArgs() {
        return this->scriptArgs;
    }

    void addScriptArg(const char *arg);

    const std::shared_ptr<Int_Object> &getExitStatus() {
        return this->exitStatus;
    }

    void updateScriptName(const char *name);

    const std::shared_ptr<DBus_Object> &getDBus() {
        return this->dbus;
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

    const std::shared_ptr<DSObject> &getThrownObject() {
        return this->thrownObject;
    }

    unsigned int getStackTopIndex() {
        return this->stackTopIndex;
    }

    unsigned int getLocalVarOffset() {
        return this->localVarOffset;
    }


    /**
     * if this->tableSize < size, expand globalVarTable.
     */
    void reserveGlobalVar(unsigned int size);

    /**
     * set stackTopIndex.
     * if this->localStackSize < size, expand localStack.
     */
    void reserveLocalVar(unsigned int size);

    /**
     * for internal error reporting.
     */
    void throwError(DSType *errorType, const char *message);

    void throwError(DSType *errorType, std::string &&message);

    /**
     * pop and set to throwObject
     */
    void storeThrowObject() {
        this->thrownObject = std::move(this->pop());
    }

    /**
     * get thrownObject and push to localStack
     */
    void loadThrownObject() {
        this->push(std::move(this->thrownObject));
    }

    void expandLocalStack(unsigned int needSize);

    void saveAndSetOffset(unsigned int newOffset) {
        this->offsetStack.push_back(this->localVarOffset);
        this->localVarOffset = newOffset;
    }

    void restoreOffset() {
        this->localVarOffset = this->offsetStack.back();
        this->offsetStack.pop_back();
    }

    // operand manipulation
    void push(const std::shared_ptr<DSObject> &value) {
        if(++this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack(this->stackTopIndex);
        }
        this->localStack[this->stackTopIndex] = value;
    }

    void push(std::shared_ptr<DSObject> &&value) {
        if(++this->stackTopIndex >= this->localStackSize) {
            this->expandLocalStack(this->stackTopIndex);
        }
        this->localStack[this->stackTopIndex] = std::move(value);
    }

    std::shared_ptr<DSObject> pop() {
        return std::move(this->localStack[this->stackTopIndex--]);
    }

    void popNoReturn() {
        this->localStack[this->stackTopIndex--].reset();
    }

    const std::shared_ptr<DSObject> &peek() {
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
        this->globalVarTable[index] = std::move(this->pop());
    }

    void storeGlobal(unsigned int index, const std::shared_ptr<DSObject> &obj) {
        this->globalVarTable[index] = obj;
    }

    void loadGlobal(unsigned int index) {
        this->push(this->globalVarTable[index]);
    }

    void storeLocal(unsigned int index) {
        this->localStack[this->localVarOffset + index] = std::move(this->pop());
    }

    void storeLocal(unsigned int index, std::shared_ptr<DSObject> &&obj) {
        this->localStack[this->localVarOffset + index] = std::move(obj);
    }

    void loadLocal(unsigned int index) {
        this->push(this->localStack[this->localVarOffset + index]);
    }

    const std::shared_ptr<DSObject> &getLocal(unsigned int index) {
        return this->localStack[this->localVarOffset + index];
    }

    // field manipulation

    void storeField(unsigned int index) {
        std::shared_ptr<DSObject> value(this->pop());
        this->pop()->getFieldTable()[index] = std::move(value);
    }

    EvalStatus storeField(DSType *recvType, const std::string &fieldName, DSType *fieldType) {
        bool status = TYPE_AS(ProxyObject, this->localStack[this->stackTopIndex - 1])->
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
        bool status = TYPE_AS(ProxyObject, this->pop())->
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
        bool status = TYPE_AS(ProxyObject, this->localStack[this->stackTopIndex])->
                invokeGetter(*this, recvType, fieldName, fieldType);
        return status ? EvalStatus::SUCCESS : EvalStatus::THROW;
    }

    void pushCallFrame(unsigned int lineNum) {
        unsigned long index = ((unsigned long) (this->funcContextStack.size() - 1)) << 32;
        this->callStack.push_back(index | (unsigned long) lineNum);
    }

    void popCallFrame() {
        this->callStack.pop_back();
    }

    EvalStatus applyFuncObject(unsigned int lineNum, bool returnTypeIsVoid, unsigned int paramSize);
    EvalStatus callMethod(unsigned int lineNum, const std::string &methodName, MethodHandle *handle);

    /**
     * allocate new DSObject on stack top.
     * if type is builtin type, not allocate it.
     */
    void newDSObject(DSType *type);

    EvalStatus callConstructor(unsigned int lineNum, unsigned int paramSize);

    /**
     * cast stack top value to String
     */
    EvalStatus toString(unsigned int lineNum);

    /**
     * call __INTERP__
     */
    EvalStatus toInterp(unsigned int lineNum);

    /**
     * call __CMD_ARG__
     */
    EvalStatus toCmdArg(unsigned int lineNum);

    /**
     * report thrown object error message.
     * after error reporting, clear thrown object
     */
    void reportError();


    // some runtime api
    void fillInStackTrace(std::vector<StackTraceElement> &stackTrace);

    void printStackTop(DSType *stackTopType);

    bool checkCast(unsigned int lineNum, DSType *targetType);

    void instanceOf(DSType *targetType);

    EvalStatus checkAssertion(unsigned int lineNum);

    /**
     * get environment variable and set to local variable
     */
    void importEnv(const std::string &envName, unsigned int index, bool isGlobal);

    /**
     * put stack top value to environment variable.
     */
    void exportEnv(const std::string &envName, unsigned int index, bool isGlobal);

    bool checkZeroDiv(int right);
    bool checkZeroDiv(double right);
    bool checkZeroMod(int right);

    void throwOutOfRangeError(std::string &&message) {
        this->throwError(this->pool.getOutOfRangeErrorType(), std::move(message));
    }

    void pushFuncContext(ast::Node *node) {
        this->funcContextStack.push_back(node);
    }

    void popFuncContext() {
        this->funcContextStack.pop_back();
    }

    void clearCallStack() {
        this->funcContextStack.clear();
        this->callStack.clear();
    }

    /**
     * update OLDPWD and PWD
     */
    void updateWorkingDir(bool OLDPWD_only);

    /**
     * register source name to readFiles.
     * return pointer of added name.
     * sourceName is null, if source is stdin.
     */
    const char *registerSourceName(const char *sourceName);

    void updateExitStatus(unsigned int status);

    void exitShell(unsigned int status);

    void registerSpecialChar(const std::string &varName, unsigned int index);
    unsigned int getSpecialCharIndex(const char *varName);

    ProcInvoker &getProcInvoker() {
        return this->procInvoker;
    }
};

} // namespace core
} // namespace ydsh

#endif /* CORE_RUNTIMECONTEXT_H_ */
