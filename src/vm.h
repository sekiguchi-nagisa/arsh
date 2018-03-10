/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
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

#ifndef YDSH_VM_H
#define YDSH_VM_H

#include <vector>
#include <chrono>
#include <cstdio>

#include <ydsh/ydsh.h>

#include "object.h"
#include "symbol_table.h"
#include "signals.h"
#include "core.h"
#include "job.h"
#include "misc/buffer.hpp"
#include "misc/noncopyable.h"

using namespace ydsh;

class SignalVector {
private:
    /**
     * pair.second must be FuncObject
     */
    std::vector<std::pair<int, DSValue>> data;

public:
    SignalVector() = default;
    ~SignalVector() = default;

    /**
     * if func is null, delete handler.
     * @param sigNum
     * @param value
     * must be FuncObject
     * may be null
     */
    void insertOrUpdate(int sigNum, const DSValue &value);

    /**
     *
     * @param sigNum
     * @return
     * if not found, return null obj.
     */
    DSValue lookup(int sigNum) const;

    const std::vector<std::pair<int, DSValue>> &getData() const {
        return this->data;
    };
};

struct ControlFrame {
    const DSCode *code;

    unsigned int stackTopIndex;

    unsigned int stackBottomIndex;

    unsigned int localVarOffset;

    unsigned int pc;
};

struct DSState {
    SymbolTable symbolTable;

    /**
     * must be Boolean_Object
     */
    const DSValue trueObj;

    /**
     * must be Boolean_Object
     */
    const DSValue falseObj;

    /**
     * must be String_Object
     */
    const DSValue emptyStrObj;

    const DSValue emptyFDObj;

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

    unsigned int callStackSize{DEFAULT_STACK_SIZE};

    static constexpr unsigned int DEFAULT_STACK_SIZE = 256;
    static constexpr unsigned int MAX_CONTROL_STACK_SIZE = 1024;

    unsigned int globalVarSize{0};

    /**
     * initial value is 0. increment index before push
     */
    unsigned int stackTopIndex{0};

    /**
     * indicate lower limit of stack top index (bottom <= top)
     */
    unsigned int stackBottomIndex{0};

    /**
     * offset of current local variable index.
     * initial value is equivalent to globalVarSize.
     */
    unsigned int localVarOffset{0};

    /**
     * indicate the index of currently evaluating op code.
     */
    unsigned int pc_{0};

    /**
     * currently executed code
     */
    const DSCode *code{nullptr};

    unsigned short option{DS_OPTION_ASSERT};

    DSExecMode execMode{DS_EXEC_MODE_NORMAL};

    DumpTarget dumpTarget;

    std::vector<ControlFrame> controlStack;

    /**
     * cache searched result.
     */
    FilePathCache pathCache;

    unsigned int lineNum{1};

    /**
     * previously computed prompt (DSState_prompt() )
     */
    std::string prompt;

    VMHook *hook{nullptr};

    std::string logicalWorkingDir;

    decltype(std::chrono::system_clock::now()) baseTime;

    unsigned int termHookIndex{0};

    DSHistory history;

    SignalVector sigVector;

    JobTable jobTable;

    Job foreground;

    static constexpr flag32_t VM_EVENT_HOOK   = 1 << 0;
    static constexpr flag32_t VM_EVENT_SIGNAL = 1 << 1;
    static constexpr flag32_t VM_EVENT_MASK   = 1 << 2;

    static flag32_set_t eventDesc;

    static unsigned int pendingSigIndex;

    static SigSet pendingSigSet;

    NON_COPYABLE(DSState);

    DSState();

    ~DSState() {
        delete[] this->callStack;

        for(unsigned int i = 0; i < this->history.size; i++) {
            free(this->history.data[i]);
        }
        free(this->history.data);
    }

    int getExitStatus() const {
        return typeAs<Int_Object>(this->getGlobal(toIndex(BuiltinVarOffset::EXIT_STATUS)))->getValue();
    }

    /**
     * abort symbol table and TypePool when error happened
     */
    void recover(bool abortType = true) {
        this->symbolTable.abort(abortType);
    }

    const DSValue &getThrownObject() const {
        return this->thrownObject;
    }

    unsigned int &pc() noexcept {
        return this->pc_;
    }

    /**
     * create error.
     */
    DSValue newError(DSType &errorType, std::string &&message) const {
        return Error_Object::newError(*this, errorType, DSValue::create<String_Object>(
                this->symbolTable.getStringType(), std::move(message)));
    }

    /**
     * pop stack top and store to thrownObject.
     */
    [[noreturn]]
    void throwException(DSValue &&except);

    /**
     * for exception reporting
     * @param except
     */
    void setThrownObject(DSValue &&except) {
        this->thrownObject = std::move(except);
    }

    /**
     * get thrownObject and push to callStack
     */
    void loadThrownObject() {
        this->push(std::move(this->thrownObject));
    }

    void storeThrowObject() {
        this->setThrownObject(this->pop());
    }

    // operand manipulation
    void push(const DSValue &value) {
        this->push(DSValue(value));
    }

    void push(DSValue &&value) {
        this->callStack[++this->stackTopIndex] = std::move(value);
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
        auto v = this->callStack[this->stackTopIndex];
        this->callStack[++this->stackTopIndex] = std::move(v);
    }

    void dup2() {
        auto v1 = this->callStack[this->stackTopIndex - 1];
        auto v2 = this->callStack[this->stackTopIndex];
        this->callStack[++this->stackTopIndex] = std::move(v1);
        this->callStack[++this->stackTopIndex] = std::move(v2);
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
        this->setGlobal(index, DSValue(obj));
    }

    void setGlobal(unsigned int index, DSValue &&obj) {
        this->callStack[index] = std::move(obj);
    }

    const DSValue &getGlobal(unsigned int index) const {
        return this->callStack[index];
    }

    void storeLocal(unsigned char index) {
        this->callStack[this->localVarOffset + index] = this->pop();
    }

    void loadLocal(unsigned char index) {
        auto v(this->callStack[this->localVarOffset + index]); // callStack may be expanded.
        this->push(std::move(v));
    }

    void setLocal(unsigned char index, const DSValue &obj) {
        setLocal(index, DSValue(obj));
    }

    void setLocal(unsigned char index, DSValue &&obj) {
        this->callStack[this->localVarOffset + index] = std::move(obj);
    }

    const DSValue &getLocal(unsigned char index) const {
        return this->callStack[this->localVarOffset + index];
    }

    DSValue moveLocal(unsigned char index) {
        return std::move(this->callStack[this->localVarOffset + index]);
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

    /**
     * reset call stack, local var offset, thrown object.
     */
    void resetState() {
        this->localVarOffset = this->globalVarSize;
        this->thrownObject.reset();
    }

    void updateExitStatus(unsigned int status) {
        unsigned int index = toIndex(BuiltinVarOffset::EXIT_STATUS);
        this->setGlobal(index, DSValue::create<Int_Object>(this->symbolTable.getInt32Type(), status));
    }

    void pushExitStatus(int status) {
        this->updateExitStatus(status);
        this->push(status == 0 ? this->trueObj : this->falseObj);
    }

    bool isJobControl() const {
        return hasFlag(this->option, DS_OPTION_JOB_CONTROL);
    }

    bool isRootShell() const {
        int shellpid = typeAs<Int_Object>(this->getGlobal(toIndex(BuiltinVarOffset::SHELL_PID)))->getValue();
        return shellpid == getpid();
    }

    bool isForeground() const {
        return this->isJobControl() && this->isRootShell();
    }

    void setVMHook(VMHook *hook) {
        this->hook = hook;
        if(hook != nullptr) {
            setFlag(eventDesc, VM_EVENT_HOOK);
        } else {
            unsetFlag(eventDesc, VM_EVENT_HOOK);
        }
    }

    enum class UnsafeSigOp {
        DFL,
        IGN,
        SET,
    };

    /**
     * unsafe op.
     * @param sigNum
     * @param op
     * @param handler
     * may be nullptr
     * @param setSIGCHLD
     * if true, set signal handler of SIGCHLD
     */
    void installSignalHandler(int sigNum, UnsafeSigOp op, const DSValue &handler, bool setSIGCHLD = false);

    /**
     * expand stack size to at least (stackTopIndex + add)
     * @param add
     * additional size
     */
    void reserveLocalStack(unsigned int add) {
        unsigned int needSize = this->stackTopIndex + add;
        if(needSize < this->callStackSize) {
            return;
        }
        this->reserveLocalStackImpl(needSize);
    }

    ControlFrame getFrame() const {
        return ControlFrame {
                .code = this->code,
                .stackTopIndex = this->stackTopIndex,
                .stackBottomIndex = this->stackBottomIndex,
                .localVarOffset = this->localVarOffset,
                .pc = this->pc_
        };
    }

private:
    /**
     * expand stack size to at least needSize
     * @param needSize
     */
    void reserveLocalStackImpl(unsigned int needSize);
};

/**
 * entry point
 */
bool vmEval(DSState &state, CompiledCode &code);

/**
 *
 * @param st
 * @param argv
 * first element of argv is command name.
 * last element of argv is null.
 * @return
 * exit status.
 * if throw exception, return always 1.
 */
int execBuiltinCommand(DSState &st, char *const argv[]);

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

/**
 *
 * @param state
 * @param funcObj
 * @param args
 * @return
 * return value of method (if no return value, return null).
 */
DSValue callFunction(DSState &state, DSValue &&funcObj, std::vector<DSValue> &&args);

#endif //YDSH_VM_H
