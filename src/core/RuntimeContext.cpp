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

#include <core/RuntimeContext.h>
#include <ast/Node.h>

namespace ydsh {
namespace core {

// ############################
// ##     RuntimeContext     ##
// ############################

#define DEFAULT_TABLE_SIZE 32
#define DEFAULT_LOCAL_SIZE 256

RuntimeContext::RuntimeContext(char **envp) :
        pool(envp),
        trueObj(new Boolean_Object(this->pool.getBooleanType(), true)),
        falseObj(new Boolean_Object(this->pool.getBooleanType(), false)),
        dummy(new DummyObject()),
        scriptName(std::make_shared<String_Object>(this->pool.getStringType(), std::string("-ydsh"))),
        scriptArgs(std::make_shared<Array_Object>(this->pool.getStringArrayType())),
        exitStatus(std::make_shared<Int_Object>(this->pool.getIntType(), 0)),
        dbus(new DBus_Object(this->pool)),
        globalVarTable(new std::shared_ptr<DSObject>[DEFAULT_TABLE_SIZE]),
        tableSize(DEFAULT_TABLE_SIZE),
        returnObject(), thrownObject(),
        localStack(new std::shared_ptr<DSObject>[DEFAULT_LOCAL_SIZE]),
        localStackSize(DEFAULT_LOCAL_SIZE), stackTopIndex(0),
        localVarOffset(0), offsetStack(), repl(false), assertion(true),
        methodIndexOf_STR(-1), methodIndexOf_INTERP(-1), methodIndexOf_CMD_ARG(-1), methodIndexOf_bt(-1),
        readFiles(), funcContextStack(), callStack() {
    this->readFiles.push_back(std::string("(stdin)"));
}

RuntimeContext::~RuntimeContext() {
    delete[] this->globalVarTable;
    this->globalVarTable = 0;

    delete[] this->localStack;
    this->localStack = 0;
}


#ifndef X_CONFIG_DIR
#define X_CONFIG_DIR "./.ydsh/"
#endif

const char *RuntimeContext::configRootDir = X_CONFIG_DIR;
const char *RuntimeContext::typeDefDir = X_CONFIG_DIR "dbus/iface/";

void RuntimeContext::fillInStackTrace(std::vector<std::string> &stackTrace) {
    static const unsigned long lowOrderMask = ~((1L << 32) - 1);
    static const unsigned long highOrderMask = ~(((1L << 32) - 1) << 32);

    for(auto iter = this->callStack.rbegin(); iter != this->callStack.rend(); ++iter) {
        unsigned long frame = *iter;
        std::string str("from ");
        unsigned long funcCtxIndex = (frame & lowOrderMask) >> 32;
        Node *node = this->funcContextStack[funcCtxIndex];
        const char *sourceNmae = node->getSourceName();
        unsigned long lineNum = frame & highOrderMask;

        str += sourceNmae;
        str += ":";
        str += std::to_string(lineNum);
        str += " '";

        FunctionNode *funcNode = dynamic_cast<FunctionNode *>(node);
        if(funcNode != 0) {
            str += "function ";
            str += funcNode->getFuncName();
        } else {
            str += "<toplevel>";
        }
        str += "()'";

        stackTrace.push_back(std::move(str));
    }
}

void RuntimeContext::printStackTop(DSType *stackTopType) {
    if(!stackTopType->isVoidType()) {
        std::cout << "(" << this->pool.getTypeName(*stackTopType)
                            << ") " << this->pop()->toString(*this) << std::endl;
    }
}

bool RuntimeContext::checkCast(unsigned int lineNum, DSType *targetType) {
    DSType *stackTopType = this->peek()->getType();
    if(!targetType->isAssignableFrom(stackTopType)) {
        this->pop();
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
    if(targetType->isAssignableFrom(this->pop()->getType())) {
        this->push(this->trueObj);
    } else {
        this->push(this->falseObj);
    }
}

void RuntimeContext::checkAssertion(unsigned int lineNum) {
    if(!TYPE_AS(Boolean_Object, this->pop())->getValue()) {
        this->pushCallFrame(lineNum);
        std::vector<std::string> stackTrace;
        this->fillInStackTrace(stackTrace);
        this->popCallFrame();

        // print stack trace
        std::cerr << "Assertion Error" << std::endl;
        for(const std::string &str : stackTrace) {
            std::cerr << "    " << str << std::endl;
        }
        exit(ASSERTION_ERROR);
    }
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

const char *RuntimeContext::registerSourceName(const char *sourceName) {
    if(sourceName == 0) {
        return this->readFiles[defaultFileNameIndex].c_str();
    }
    this->readFiles.push_back(std::string(sourceName));
    return this->readFiles.back().c_str();
}

} // namespace core
} // namespace ydsh
