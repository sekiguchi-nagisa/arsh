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
#include <util/debug.h>

#include <iostream>

// ############################
// ##     RuntimeContext     ##
// ############################

#define DEFAULT_TABLE_SIZE 32
#define DEFAULT_LOCAL_SIZE 256

RuntimeContext::RuntimeContext() :
        pool(),
        trueObj(new Boolean_Object(this->pool.getBooleanType(), true)),
        falseObj(new Boolean_Object(this->pool.getBooleanType(), false)),
        globalVarTable(new std::shared_ptr<DSObject>[DEFAULT_TABLE_SIZE]),
        tableSize(DEFAULT_TABLE_SIZE),
        thrownObject(std::shared_ptr<DSObject>(nullptr)),
        localStack(new std::shared_ptr<DSObject>[DEFAULT_LOCAL_SIZE]),
        localStackSize(DEFAULT_LOCAL_SIZE), stackTopIndex(0),
        localVarOffset(0), repl(false) {
}

RuntimeContext::~RuntimeContext() {
    delete[] this->globalVarTable;
    this->globalVarTable = 0;

    delete[] this->localStack;
    this->localStack = 0;
}

void RuntimeContext::printStackTop(DSType *stackTopType) {
    if(!stackTopType->equals(this->pool.getVoidType())) {
        std::cout << "(" << stackTopType->getTypeName()
                << ") " << this->pop()->toString() << std::endl;
    }
}

void RuntimeContext::checkCast(DSType *targetType) {
    if(!this->peek()->getType()->isAssignableFrom(targetType)) {
        this->pop();
        //FIXME: throw TypeCastException
        fatal("throw TypeCastException\n");
    }
}

void RuntimeContext::instanceOf(DSType *targetType) {
    if(this->pop()->getType()->isAssignableFrom(targetType)) {
        this->push(this->trueObj);
    } else {
        this->push(this->falseObj);
    }
}

void RuntimeContext::assertion() {
    if(!TYPE_AS(Boolean_Object, this->pop())->getValue()) {
        fatal("assertion error\n"); //FIXME:
    }
}

