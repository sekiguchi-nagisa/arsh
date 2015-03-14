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

struct RuntimeContext {
    /**
     * contains global variables(or function)
     */
    std::shared_ptr<DSObject> *globalVarTable;

    /**
     * size of global variable table.
     */
    unsigned int tableSize;

    /**
     * if not null ptr, thrown exception.
     */
    std::shared_ptr<DSObject> thrownObject;

    /**
     * contains operand or local variable
     */
    std::shared_ptr<DSObject> *localStack;

    unsigned int localStackSize;
    unsigned int stackTopIndex;

    /**
     * offset current local variable index.
     */
    unsigned int localVarOffset;

    RuntimeContext();
    ~RuntimeContext();

    /**
     * if this->tableSize < size, expand globalVarTable.
     */
    void reserveGlobalVar(unsigned int size);

    /**
     * reset this->throwObject.
     */
    void clearThrownObject();

    void expandLocalStack();
    std::shared_ptr<DSObject> pop();
};

// helper macro for RuntimeContext manipulation.
#define SET_GVAR(ctx, index, val) ctx.globalVarTable[index] = val
#define GET_GVAR(ctx, index)      ctx.globalVarTable[index]

#define SET_LVAR(ctx, index, val) ctx.localStack[ctx.localVarOffset + index] = val
#define GET_LVAR(ctx, index)      ctx.localStack[ctx.localVarOffset + index]

#define PUSH(ctx, val)            \
    do {\
        if(ctx.stackTopIndex >= ctx.localStackSize) { ctx.expandLocalStack(); }\
        ctx.localStack[ctx.stackTopIndex++] = val;\
    } while(0)

#define POP(ctx)                  ctx.pop()

#endif /* CORE_RUNTIMECONTEXT_H_ */
