/*
 * TypePool.h
 *
 *  Created on: 2015/01/10
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_TYPEPOOL_H_
#define CORE_TYPEPOOL_H_

#include <string>
#include <vector>
#include <unordered_map>

class DSType;
class FunctionType;

class TypePool {
private:
    std::unordered_map<std::string, DSType*> typeMap;

public:
    TypePool();
    virtual ~TypePool();

    /**
     * get any type (root class of ydsh class)
     */
    DSType *getAnyType();

    /**
     * get void type (pseudo class representing for void)
     */
    DSType *getVoidType();

    /**
     * equivalent to getInt64Type()
     */
    DSType *getIntType();
    DSType *getInt64Type();
    DSType *getFloatType();
    DSType *getBooleanType();
    DSType *getStringType();
    DSType *getTaskType();

    // for reified type.
    DSType *getBaseArrayType(); //TODO: return type
    DSType *getBaseMapType();	// TODO: return type
    DSType *getBasePairType();	// TODO: return type

    // for type lookup

    /**
     * return null, if type is not defined.
     * cannot get TemplateType(array.. etc)
     */
    DSType *getType(const std::string &typeName);

    /**
     * get type except template type.
     * if type is undefined, throw exception
     */
    DSType *getTypeAndThrowIfUndefined(const std::string &typeName);

    /**
     * get template type.
     * if template type is not found, throw exception
     */
    DSType *getTemplateType(const std::string &typeName, int elementSize);  //FIXME: return type

    //TODO: template type
    DSType *createAndGetReifiedTypeIfUndefined(DSType *templateType, const std::vector<DSType*> &elementTypes);

    FunctionType *createAndGetFuncTypeIfUndefined(DSType *returnType, const std::vector<DSType*> &paramTypes);
};


#endif /* CORE_TYPEPOOL_H_ */
