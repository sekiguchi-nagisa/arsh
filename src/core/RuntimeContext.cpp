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

// ############################
// ##     RuntimeContext     ##
// ############################

#define DEFAULT_TABLE_SIZE 32
#define DEFAULT_LOCAL_SIZE 256

RuntimeContext::RuntimeContext() :
        globalVarTable(new std::shared_ptr<DSObject>[DEFAULT_TABLE_SIZE]),
        tableSize(DEFAULT_TABLE_SIZE),
        thrownObject(std::shared_ptr<DSObject>(nullptr)),
        localStack(new std::shared_ptr<DSObject>[DEFAULT_LOCAL_SIZE]),
        localStackSize(DEFAULT_LOCAL_SIZE), stackTopIndex(0), localVarOffset(0) {
}

RuntimeContext::~RuntimeContext() {
    delete[] this->globalVarTable;
    this->globalVarTable = 0;

    delete[] this->localStack;
    this->localStack = 0;
}

void RuntimeContext::reserveGlobalVar(unsigned int size) {
    if(this->tableSize < size) {
        unsigned int newSize = this->tableSize;
        do {
            newSize *= 2;
        } while(newSize < size);
        std::shared_ptr<DSObject> *newTable = new std::shared_ptr<DSObject>[newSize];
        for(unsigned int i = 0; i < this->tableSize; i++) {
            newTable[i] = this->globalVarTable[i];
        }
        delete[] this->globalVarTable;
        this->globalVarTable = newTable;
        this->tableSize = newSize;
    }
}

void RuntimeContext::clearThrownObject() {
    this->thrownObject.reset();
}

void RuntimeContext::expandLocalStack() {
    unsigned int newSize = this->localStackSize;
    do {
        newSize *= 2;
    } while(newSize < this->localStackSize);
    auto newTable = new std::shared_ptr<DSObject>[newSize];
    for(unsigned int i = 0; i < this->localStackSize; i++) {
        newTable[i] = this->localStack[i];
    }
    delete[] this->localStack;
    this->localStack = newTable;
    this->localStackSize = newSize;
}

inline std::shared_ptr<DSObject> RuntimeContext::pop() {
    std::shared_ptr<DSObject> obj = this->localStack[this->stackTopIndex];
    this->localStack[this->stackTopIndex] = std::shared_ptr<DSObject>();
    this->stackTopIndex--;
    return obj;
}
