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

#include "TypePool.h"
#include "DSType.h"
#include "../parser/TypeLookupError.h"

// ######################
// ##     TypePool     ##
// ######################

TypePool::TypePool() :
        typeMap() {
}

TypePool::~TypePool() {
    for(const std::pair<std::string, DSType*> &pair : this->typeMap) {
        delete pair.second;
    }
    this->typeMap.clear();
}

DSType *TypePool::getAnyType() {
    return ClassType::anyType;
}

DSType *TypePool::getVoidType() {
    return ClassType::voidType;
}

DSType *TypePool::getIntType() {
    return this->getInt64Type();
}

DSType *TypePool::getInt64Type() {
    return 0;	// TODO
}

DSType *TypePool::getFloatType() {
    return 0;	// TODO
}

DSType *TypePool::getBooleanType() {
    return 0;	// TODO
}

DSType *TypePool::getStringType() {
    return 0;	//TODO:
}

DSType *TypePool::getTaskType() {
    return 0;   //TODO:
}

DSType *TypePool::getBaseArrayType() {
    return 0;	//TODO:
}

DSType *TypePool::getBaseMapType() {
    return 0;	//TODO:
}

DSType *TypePool::getBasePairType() {
    return 0;	//TODO:
}

DSType *TypePool::getType(const std::string &typeName) {
    DSType *type = this->typeMap[typeName];
    //TODO: check template type
    return type;
}

DSType *TypePool::getTypeAndThrowIfUndefined(const std::string &typeName) {
    DSType *type = this->getType(typeName);
    if(type == 0) {
        E_UndefinedType->report(typeName);
    }
    return type;
}

DSType *TypePool::getTemplateType(const std::string &typeName, int elementSize) {
    return 0;   //FIXME:
}

DSType *TypePool::createAndGetReifiedTypeIfUndefined(DSType *templateType,
        const std::vector<DSType*> &elementTypes) {
    return 0;   //FIXME:
}

FunctionType *TypePool::createAndGetFuncTypeIfUndefined(DSType *returnType,
        const std::vector<DSType*> &paramTypes) {
    std::string typeName = toFunctionTypeName(returnType, paramTypes);
    DSType *funcType = this->getType(typeName);
    if(funcType == 0) {
        funcType = new FunctionType(returnType, paramTypes);
        this->typeMap[typeName] = funcType;
    }
    return dynamic_cast<FunctionType*>(funcType);
}
