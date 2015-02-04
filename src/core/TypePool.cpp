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

#include <core/TypePool.h>
#include <core/DSType.h>
#include <core/TypeLookupError.h>
#include <core/TypeTemplate.h>

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
    return 0;   //TODO:
}

DSType *TypePool::getVoidType() {
    return 0;   //TODO:
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

DSType *TypePool::getBaseFuncType() {
    return 0;   //TODO:
}

DSType *TypePool::getProcArgType( ){
    return 0;   //TODO:
}

DSType *TypePool::getProcType() {
    return 0;   //TODO:
}

TypeTemplate *TypePool::getArrayTemplate() {
    return 0;	//TODO:
}

TypeTemplate *TypePool::getMapTemplate() {
    return 0;	//TODO:
}

TypeTemplate *TypePool::getPairTemplate() {
    return 0;	//TODO:
}

DSType *TypePool::getType(const std::string &typeName) {
    auto iter = this->typeMap.find(typeName);
    return iter != this->typeMap.end() ? iter->second : 0;
}

DSType *TypePool::getTypeAndThrowIfUndefined(const std::string &typeName) {
    DSType *type = this->getType(typeName);
    if(type == 0) {
        E_UndefinedType(typeName);
    }
    return type;
}

TypeTemplate *TypePool::getTypeTemplate(const std::string &typeName, int elementSize) {
    return 0;   //FIXME:
}

DSType *TypePool::createAndGetReifiedTypeIfUndefined(TypeTemplate *typeTemplate,
        const std::vector<DSType*> &elementTypes) {
    return 0;   //FIXME:
}

FunctionType *TypePool::createAndGetFuncTypeIfUndefined(DSType *returnType,
        const std::vector<DSType*> &paramTypes) {   //FIXME: not use typeMap
    std::string typeName = toFunctionTypeName(returnType, paramTypes);
    FunctionType *funcType = new FunctionType(this->getBaseFuncType(), returnType, paramTypes);
    auto pair = this->typeMap.insert(std::make_pair(typeName, funcType));
    if(!pair.second) {
        delete funcType;
    }
    return dynamic_cast<FunctionType*>(pair.first->second);
}
