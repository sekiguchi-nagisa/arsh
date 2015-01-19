/*
 * TypePool.cpp
 *
 *  Created on: 2015/01/10
 *      Author: skgchxngsxyz-osx
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
