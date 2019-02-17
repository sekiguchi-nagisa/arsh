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
    void install(int sigNum, UnsafeSigOp op, const DSValue &handler, bool setSIGCHLD = false);
};

struct ControlFrame {
    /**
     * currently executed code
     */
    const DSCode *code{nullptr};

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
    unsigned int pc{0};

    /**
     * interpreter recursive depth
     */
    unsigned int recDepth{0};
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
    static constexpr unsigned int MAX_CONTROL_STACK_SIZE = 2048;

    unsigned int globalVarSize{0};

    /**
     * currently executed frame
     */
    ControlFrame frame;

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

    static constexpr flag32_t VM_EVENT_HOOK   = 1u << 0u;
    static constexpr flag32_t VM_EVENT_SIGNAL = 1u << 1u;
    static constexpr flag32_t VM_EVENT_MASK   = 1u << 2u;

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

    const DSValue &getThrownObject() const {
        return this->thrownObject;
    }

    unsigned int &stackTopIndex() noexcept {
        return this->frame.stackTopIndex;
    }

    unsigned int &stackBottomIndex() noexcept {
        return this->frame.stackBottomIndex;
    }

    unsigned int &localVarOffset() noexcept {
        return this->frame.localVarOffset;
    }

    unsigned int localVarOffset() const noexcept {
        return this->frame.localVarOffset;
    }

    unsigned int &pc() noexcept {
        return this->frame.pc;
    }

    const DSCode *&code() noexcept {
        return this->frame.code;
    }

    unsigned int &recDepth() noexcept {
        return this->frame.recDepth;
    }

    unsigned int recDepth() const noexcept {
        return this->frame.recDepth;
    }

    /**
     * set thrownObject and update exit status
     * @param except
     * @param afterStatus
     * set exit status to it
     */
    void throwObject(DSValue &&except, int afterStatus) {
        this->setThrownObject(std::move(except));
        this->updateExitStatus(afterStatus);
    }

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

    void storeThrownObject() {
        this->setThrownObject(this->pop());
    }

    void clearThrownObject() {
        this->thrownObject.reset();
    }

    // operand manipulation
    void push(const DSValue &value) {
        this->push(DSValue(value));
    }

    void push(DSValue &&value) {
        this->callStack[++this->stackTopIndex()] = std::move(value);
    }

    DSValue pop() {
        return std::move(this->callStack[this->stackTopIndex()--]);
    }

    void popNoReturn() {
        this->callStack[this->stackTopIndex()--].reset();
    }

    const DSValue &peek() {
        return this->callStack[this->stackTopIndex()];
    }

    void dup() {
        auto v = this->callStack[this->stackTopIndex()];
        this->callStack[++this->stackTopIndex()] = std::move(v);
    }

    void dup2() {
        auto v1 = this->callStack[this->stackTopIndex() - 1];
        auto v2 = this->callStack[this->stackTopIndex()];
        this->callStack[++this->stackTopIndex()] = std::move(v1);
        this->callStack[++this->stackTopIndex()] = std::move(v2);
    }

    void swap() {
        this->callStack[this->stackTopIndex()].swap(this->callStack[this->stackTopIndex() - 1]);
    }

    void clearOperandStack() {
        while(this->stackTopIndex() > this->stackBottomIndex()) {
            this->popNoReturn();
        }
    }

    void reclaimLocals(unsigned char offset, unsigned char size) {
        auto *limit = this->callStack + this->localVarOffset() + offset;
        auto *cur = limit + size - 1;
        while(cur >= limit) {
            (cur--)->reset();
        }
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
        this->callStack[this->localVarOffset() + index] = this->pop();
    }

    void loadLocal(unsigned char index) {
        auto v(this->callStack[this->localVarOffset() + index]); // callStack may be expanded.
        this->push(std::move(v));
    }

    void setLocal(unsigned char index, const DSValue &obj) {
        setLocal(index, DSValue(obj));
    }

    void setLocal(unsigned char index, DSValue &&obj) {
        this->callStack[this->localVarOffset() + index] = std::move(obj);
    }

    const DSValue &getLocal(unsigned char index) const {
        return this->callStack[this->localVarOffset() + index];
    }

    DSValue moveLocal(unsigned char index) {
        return std::move(this->callStack[this->localVarOffset() + index]);
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
        this->callStack[this->stackTopIndex()] =
                this->callStack[this->stackTopIndex()]->getFieldTable()[index];
    }

    void updateExitStatus(unsigned int status) {
        unsigned int index = toIndex(BuiltinVarOffset::EXIT_STATUS);
        this->setGlobal(index, DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Int32), status));
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

    /**
     * expand stack size to at least (stackTopIndex + add)
     * @param add
     * additional size
     */
    void reserveLocalStack(unsigned int add) {
        unsigned int needSize = this->stackTopIndex() + add;
        if(needSize < this->callStackSize) {
            return;
        }
        this->reserveLocalStackImpl(needSize);
    }

    /**
     * reserve global variable entry and set local variable offset.
     */
    void reserveGlobalVar() {
        unsigned int size = this->symbolTable.getMaxGVarIndex();
        this->reserveLocalStack(size - this->globalVarSize);
        this->globalVarSize = size;
        this->localVarOffset() = size;
        this->stackTopIndex() = size;
        this->stackBottomIndex() = size;
    }

    const ControlFrame &getFrame() const {
        return this->frame;
    }

    // runtime api
    bool checkCast(DSType *targetType);

    bool checkAssertion();

    void exitShell(unsigned int status);

    const char *loadEnv(bool hasDefault);

    bool windStackFrame(unsigned int stackTopOffset, unsigned int paramSize, const DSCode *code);

    void unwindStackFrame();

    /**
     * stack state in function apply    stack grow ===>
     *
     * +-----------+---------+--------+   +--------+
     * | stack top | funcObj | param1 | ~ | paramN |
     * +-----------+---------+--------+   +--------+
     *                       | offset |   |        |
     */
    bool prepareFuncCall(unsigned int paramSize) {
        auto *func = typeAs<FuncObject>(this->callStack[this->stackTopIndex() - paramSize]);
        return this->windStackFrame(paramSize + 1, paramSize, &func->getCode());
    }

    /**
     * stack state in method call    stack grow ===>
     *
     * +-----------+------------------+   +--------+
     * | stack top | param1(receiver) | ~ | paramN |
     * +-----------+------------------+   +--------+
     *             | offset           |   |        |
     */
    bool prepareMethodCall(unsigned short index, unsigned short paramSize) {
        const unsigned int actualParamSize = paramSize + 1; // include receiver
        const unsigned int recvIndex = this->stackTopIndex() - paramSize;

        return this->windStackFrame(actualParamSize, actualParamSize, this->callStack[recvIndex]->getType()->getMethodRef(index));
    }


    /**
     * stack state in constructor call     stack grow ===>
     *
     * +-----------+------------------+   +--------+
     * | stack top | param1(receiver) | ~ | paramN |
     * +-----------+------------------+   +--------+
     *             |    new offset    |
     */
    bool prepareConstructorCall(unsigned short paramSize) {
        const unsigned int recvIndex = this->stackTopIndex() - paramSize;

        return this->windStackFrame(paramSize, paramSize + 1, this->callStack[recvIndex]->getType()->getConstructor());
    }

    const DSCode *lookupUserDefinedCommand(const char *commandName) const {
        auto handle = this->symbolTable.lookupUdc(commandName);
        return handle == nullptr ? nullptr : &typeAs<FuncObject>(this->getGlobal(handle->getIndex()))->getCode();
    }

    bool checkVMReturn() const {
        return this->controlStack.empty() || this->recDepth() != this->controlStack.back().recDepth;
    }

    /**
     * stack state in function apply    stack grow ===>
     *
     * +-----------+---------------+--------------+
     * | stack top | param1(redir) | param2(argv) |
     * +-----------+---------------+--------------+
     *             |     offset    |
     */
    bool prepareUserDefinedCommandCall(const DSCode *code, DSValue &&argvObj,
                                       DSValue &&restoreFD, const flag8_set_t attr);


private:
    /**
     * expand stack size to at least needSize
     * @param needSize
     */
    void reserveLocalStackImpl(unsigned int needSize);
};

namespace ydsh {

/**
 * entry point
 */
bool vmEval(DSState &state, const CompiledCode &code);

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

} // namespace

#endif //YDSH_VM_H
