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

RuntimeContext::RuntimeContext() :
        globalVarTable(), thrownObject(0) {
}

RuntimeContext::~RuntimeContext() {
    for(DSObject *o : this->globalVarTable) {
        delete o;
    }
    this->globalVarTable.clear();
}

void RuntimeContext::addGlobalVar(DSObject *obj) {
    this->globalVarTable.push_back(obj);
}

void RuntimeContext::updateGlobalVar(int varIndex, DSObject *obj) {
    this->globalVarTable[varIndex] = obj;
}

DSObject *RuntimeContext::getGlobalVar(int index) {
    return this->globalVarTable[index];
}

int RuntimeContext::getGlobalVarSize() {
    return this->globalVarTable.size();
}

void RuntimeContext::setThrownObject(DSObject *obj) {
    this->thrownObject = obj;
}

void RuntimeContext::clearThrownObject() {
    this->thrownObject = 0;
}

DSObject *RuntimeContext::getThrownObject() {
    return this->thrownObject;
}

