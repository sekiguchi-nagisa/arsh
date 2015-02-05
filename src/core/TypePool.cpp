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

#define INIT_CLASS_TYPE(name, extendable, superType) \
    this->typeMap.insert(\
            std::make_pair(name, \
                    new ClassType(name, extendable, superType))).first->second

#define INIT_TYPE_TEMPLATE(name, elemSize) \
    this->templateMap.insert(\
            std::make_pair(name, \
                    new TypeTemplate(name, elemSize))).first->second



// ######################
// ##     TypePool     ##
// ######################

TypePool::TypePool() :
        typeMap(16), anyType(), voidType(), valueType(),
        intType(), floatType(), boolType(), stringType(),
        taskType(), baseFuncType(),
        procArgType(), procType(),
        templateMap(8),
        arrayTemplate(), mapTemplate(), pairTemplate() {

    // initialize type
    this->anyType    = INIT_CLASS_TYPE("Any", true, 0);
    this->voidType   = INIT_CLASS_TYPE("Void", false, 0);

    /**
     * hidden from script.
     */
    this->valueType  = INIT_CLASS_TYPE("%Value%", true, this->anyType);

    this->intType    = INIT_CLASS_TYPE("Int", false, this->valueType);
    this->floatType  = INIT_CLASS_TYPE("Float", false, this->valueType);
    this->boolType   = INIT_CLASS_TYPE("Boolean", false, this->valueType);
    this->stringType = INIT_CLASS_TYPE("String", false, this->valueType);
    this->taskType   = INIT_CLASS_TYPE("Task", false, this->anyType);

    /**
     * hidden from script
     */
    this->baseFuncType = INIT_CLASS_TYPE("%BaseFunc%", false, this->anyType);

    /**
     * hidden from script
     */
    this->procArgType  = INIT_CLASS_TYPE("%ProcArg%", false, this->anyType);

    /**
     * hidden from script
     */
    this->procType     = INIT_CLASS_TYPE("%Proc%", false, this->anyType);

    // initialize type template
    this->arrayTemplate = INIT_TYPE_TEMPLATE("Array", 1);
    this->mapTemplate   = INIT_TYPE_TEMPLATE("Map", 1); //FIXME: element size will be 2.
    this->pairTemplate  = INIT_TYPE_TEMPLATE("Pair", 2);    //FIXME: replace to Tuple
}

TypePool::~TypePool() {
    for(const std::pair<std::string, DSType*> &pair : this->typeMap) {
        delete pair.second;
    }
    this->typeMap.clear();

    for(const std::pair<std::string, TypeTemplate*> &pair : this->templateMap) {
        delete pair.second;
    }
    this->templateMap.clear();
}

DSType *TypePool::getAnyType() {
    return this->anyType;
}

DSType *TypePool::getVoidType() {
    return this->voidType;
}

DSType *TypePool::getValueType() {
    return this->valueType;
}

DSType *TypePool::getIntType() {
    return this->getInt64Type();
}

DSType *TypePool::getInt64Type() {
    return this->intType;
}

DSType *TypePool::getFloatType() {
    return this->floatType;
}

DSType *TypePool::getBooleanType() {
    return this->boolType;
}

DSType *TypePool::getStringType() {
    return this->stringType;
}

DSType *TypePool::getTaskType() {
    return this->taskType;
}

DSType *TypePool::getBaseFuncType() {
    return this->baseFuncType;
}

DSType *TypePool::getProcArgType( ){
    return this->procArgType;
}

DSType *TypePool::getProcType() {
    return this->procType;
}

TypeTemplate *TypePool::getArrayTemplate() {
    return this->arrayTemplate;
}

TypeTemplate *TypePool::getMapTemplate() {
    return this->mapTemplate;
}

TypeTemplate *TypePool::getPairTemplate() {
    return this->pairTemplate;
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
    auto iter = this->templateMap.find(typeName);
    if(iter == this->templateMap.end()) {
        E_NotGenericBase(typeName);
    }
    return iter->second;
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
