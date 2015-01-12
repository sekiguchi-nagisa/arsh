/*
 * TypePool.cpp
 *
 *  Created on: 2015/01/10
 *      Author: skgchxngsxyz-osx
 */

#include "TypePool.h"
#include "DSType.h"

// ######################
// ##     TypePool     ##
// ######################

TypePool::TypePool() {
    // TODO Auto-generated constructor stub

}

TypePool::~TypePool() {
    // TODO Auto-generated destructor stub
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

DSType *TypePool::getBaseArrayType() {
    return 0;	//TODO:
}

DSType *TypePool::getBaseMapType() {
    return 0;	//TODO:
}

DSType *TypePool::getBasePairType() {
    return 0;	//TODO:
}

DSType *getType(const std::string &typeName) {
    return 0;	//TODO:
}
