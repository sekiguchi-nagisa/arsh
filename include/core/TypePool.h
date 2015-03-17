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

#ifndef CORE_TYPEPOOL_H_
#define CORE_TYPEPOOL_H_

#include <string>
#include <vector>
#include <unordered_map>

class TypeTemplate;
class DSType;
class FunctionType;

class TypePool {
private:
    /**
     * for class type
     */
    std::unordered_map<std::string, DSType*> typeMap;

    // type definition
    DSType *anyType;
    DSType *voidType;

    /**
     * super type of value type(int, float, bool, string)
     * not directly used it.
     */
    DSType *valueType;

    DSType *intType;
    DSType *floatType;
    DSType *boolType;
    DSType *stringType;
    DSType *taskType;
    DSType *baseFuncType;

    /**
     * for type template
     */
    std::unordered_map<std::string, TypeTemplate*> templateMap;

    // type template definition
    TypeTemplate *arrayTemplate;
    TypeTemplate *mapTemplate;

    /**
     * pseudo type template for Tuple type
     */
    TypeTemplate *tupleTemplate;

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

    DSType *getValueType();

    /**
     * int is 32bit.
     */
    DSType *getIntType();

    /**
     * float is 64bit.
     */
    DSType *getFloatType();

    DSType *getBooleanType();
    DSType *getStringType();
    DSType *getTaskType();
    DSType *getBaseFuncType();

    // for reified type.
    TypeTemplate *getArrayTemplate();
    TypeTemplate *getMapTemplate();
    TypeTemplate *getTupleTemplate();

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

    /**
     * if type template is Tuple, call createAndGetTupleTypeIfUndefined()
     */
    DSType *createAndGetReifiedTypeIfUndefined(TypeTemplate *typeTemplate, const std::vector<DSType*> &elementTypes);

    DSType *createAndGetTupleTypeIfUndefined(const std::vector<DSType*> &elementTypes);

    FunctionType *createAndGetFuncTypeIfUndefined(DSType *returnType, const std::vector<DSType*> &paramTypes);

private:
    void checkElementTypes(const std::vector<DSType*> &elementTypes);
};


#endif /* CORE_TYPEPOOL_H_ */
