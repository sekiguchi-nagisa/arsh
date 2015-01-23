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

#ifndef YDSH_INCLUDE_CORE_TYPEPOOL_H_
#define YDSH_INCLUDE_CORE_TYPEPOOL_H_

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


#endif /* YDSH_INCLUDE_CORE_TYPEPOOL_H_ */
