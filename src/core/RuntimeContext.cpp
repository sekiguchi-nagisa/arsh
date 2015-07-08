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

#include "RuntimeContext.h"
#include "FieldHandle.h"
#include "symbol.h"
#include "../misc/debug.h"
#include "../misc/files.h"

namespace ydsh {
namespace core {

// ############################
// ##     RuntimeContext     ##
// ############################

#define DEFAULT_TABLE_SIZE 32
#define DEFAULT_LOCAL_SIZE 256

RuntimeContext::RuntimeContext() :
        pool(),
        trueObj(new Boolean_Object(this->pool.getBooleanType(), true)),
        falseObj(new Boolean_Object(this->pool.getBooleanType(), false)),
        dummy(new DummyObject()),
        scriptName(std::make_shared<String_Object>(this->pool.getStringType(), std::string("ydsh"))),
        scriptArgs(std::make_shared<Array_Object>(this->pool.getStringArrayType())),
        exitStatus(std::make_shared<Int_Object>(this->pool.getInt32Type(), 0)),
        dbus(DBus_Object::newDBus_Object(&this->pool)),
        globalVarTable(new std::shared_ptr<DSObject>[DEFAULT_TABLE_SIZE]),
        tableSize(DEFAULT_TABLE_SIZE), thrownObject(),
        localStack(new std::shared_ptr<DSObject>[DEFAULT_LOCAL_SIZE]),
        localStackSize(DEFAULT_LOCAL_SIZE), stackTopIndex(0),
        localVarOffset(0), offsetStack(), toplevelPrinting(false), assertion(true),
        handle_STR(0), handle_INTERP(0), handle_CMD_ARG(0), handle_bt(0),
        readFiles(), funcContextStack(), callStack(),
        workingDir(getCurrentWorkingDir()), specialCharMap(), procInvoker(this) {
    this->readFiles.push_back(std::string("(stdin)"));
}

RuntimeContext::~RuntimeContext() {
    delete[] this->globalVarTable;
    this->globalVarTable = 0;

    delete[] this->localStack;
    this->localStack = 0;
}

std::string RuntimeContext::getConfigRootDir() {
#ifdef X_CONFIG_DIR
    return std::string(X_CONFIG_DIR);
#else
    std::string path(getenv("HOME"));
    path += "/.ydsh";
    return path;
#endif
}

std::string RuntimeContext::getIfaceDir() {
    std::string root(getConfigRootDir());
    root += "/dbus/iface";
    return root;
}

void RuntimeContext::updateScriptName(const char *name) {
    this->scriptName = std::make_shared<String_Object>(this->pool.getStringType(), std::string(name));
    unsigned int index = this->getSpecialCharIndex("0");
    this->storeGlobal(index, this->scriptName);
}

void RuntimeContext::addScriptArg(const char *arg) {
    this->scriptArgs->append(
            std::make_shared<String_Object>(this->pool.getStringType(), std::string(arg)));
}

void RuntimeContext::reserveGlobalVar(unsigned int size) {
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

void RuntimeContext::reserveLocalVar(unsigned int size) {
    if(size > this->localStackSize) {
        this->expandLocalStack(size);
    }
    this->stackTopIndex = size;
}

void RuntimeContext::throwError(DSType *errorType, const char *message) {
    this->thrownObject = Error_Object::newError(*this, errorType, std::make_shared<String_Object>(
            this->pool.getStringType(), std::string(message)));
}

void RuntimeContext::throwError(DSType *errorType, std::string &&message) {
    this->thrownObject = Error_Object::newError(*this, errorType, std::make_shared<String_Object>(
            this->pool.getStringType(), message));
}

void RuntimeContext::expandLocalStack(unsigned int needSize) {
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

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+---------+--------+   +--------+
 * | stack top | funcObj | param1 | ~ | paramN |
 * +-----------+---------+--------+   +--------+
 *                       | offset |   |        |
 */
EvalStatus RuntimeContext::applyFuncObject(unsigned int lineNum, bool returnTypeIsVoid, unsigned int paramSize) {
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
    return status ? EvalStatus::SUCCESS : EvalStatus::THROW;
}

/**
 * stack state in method call    stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             | offset           |   |        |
 */
EvalStatus RuntimeContext::callMethod(unsigned int lineNum, const std::string &methodName, MethodHandle *handle) {
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
                getType()->getMethodRef(handle->getMethodIndex())->invoke(*this);
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
    return status ? EvalStatus::SUCCESS : EvalStatus::THROW;
}

void RuntimeContext::newDSObject(DSType *type) {
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
EvalStatus RuntimeContext::callConstructor(unsigned int lineNum, unsigned int paramSize) {
    unsigned int savedStackTopIndex = this->stackTopIndex - paramSize;

    // call constructor
    this->saveAndSetOffset(savedStackTopIndex);
    this->pushCallFrame(lineNum);
    bool status =
            this->localStack[savedStackTopIndex]->getType()->getConstructor()->invoke(*this);
    this->popCallFrame();

    // restore stack state
    this->restoreOffset();
    for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
        this->popNoReturn();
    }

    if(status) {
        return EvalStatus::SUCCESS;
    } else {
        return EvalStatus::THROW;
    }
}

EvalStatus RuntimeContext::toString(unsigned int lineNum) {
    static const std::string methodName(OP_STR);

    if(this->handle_STR == nullptr) {
        this->handle_STR = this->pool.getAnyType()->
                lookupMethodHandle(&this->pool, methodName);
    }
    return this->callMethod(lineNum, methodName, this->handle_STR);
}

EvalStatus RuntimeContext::toInterp(unsigned int lineNum) {
    static const std::string methodName(OP_INTERP);

    if(this->handle_INTERP == nullptr) {
        this->handle_INTERP = this->pool.getAnyType()->
                lookupMethodHandle(&this->pool, methodName);
    }
    return this->callMethod(lineNum, methodName, this->handle_INTERP);
}

EvalStatus RuntimeContext::toCmdArg(unsigned int lineNum) {
    static const std::string methodName(OP_CMD_ARG);

    if(this->handle_CMD_ARG == nullptr) {
        this->handle_CMD_ARG = this->pool.getAnyType()->
                lookupMethodHandle(&this->pool, methodName);
    }
    return this->callMethod(lineNum, methodName, this->handle_CMD_ARG);
}

void RuntimeContext::reportError() {
    std::cerr << "[runtime error]" << std::endl;
    if(this->pool.getErrorType()->isSameOrBaseTypeOf(this->thrownObject->getType())) {
        static const std::string methodName("backtrace");

        if(this->handle_bt == nullptr) {
            this->handle_bt = this->pool.getErrorType()->lookupMethodHandle(&this->pool, methodName);
        }
        this->loadThrownObject();
        this->callMethod(0, methodName, this->handle_bt);
    } else {
        std::cerr << this->thrownObject->toString(*this) << std::endl;
    }
}

void RuntimeContext::fillInStackTrace(std::vector<StackTraceElement> &stackTrace) {
    static const unsigned long lowOrderMask = ~((1L << 32) - 1);
    static const unsigned long highOrderMask = ~(((1L << 32) - 1) << 32);

    for(auto iter = this->callStack.rbegin(); iter != this->callStack.rend(); ++iter) {
        unsigned long frame = *iter;
        unsigned long funcCtxIndex = (frame & lowOrderMask) >> 32;
        Node *node = this->funcContextStack[funcCtxIndex];
        const char *sourceName = node->getSourceName();
        unsigned long lineNum = frame & highOrderMask;

        std::string callerName;
        FunctionNode *funcNode = dynamic_cast<FunctionNode *>(node);
        if(funcNode != 0) {
            callerName += "function ";
            callerName += funcNode->getFuncName();
        } else {
            callerName += "<toplevel>";
        }

        stackTrace.push_back(StackTraceElement(sourceName, lineNum, std::move(callerName)));
    }
}

void RuntimeContext::printStackTop(DSType *stackTopType) {
    if(!stackTopType->isVoidType()) {
        std::cout << "(" << this->pool.getTypeName(*stackTopType)
        << ") " << this->pop()->toString(*this) << std::endl;
    }
}

bool RuntimeContext::checkCast(unsigned int lineNum, DSType *targetType) {
    if(!this->peek()->introspect(*this, targetType)) {
        DSType *stackTopType = this->pop()->getType();
        std::string str("cannot cast ");
        str += this->pool.getTypeName(*stackTopType);
        str += " to ";
        str += this->pool.getTypeName(*targetType);
        this->pushCallFrame(lineNum);
        this->throwError(this->pool.getTypeCastErrorType(), std::move(str));
        this->popCallFrame();
        return false;
    }
    return true;
}

void RuntimeContext::instanceOf(DSType *targetType) {
    if(this->pop()->introspect(*this, targetType)) {
        this->push(this->trueObj);
    } else {
        this->push(this->falseObj);
    }
}

EvalStatus RuntimeContext::checkAssertion(unsigned int lineNum) {
    if(!TYPE_AS(Boolean_Object, this->pop())->getValue()) {
        this->pushCallFrame(lineNum);
        this->throwError(this->pool.getAssertFail(), "");
        this->popCallFrame();
        return EvalStatus::THROW;
    }
    return EvalStatus::SUCCESS;
}

void RuntimeContext::importEnv(const std::string &envName, int index, bool isGlobal) {
    if(isGlobal) {
        this->globalVarTable[index] =
                std::make_shared<String_Object>(this->pool.getStringType(),
                                                std::string(getenv(envName.c_str())));
    } else {
        this->localStack[this->localVarOffset + index] =
                std::make_shared<String_Object>(this->pool.getStringType(),
                                                std::string(getenv(envName.c_str())));
    }
}

void RuntimeContext::exportEnv(const std::string &envName, int index, bool isGlobal) {
    setenv(envName.c_str(),
           TYPE_AS(String_Object, this->peek())->getValue().c_str(), 1);   //FIXME: check return value and throw
    if(isGlobal) {
        this->globalVarTable[index] = this->pop();
    } else {
        this->localStack[this->localVarOffset + index] = this->pop();
    }
}

bool RuntimeContext::checkZeroDiv(int right) {
    if(right == 0) {
        this->throwError(this->pool.getArithmeticErrorType(), "zero division");
        return false;
    }
    return true;
}

bool RuntimeContext::checkZeroDiv(double right) {
    if(right == 0) {
        this->throwError(this->pool.getArithmeticErrorType(), "zero division");
        return false;
    }
    return true;
}

bool RuntimeContext::checkZeroMod(int right) {
    if(right == 0) {
        this->throwError(this->pool.getArithmeticErrorType(), "zero module");
        return false;
    }
    return true;
}

void RuntimeContext::updateWorkingDir() {
    this->workingDir = getCurrentWorkingDir();
}

const char *RuntimeContext::registerSourceName(const char *sourceName) {
    if(sourceName == 0) {
        return this->readFiles[defaultFileNameIndex].c_str();
    }
    this->readFiles.push_back(std::string(sourceName));
    return this->readFiles.back().c_str();
}

void RuntimeContext::updateExitStatus(unsigned int status) {
    this->exitStatus = std::make_shared<Int_Object>(this->pool.getInt32Type(), status);
    unsigned int index = this->getSpecialCharIndex("?");
    this->storeGlobal(index, this->exitStatus);
}

void RuntimeContext::exitShell(unsigned int status) {
    this->updateExitStatus(status);
    this->throwError(this->pool.getShellExit(), "exit shell by exit command");
}

void RuntimeContext::registerSpecialChar(const std::string &varName, unsigned int index) {
    auto pair = this->specialCharMap.insert(std::make_pair(varName, index));
    if(!pair.second) {
        fatal("found duplicated character: %s\n", varName.c_str());
    }
}

unsigned int RuntimeContext::getSpecialCharIndex(const char *varName) {
    auto iter = this->specialCharMap.find(varName);
    if(iter == this->specialCharMap.end()) {
        fatal("undefined special character: %s\n", varName);
    }
    return iter->second;
}

} // namespace core
} // namespace ydsh
