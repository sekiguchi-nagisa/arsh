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
#include <core/symbol.h>
#include <misc/debug.h>

#include <vector>
#include <iostream>

namespace ydsh {
namespace core {

typedef enum {
    EVAL_SUCCESS,
    EVAL_BREAK,
    EVAL_CONTINUE,
    EVAL_THROW,
    EVAL_RETURN,
    EVAL_REMOVE,
} EvalStatus;

struct RuntimeContext {
    TypePool pool;

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

    /**
     * for string cast
     */
    int fieldIndexOf_STR;

    /**
     * for string interpolation
     */
    int fieldIndexOf_INTERP;

    /**
     * for command argument
     */
    int fieldIndexOf_CMD_ARG;

    /**
     * for error reporting
     */
    int fieldIndexOf_bt;

    RuntimeContext(char **envp);

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
     * for internal error reporting.
     */
    void throwError(DSType *errorType, const char *message) {
        this->thrownObject = std::shared_ptr<DSObject>(
                Error_Object::newError(*this, errorType, std::make_shared<String_Object>(
                        this->pool.getStringType(), std::string(message))));
    }

    void throwError(DSType *errorType, std::string &&message) {
        this->thrownObject = std::shared_ptr<DSObject>(
                Error_Object::newError(*this, errorType, std::make_shared<String_Object>(
                        this->pool.getStringType(), message)));
    }

    /**
     * pop and set to throwObject
     */
    void setThrowObject() {
        this->thrownObject = this->pop();
    }

    /**
     * get thrownObject and push to localStack
     */
    void getThrownObject() {
        this->push(this->thrownObject);
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

    void setLocal(unsigned int index, std::shared_ptr<DSObject> &&obj) {
        this->localStack[this->localVarOffset + index] = obj;
    }

    void getLocal(unsigned int index) {
        this->push(this->localStack[this->localVarOffset + index]);
    }

    // field manipulation

    /**
     * get field from stack top value.
     */
    void getField(unsigned int index) {
        this->localStack[this->stackTopIndex] =
                this->localStack[this->stackTopIndex]->getFieldTable()[index];
    }

    /**
     * dup stack top value and get field from it.
     */
    void dupAndGetField(unsigned int index) {
        this->push(this->peek()->getFieldTable()[index]);
    }

    /**
     * stack state in function apply    stack grow ===>
     *
     * +-----------+---------+--------+   +--------+
     * | stack top | funcObj | param1 | ~ | paramN |
     * +-----------+---------+--------+   +--------+
     *                       | offset |   |        |
     */
    EvalStatus applyFuncObject(bool returnTypeIsVoid, unsigned int paramSize) {
        unsigned int savedStackTopIndex = this->stackTopIndex - paramSize - 1;

        // call function
        this->saveAndSetOffset(savedStackTopIndex + 2);
        bool status = TYPE_AS(FuncObject,
                              this->localStack[savedStackTopIndex + 1])->invoke(*this);

        // restore stack state
        this->restoreOffset();
        for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
            this->pop();
        }

        if(status) {
            if(!returnTypeIsVoid) {
                this->getReturnObject(); // push return value
            }
            return EVAL_SUCCESS;
        } else {
            return EVAL_THROW;
        }
    }

    /**
     * stack state in method call    stack grow ===>
     *
     * +-----------+------------------+   +--------+
     * | stack top | param1(receiver) | ~ | paramN |
     * +-----------+------------------+   +--------+
     *             | offset           |   |        |
     */
    EvalStatus callMethod(bool returnTypeIsVoid, unsigned int methodIndex, unsigned int paramSize) {
        unsigned int savedStackTopIndex = this->stackTopIndex - paramSize;

        // call method
        this->saveAndSetOffset(savedStackTopIndex + 1);
        bool status = this->localStack[savedStackTopIndex + 1]->
                type->getMethodRef(methodIndex)->invoke(*this);

        // restore stack state
        this->restoreOffset();
        for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
            this->pop();
        }

        if(status) {
            if(!returnTypeIsVoid) {
                this->getReturnObject(); // push return value
            }
            return EVAL_SUCCESS;
        } else {
            return EVAL_THROW;
        }
    }

    /**
     * allocate new DSObject on stack top.
     * if type is builtin type, not allocate it.
     */
    void newDSObject(DSType *type) {
        if(type->isBuiltinType()) {
           this->dummy->setType(type);
            this->push(this->dummy);
        } else {
            fatal("currently, DSObject allocation not supported\n");
        }
    }

    /**
     * stack state in constructor call     stack grow ===>
     *
     * +-----------+------------------+   +--------+
     * | stack top | param1(receiver) | ~ | paramN |
     * +-----------+------------------+   +--------+
     *             |    new offset    |
     */
    EvalStatus callConstructor(unsigned int paramSize) {
        unsigned int savedStackTopIndex = this->stackTopIndex - paramSize;

        // call constructor
        this->saveAndSetOffset(savedStackTopIndex);
        bool status =
                this->localStack[savedStackTopIndex]->type->getConstructor()->invoke(*this);

        // restore stack state
        this->restoreOffset();
        for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
            this->pop();
        }

        if(status) {
            return EVAL_SUCCESS;
        } else {
            return EVAL_THROW;
        }
    }

    /**
     * cast stack top value to String
     */
    EvalStatus toString() {
        if(this->fieldIndexOf_STR == -1) {
            auto *handle = this->pool.getAnyType()->
                    lookupMethodHandle(&this->pool, std::string(OP_STR));
            this->fieldIndexOf_STR = handle->getFieldIndex();
        }
        return this->callMethod(false, this->fieldIndexOf_STR, 1);
    }

    /**
     * call __INTERP__
     */
    EvalStatus toInterp() {
        if(this->fieldIndexOf_INTERP == -1) {
            auto *handle = this->pool.getAnyType()->
                    lookupMethodHandle(&this->pool, std::string(OP_INTERP));
            this->fieldIndexOf_INTERP = handle->getFieldIndex();
        }
        return this->callMethod(false, this->fieldIndexOf_INTERP, 1);
    }

    /**
     * call __CMD_ARG__
     */
    EvalStatus toCmdArg() {
        if(this->fieldIndexOf_CMD_ARG == -1) {
            auto *handle = this->pool.getAnyType()->
                    lookupMethodHandle(&this->pool, std::string(OP_CMD_ARG));
            this->fieldIndexOf_CMD_ARG = handle->getFieldIndex();
        }
        return this->callMethod(false, this->fieldIndexOf_CMD_ARG, 1);
    }

    /**
     * report thrown object error message.
     * after error reporting, clear thrown object
     */
    void reportError() {
        std::cerr << "[runtime error]" << std::endl;
        if(this->pool.getErrorType()->isAssignableFrom(this->thrownObject->type)) {
            if(this->fieldIndexOf_bt == -1) {
                std::string str("backtrace");
                FieldHandle *handle = this->pool.getErrorType()->lookupMethodHandle(&this->pool, str);
                this->fieldIndexOf_bt = handle->getFieldIndex();
            }
            this->getThrownObject();
            this->callMethod(false, this->fieldIndexOf_bt, 1);
        } else {
            std::cerr << this->thrownObject->toString(*this) << std::endl;
        }
    }


    // some runtime api
    void printStackTop(DSType *stackTopType);

    bool checkCast(DSType *targetType);

    void instanceOf(DSType *targetType);

    void checkAssertion();

    /**
     * get environment variable and set to local variable
     */
    void importEnv(const std::string &envName, int index, bool isGlobal);

    /**
     * put stack top value to environment variable.
     */
    void exportEnv(const std::string &envName, int index, bool isGlobal);

    bool checkZeroDiv(int right) {
        if(right == 0) {
            this->throwError(this->pool.getArithmeticErrorType(), "zero division");
            return false;
        }
        return true;
    }

    bool checkZeroDiv(double right) {
        if(right == 0) {
            this->throwError(this->pool.getArithmeticErrorType(), "zero division");
            return false;
        }
        return true;
    }

    bool checkZeroMod(int right) {
        if(right == 0) {
            this->throwError(this->pool.getArithmeticErrorType(), "zero module");
            return false;
        }
        return true;
    }

    bool checkZeroMod(double right) {
        if(right == 0) {
            this->throwError(this->pool.getArithmeticErrorType(), "zero module");
            return false;
        }
        return true;
    }

    void throwOutOfIndexError(std::string &&message) {
        this->throwError(this->pool.getOutOfIndexErrorType(), std::move(message));
    }
};

} // namespace core
} // namespace ydsh

#endif /* CORE_RUNTIMECONTEXT_H_ */
