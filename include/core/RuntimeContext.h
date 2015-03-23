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

#include <core/DSObject.h>
#include <core/TypePool.h>
#include <core/DSType.h>
#include <core/ProcContext.h>

#include <vector>

struct RuntimeContext {
    TypePool pool;

    std::shared_ptr<Boolean_Object> trueObj;
    std::shared_ptr<Boolean_Object> falseObj;

    /**
     * contains global variables(or function)
     */
    std::shared_ptr<DSObject> *globalVarTable;

    /**
     * size of global variable table.
     */
    unsigned int tableSize;

    /**
     * if not null ptr, has return value.
     */
    std::shared_ptr<DSObject> returnObject;

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
     * if true, runtime interactive mode.
     */
    bool repl;

    /**
     * if true, enable assertion.
     */
    bool assertion;

    RuntimeContext();
    ~RuntimeContext();

    /**
     * if this->tableSize < size, expand globalVarTable.
     */
    void reserveGlobalVar(unsigned int size) {
        if(this->tableSize < size) {
            unsigned int newSize = this->tableSize;
            do {
                newSize *= 2;
            } while(newSize < size);
            auto newTable = new std::shared_ptr<DSObject>[newSize];
            for(unsigned int i = 0; i < this->tableSize; i++) {
                newTable[i] = this->globalVarTable[i];
            }
            delete[] this->globalVarTable;
            this->globalVarTable = newTable;
            this->tableSize = newSize;
        }
    }

    /**
     * pop and set to returnObject.
     */
    void setReturnObject() {
        this->returnObject = this->pop();
    }

    /**
     * pop returnObject and push to localStack.
     */
    void getReturnObject() {
        this->push(this->returnObject);
        this->returnObject.reset();
    }

    /**
     * reset this->throwObject.
     */
    void clearThrownObject() {
        this->thrownObject.reset();
    }

    void expandLocalStack(unsigned int needSize) {
        unsigned int newSize = this->localStackSize;
        do {
            newSize *= 2;
        } while(newSize < needSize);
        auto newTable = new std::shared_ptr<DSObject>[newSize];
        for(unsigned int i = 0; i < this->localStackSize; i++) {
            newTable[i] = this->localStack[i];
        }
        delete[] this->localStack;
        this->localStack = newTable;
        this->localStackSize = newSize;
    }

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

    std::shared_ptr<DSObject> pop() {
        std::shared_ptr<DSObject> obj(this->localStack[this->stackTopIndex]);
        this->localStack[this->stackTopIndex].reset();
        --this->stackTopIndex;
        return obj;
    }

    std::shared_ptr<DSObject> peek() {
        return this->localStack[this->stackTopIndex];
    }

    // variable operation
    void setGlobal(unsigned int index) {
        this->globalVarTable[index] = this->pop();
    }

    void setGlobal(unsigned int index, const std::shared_ptr<DSObject> &obj) {
        this->globalVarTable[index] = obj;
    }

    void getGlobal(unsigned int index) {
        this->push(this->globalVarTable[index]);
    }

    void setLocal(unsigned int index) {
        this->localStack[this->localVarOffset + index] = this->pop();
    }

    void getLocal(unsigned int index) {
        this->push(this->localStack[this->localVarOffset + index]);
    }

    // some runtime api
    void printStackTop(DSType *stackTopType);
    void checkCast(DSType *targetType);
    void instanceOf(DSType *targetType);
    void checkAssertion();
};

#endif /* CORE_RUNTIMECONTEXT_H_ */
