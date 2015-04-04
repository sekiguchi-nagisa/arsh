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
#include <core/TypeLookupError.h>
#include <core/bind.h>

#define NEW_ID() this->idCount++

namespace ydsh {
namespace core {

// ######################
// ##     TypePool     ##
// ######################

TypePool::TypePool(char **envp) :
        idCount(0), typeMap(16), typeNameTable(),
        anyType(), voidType(), valueType(),
        intType(), floatType(), boolType(), stringType(),
        taskType(), baseFuncType(),
        templateMap(8),
        arrayTemplate(), mapTemplate(), tupleTemplate(),
        envp(envp), envSet() {

    // initialize type
    this->anyType = this->initBuiltinType("Any", true, 0, info_AnyType());
    this->voidType = this->initBuiltinType("Void", false, 0, info_VoidType(), true);

    /**
     * hidden from script.
     */
    this->valueType = this->initBuiltinType("%Value%", true, this->anyType, info_Dummy());

    this->intType = this->initBuiltinType("Int", false, this->valueType, info_IntType());
    this->floatType = this->initBuiltinType("Float", false, this->valueType, info_FloatType());
    this->boolType = this->initBuiltinType("Boolean", false, this->valueType, info_BooleanType());
    this->stringType = this->initBuiltinType("String", false, this->valueType, info_StringType());
    this->taskType = this->initBuiltinType("Task", false, this->anyType, info_Dummy());

    /**
     * hidden from script
     */
    this->baseFuncType = this->initBuiltinType("%BaseFunc%", false, this->anyType, info_Dummy());

    // initialize type template
    this->arrayTemplate = this->initTypeTemplate("Array", 1, info_Dummy());
    this->mapTemplate = this->initTypeTemplate("Map", 2, info_Dummy());
    this->tupleTemplate = this->initTypeTemplate("Tuple", 0, 0);   // pseudo template.
}

TypePool::~TypePool() {
    for(const std::pair<std::string, DSType *> &pair : this->typeMap) {
        delete pair.second;
    }
    this->typeMap.clear();

    for(const std::pair<std::string, TypeTemplate *> &pair : this->templateMap) {
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

TypeTemplate *TypePool::getArrayTemplate() {
    return this->arrayTemplate;
}

TypeTemplate *TypePool::getMapTemplate() {
    return this->mapTemplate;
}

TypeTemplate *TypePool::getTupleTemplate() {
    return this->tupleTemplate;
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
                                                     const std::vector<DSType *> &elementTypes) {
    if(this->tupleTemplate->getName() == typeTemplate->getName()) {
        return this->createAndGetTupleTypeIfUndefined(elementTypes);
    }
    this->checkElementTypes(elementTypes);

    if(typeTemplate->getElementTypeSize() != elementTypes.size()) {
        E_UnmatchElement(typeTemplate->getName(),
                         std::to_string(typeTemplate->getElementTypeSize()), std::to_string(elementTypes.size()));
    }

    std::string typeName(this->toReifiedTypeName(typeTemplate, elementTypes));
    auto iter = this->typeMap.find(typeName);
    if(iter == this->typeMap.end()) {
        return this->addType(std::move(typeName),
                             newReifiedType(NEW_ID(), typeTemplate->getInfo(), this->anyType, elementTypes));
    }
    return iter->second;
}

DSType *TypePool::createAndGetTupleTypeIfUndefined(const std::vector<DSType *> &elementTypes) {
    this->checkElementTypes(elementTypes);

    if(elementTypes.size() < 2) {
        E_TupleElement();
    }

    std::string typeName(this->toTupleTypeName(elementTypes));
    auto iter = this->typeMap.find(typeName);
    if(iter == this->typeMap.end()) {
        return this->addType(std::move(typeName), newTupleType(NEW_ID(), this->anyType, elementTypes));
    }
    return iter->second;
}

FunctionType *TypePool::createAndGetFuncTypeIfUndefined(DSType *returnType,
                                                        const std::vector<DSType *> &paramTypes) {
    this->checkElementTypes(paramTypes);

    std::string typeName(toFunctionTypeName(returnType, paramTypes));
    auto iter = this->typeMap.find(typeName);
    if(iter == this->typeMap.end()) {
        FunctionType *funcType =
                new FunctionType(NEW_ID(), this->getBaseFuncType(), returnType, paramTypes);
        this->addType(std::move(typeName), funcType);
        return funcType;
    }

    return dynamic_cast<FunctionType *>(iter->second);
}

const std::string &TypePool::getTypeName(const DSType &type) {
    return *this->typeNameTable[type.getTypeId()];
}

std::string TypePool::toReifiedTypeName(TypeTemplate *typeTemplate, const std::vector<DSType *> &elementTypes) {
    return this->toReifiedTypeName(typeTemplate->getName(), elementTypes);
}

std::string TypePool::toReifiedTypeName(const std::string &name, const std::vector<DSType *> &elementTypes) {
    int elementSize = elementTypes.size();
    std::string reifiedTypeName(name);
    reifiedTypeName += "<";
    for(int i = 0; i < elementSize; i++) {
        if(i > 0) {
            reifiedTypeName += ",";
        }
        reifiedTypeName += this->getTypeName(*elementTypes[i]);
    }
    reifiedTypeName += ">";
    return reifiedTypeName;
}


std::string TypePool::toTupleTypeName(const std::vector<DSType *> &elementTypes) {
    return toReifiedTypeName("Tuple", elementTypes);
}

std::string TypePool::toFunctionTypeName(DSType *returnType, const std::vector<DSType *> &paramTypes) {
    int paramSize = paramTypes.size();
    std::string funcTypeName("Func<");
    funcTypeName += this->getTypeName(*returnType);
    for(int i = 0; i < paramSize; i++) {
        if(i == 0) {
            funcTypeName += ",[";
        }
        if(i > 0) {
            funcTypeName += ",";
        }
        funcTypeName += this->getTypeName(*paramTypes[i]);
        if(i == paramSize - 1) {
            funcTypeName += "]";
        }
    }
    funcTypeName += ">";
    return funcTypeName;
}

bool TypePool::hasEnv(const std::string &envName) {
    if(this->envSet.empty()) {
        this->initEnvSet();
    }
    return this->envSet.find(envName) != this->envSet.end();
}

void TypePool::addEnv(const std::string &envName) {
    if(this->envSet.empty()) {
        this->initEnvSet();
    }
    this->envSet.insert(envName);
}

void TypePool::initEnvSet() {
    for(unsigned int i = 0; this->envp[i] != NULL; i++) {
        char *env = this->envp[i];
        std::string str;
        for(unsigned int j = 0; env[j] != '\0'; j++) {
            char ch = env[j];
            if(ch == '=') {
                break;
            }
            str += ch;
        }
        this->envSet.insert(std::move(str));
    }
}

DSType *TypePool::addType(std::string &&typeName, DSType *type) {
    auto pair = this->typeMap.insert(std::make_pair(std::move(typeName), type));
    this->typeNameTable.push_back(&pair.first->first);
    return type;
}

DSType *TypePool::initBuiltinType(const char *typeName, bool extendable,
                                  DSType *superType, native_type_info_t *info, bool isVoid) {
    // create and register type
    return this->addType(std::string(typeName),
                         newBuiltinType(NEW_ID(), extendable, superType, info, isVoid));
}

TypeTemplate *TypePool::initTypeTemplate(const char *typeName,
                                         unsigned int elemSize, native_type_info_t *info) {
    return this->templateMap.insert(
            std::make_pair(typeName,
                           new TypeTemplate(typeName, elemSize, info))).first->second;
}

void TypePool::checkElementTypes(const std::vector<DSType *> &elementTypes) {
    for(DSType *type : elementTypes) {
        if(*type == *this->voidType) {
            E_InvalidElement(this->getTypeName(*type));
        }
    }
}

} // namespace core
} // namespace ydsh
