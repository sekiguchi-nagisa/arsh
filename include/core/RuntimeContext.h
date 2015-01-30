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

#ifndef CORE_RUNTIMECONTEXT_H_
#define CORE_RUNTIMECONTEXT_H_

#include <core/DSObject.h>
#include <vector>


class RuntimeContext {
private:
    /**
     * contains global variables(or function)
     */
    std::vector<DSObject*> globalVarTable;

    /**
     * if not null, thrown exception.
     */
    DSObject *thrownObject;

public:
    RuntimeContext();
    virtual ~RuntimeContext();

    /**
     * add new global variable or function
     */
    void addGlobalVar(DSObject *obj);

    /**
     * update exist global variable.
     * this is not type-safe method
     */
    void updateGlobalVar(int varIndex, DSObject *obj);

    /**
     * this is not type-safe method
     */
    DSObject *getGlobalVar(int index);

    int getGlobalVarSize();

    void setThrownObject(DSObject *obj);
    void clearThrownObject();

    /**
     * return null, if not thrown
     */
    DSObject *getThrownObject();
};

#endif /* CORE_RUNTIMECONTEXT_H_ */
