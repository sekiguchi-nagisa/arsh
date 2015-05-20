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

#include "DSObject.h"
#include "TypePool.h"
#include "FieldHandle.h"
#include "DSType.h"
#include "ProcContext.h"
#include "symbol.h"
#include "status.h"
#include "../misc/debug.h"

#include <vector>
#include <iostream>

namespace ydsh {
namespace ast {

class Node;

}
}


namespace ydsh {
namespace core {

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

    static const char *configRootDir;
    static const char *typeDefDir;

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

    const std::shared_ptr<DSObject> peek() {
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

    void setField(unsigned int index) {
        std::shared_ptr<DSObject> value(this->pop());
        this->pop()->getFieldTable()[index] = std::move(value);
    }

    EvalStatus setField(DSType *recvType, const std::string &fieldName, DSType *fieldType) {
        bool status = TYPE_AS(ProxyObject, this->localStack[this->stackTopIndex - 1])->
                invokeSetter(*this, recvType, fieldName, fieldType);
        // pop receiver
        this->popNoReturn();
        return status ? EVAL_SUCCESS : EVAL_THROW;
    }

    /**
     * get field from stack top value.
     */
    void getField(unsigned int index) {
        this->localStack[this->stackTopIndex] =
                this->localStack[this->stackTopIndex]->getFieldTable()[index];
    }

    EvalStatus getField(DSType *recvType, const std::string &fieldName, DSType *fieldType) {
        bool status = TYPE_AS(ProxyObject, this->pop())->
                invokeGetter(*this, recvType, fieldName, fieldType);
        return status ? EVAL_SUCCESS : EVAL_THROW;
    }

    /**
     * dup stack top value and get field from it.
     */
    void dupAndGetField(unsigned int index) {
        this->push(this->peek()->getFieldTable()[index]);
    }

    EvalStatus dupAndGetField(DSType *recvType, const std::string &fieldName, DSType *fieldType) {
        bool status = TYPE_AS(ProxyObject, this->localStack[this->stackTopIndex])->
                invokeGetter(*this, recvType, fieldName, fieldType);
        return status ? EVAL_SUCCESS : EVAL_THROW;
    }

    void pushCallFrame(unsigned int lineNum) {
        unsigned long index = ((unsigned long) (this->funcContextStack.size() - 1)) << 32;
        this->callStack.push_back(index | (unsigned long) lineNum);
    }

    void popCallFrame() {
        this->callStack.pop_back();
    }

    /**
     * stack state in function apply    stack grow ===>
     *
     * +-----------+---------+--------+   +--------+
     * | stack top | funcObj | param1 | ~ | paramN |
     * +-----------+---------+--------+   +--------+
     *                       | offset |   |        |
     */
    EvalStatus applyFuncObject(unsigned int lineNum, bool returnTypeIsVoid, unsigned int paramSize) {
        unsigned int savedStackTopIndex = this->stackTopIndex - paramSize - 1;

        // call function
        this->saveAndSetOffset(savedStackTopIndex + 2);
        this->pushCallFrame(lineNum);
        bool status = TYPE_AS(FuncObject,
                              this->localStack[savedStackTopIndex + 1])->invoke(*this);
        this->popCallFrame();

        // restore stack state
        std::shared_ptr<DSObject> returnValue;
        if(!returnTypeIsVoid) {
            returnValue = std::move(this->localStack[this->stackTopIndex]);
        }

        this->restoreOffset();
        for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
            this->popNoReturn();
        }

        if(returnValue) {
            this->push(std::move(returnValue));
        }
        return status ? EVAL_SUCCESS : EVAL_THROW;
    }

    /**
     * stack state in method call    stack grow ===>
     *
     * +-----------+------------------+   +--------+
     * | stack top | param1(receiver) | ~ | paramN |
     * +-----------+------------------+   +--------+
     *             | offset           |   |        |
     */
    EvalStatus callMethod(unsigned int lineNum, const std::string &methodName, MethodHandle *handle) {
        /**
         * include receiver
         */
        unsigned int paramSize = handle->getParamTypes().size() + 1;

        unsigned int savedStackTopIndex = this->stackTopIndex - paramSize;

        // call method
        this->saveAndSetOffset(savedStackTopIndex + 1);
        this->pushCallFrame(lineNum);

        bool status;
        // check method handle type
        if(!handle->isInterfaceMethod()) {  // call virtual method
            status = this->localStack[savedStackTopIndex + 1]->
                    type->getMethodRef(handle->getMethodIndex())->invoke(*this);
        } else {    // call proxy method
            status = TYPE_AS(ProxyObject, this->localStack[savedStackTopIndex + 1])->
                    invokeMethod(*this, methodName, handle);
        }

        this->popCallFrame();

        // restore stack state
        std::shared_ptr<DSObject> returnValue;
        if(!handle->getReturnType()->isVoidType()) {
            returnValue = std::move(this->localStack[this->stackTopIndex]);
        }

        this->restoreOffset();
        for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
            this->popNoReturn();
        }

        if(returnValue) {
            this->push(std::move(returnValue));
        }
        return status ? EVAL_SUCCESS : EVAL_THROW;
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
    EvalStatus callConstructor(unsigned int lineNum, unsigned int paramSize) {
        unsigned int savedStackTopIndex = this->stackTopIndex - paramSize;

        // call constructor
        this->saveAndSetOffset(savedStackTopIndex);
        this->pushCallFrame(lineNum);
        bool status =
                this->localStack[savedStackTopIndex]->type->getConstructor()->invoke(*this);
        this->popCallFrame();

        // restore stack state
        this->restoreOffset();
        for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
            this->popNoReturn();
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
    EvalStatus toString(unsigned int lineNum) {
        static const std::string methodName(OP_STR);

        if(this->handle_STR == nullptr) {
            this->handle_STR = this->pool.getAnyType()->
                    lookupMethodHandle(&this->pool, methodName);
        }
        return this->callMethod(lineNum, methodName, this->handle_STR);
    }

    /**
     * call __INTERP__
     */
    EvalStatus toInterp(unsigned int lineNum) {
        static const std::string methodName(OP_INTERP);

        if(this->handle_INTERP == nullptr) {
            this->handle_INTERP = this->pool.getAnyType()->
                    lookupMethodHandle(&this->pool, methodName);
        }
        return this->callMethod(lineNum, methodName, this->handle_INTERP);
    }

    /**
     * call __CMD_ARG__
     */
    EvalStatus toCmdArg(unsigned int lineNum) {
        static const std::string methodName(OP_CMD_ARG);

        if(this->handle_CMD_ARG == nullptr) {
            this->handle_CMD_ARG = this->pool.getAnyType()->
                    lookupMethodHandle(&this->pool, methodName);
        }
        return this->callMethod(lineNum, methodName, this->handle_CMD_ARG);
    }

    /**
     * report thrown object error message.
     * after error reporting, clear thrown object
     */
    void reportError() {
        std::cerr << "[runtime error]" << std::endl;
        if(this->pool.getErrorType()->isAssignableFrom(this->thrownObject->type)) {
            static const std::string methodName("backtrace");

            if(this->handle_bt == nullptr) {
                this->handle_bt = this->pool.getErrorType()->lookupMethodHandle(&this->pool, methodName);
            }
            this->getThrownObject();
            this->callMethod(0, methodName, this->handle_bt);
        } else {
            std::cerr << this->thrownObject->toString(*this) << std::endl;
        }
    }


    // some runtime api
    void fillInStackTrace(std::vector<std::string> &stackTrace);

    void printStackTop(DSType *stackTopType);

    bool checkCast(unsigned int lineNum, DSType *targetType);

    void instanceOf(DSType *targetType);

    void checkAssertion(unsigned int lineNum);

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

    /**
     * register source name to readFiles.
     * return pointer of added name.
     * sourceName is null, if source is stdin.
     */
    const char *registerSourceName(const char *sourceName);
};

} // namespace core
} // namespace ydsh

#endif /* CORE_RUNTIMECONTEXT_H_ */
