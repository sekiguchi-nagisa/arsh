/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cstring>
#include <dirent.h>
#include <csignal>

#include <ydsh/ydsh.h>
#include "state.h"
#include "symbol.h"
#include "logger.h"
#include "misc/files.h"

static std::string initLogicalWorkingDir() {
    const char *dir = getenv(ENV_PWD);
    if(dir == nullptr || !S_ISDIR(getStMode(dir))) {
        size_t size = PATH_MAX;
        char buf[size];
        const char *cwd = getcwd(buf, size);
        return std::string(cwd != nullptr ? cwd : "");
    }
    if(dir[0] == '/') {
        return std::string(dir);
    } else {
        return expandDots(nullptr, dir);
    }
}

// #####################
// ##     DSState     ##
// #####################

DSState::DSState() :
        pool(), symbolTable(),
        trueObj(new Boolean_Object(this->pool.getBooleanType(), true)),
        falseObj(new Boolean_Object(this->pool.getBooleanType(), false)),
        emptyStrObj(new String_Object(this->pool.getStringType(), std::string())),
        dummy(new DummyObject()), thrownObject(),
        callStack(new DSValue[DEFAULT_STACK_SIZE]),
        callStackSize(DEFAULT_STACK_SIZE), globalVarSize(0),
        stackTopIndex(0), stackBottomIndex(0), localVarOffset(0), pc_(0),
        option(DS_OPTION_ASSERT), IFS_index(0),
        codeStack(), pipelineEvaluator(nullptr),
        pathCache(), terminationHook(nullptr), lineNum(1), prompt(),
        hook(nullptr), logicalWorkingDir(initLogicalWorkingDir()) { }

void DSState::expandLocalStack() {
    const unsigned int needSize = this->stackTopIndex;
    if(needSize >= MAXIMUM_STACK_SIZE) {
        this->stackTopIndex = this->callStackSize - 1;
        throwError(*this, this->pool.getStackOverflowErrorType(), "local stack size reaches limit");
    }

    unsigned int newSize = this->callStackSize;
    do {
        newSize += (newSize >> 1);
    } while(newSize < needSize);
    auto newTable = new DSValue[newSize];
    for(unsigned int i = 0; i < this->callStackSize; i++) {
        newTable[i] = std::move(this->callStack[i]);
    }
    delete[] this->callStack;
    this->callStack = newTable;
    this->callStackSize = newSize;
}