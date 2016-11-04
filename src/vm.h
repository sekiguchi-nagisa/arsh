/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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

#include <unistd.h>

#include <vector>
#include <iostream>

#include <ydsh/ydsh.h>

#include "node.h"
#include "object.h"
#include "symbol_table.h"
#include "proc.h"
#include "core.h"
#include "misc/buffer.hpp"
#include "misc/noncopyable.h"

using namespace ydsh;

// for exception handling
struct DSException {};

struct DSState {
    TypePool pool;
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

    /**
     * for pseudo object allocation (used for builtin constructor call)
     */
    const DSValue dummy;

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
    std::vector<const DSCode *> codeStack;

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

    std::string logicalWorkingDir;

    NON_COPYABLE(DSState);

    DSState();

    ~DSState() {
        delete[] this->callStack;
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
    void recover(bool abortType = true) {
        this->symbolTable.abort();
        if(abortType) {
            this->pool.abort();
        }
    }

    const DSValue &getThrownObject() const {
        return this->thrownObject;
    }

    void setThrownObject(DSValue &&value) {
        this->thrownObject = std::move(value);
    }

    unsigned int &pc() noexcept {
        return this->pc_;
    }

    /**
     * create error.
     */
    DSValue newError(DSType &errorType, std::string &&message) const {
        return Error_Object::newError(*this, errorType, DSValue::create<String_Object>(
                this->pool.getStringType(), std::move(message)));
    }

    /**
     * pop stack top and store to thrownObject.
     */
    [[noreturn]]
    void throwException(DSValue &&except) {
        this->thrownObject = std::move(except);
        throw DSException();
    }

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

    // operand manipulation
    void push(const DSValue &value) {
        this->push(DSValue(value));
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
        this->setGlobal(index, DSValue(obj));
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
        setLocal(index, DSValue(obj));
    }

    void setLocal(unsigned int index, DSValue &&obj) {
        this->callStack[this->localVarOffset + index] = std::move(obj);
    }

    const DSValue &getLocal(unsigned int index) const {
        return this->callStack[this->localVarOffset + index];
    }

    DSValue moveLocal(unsigned int index) {
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
        this->codeStack.clear();
    }

    void updateExitStatus(unsigned int status) {
        unsigned int index = toIndex(BuiltinVarOffset::EXIT_STATUS);
        this->setGlobal(index, DSValue::create<Int_Object>(this->pool.getInt32Type(), status));
    }
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
 * exit status
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

#endif //YDSH_VM_H
