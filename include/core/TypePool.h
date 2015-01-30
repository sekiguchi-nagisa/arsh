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

class TypeTemplate;
class DSType;
class FunctionType;

class TypePool {
private:
    std::unordered_map<std::string, DSType*> typeMap;

public:
    TypePool();
    ~TypePool();

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
    DSType *getBaseFuncType();

    // for reified type.
    TypeTemplate *getArrayTemplate();
    TypeTemplate *getMapTemplate();
    TypeTemplate *getPairTemplate();

    // for type lookup

    /**
     * return null, if type is not defined.
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
    TypeTemplate *getTypeTemplate(const std::string &typeName, int elementSize);

    DSType *createAndGetReifiedTypeIfUndefined(TypeTemplate *typeTemplate, const std::vector<DSType*> &elementTypes);

    FunctionType *createAndGetFuncTypeIfUndefined(DSType *returnType, const std::vector<DSType*> &paramTypes);
};


#endif /* YDSH_INCLUDE_CORE_TYPEPOOL_H_ */
