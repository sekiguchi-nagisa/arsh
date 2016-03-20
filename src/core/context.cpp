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
#include <sys/wait.h>
#include <sys/stat.h>

#include <cstring>
#include <dirent.h>
#include <csignal>

#include <config.h>
#include "context.h"
#include "symbol.h"
#include "logger.h"
#include "../parser/parser.h"
#include "../misc/num.h"
#include "../misc/time.h"
#include "../misc/files.h"

namespace ydsh {
namespace core {

// ###########################
// ##     FilePathCache     ##
// ###########################

FilePathCache::~FilePathCache() {
    for(auto &pair : this->map) {
        free(const_cast<char *>(pair.first));
    }
}

const char *FilePathCache::searchPath(const char *cmdName, unsigned char option) {
    // if found '/', return fileName
    if(strchr(cmdName, '/') != nullptr) {
        return cmdName;
    }

    // search cache
    if(!hasFlag(option, DIRECT_SEARCH)) {
        auto iter = this->map.find(cmdName);
        if(iter != this->map.end()) {
            return iter->second.c_str();
        }
    }

    // get PATH
    const char *pathPrefix = getenv(ENV_PATH);
    if(pathPrefix == nullptr || hasFlag(option, USE_DEFAULT_PATH)) {
        pathPrefix = VAR_DEFAULT_PATH;
    }

    // resolve path
    std::string resolvedPath;
    for(unsigned int i = 0; !resolvedPath.empty() || pathPrefix[i] != '\0'; i++) {
        char ch = pathPrefix[i];
        bool stop = false;

        if(ch == '\0') {
            stop = true;
        } else if(ch != ':') {
            resolvedPath += ch;
            continue;
        }
        if(resolvedPath.empty()) {
            continue;
        }

        if(resolvedPath.back() != '/') {
            resolvedPath += '/';
        }
        resolvedPath += cmdName;

        if(resolvedPath[0] == '~') {
            resolvedPath = expandTilde(resolvedPath.c_str());
        }

        struct stat st;
        if(stat(resolvedPath.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) == S_IXUSR) {
            if(hasFlag(option, DIRECT_SEARCH)) {
                this->prevPath = std::move(resolvedPath);
                return this->prevPath.c_str();
            } else {
                // set to cache
                if(this->map.size() == MAX_CACHE_SIZE) {
                    free(const_cast<char *>(this->map.begin()->first));
                    this->map.erase(this->map.begin());
                }
                auto pair = this->map.insert(std::make_pair(strdup(cmdName), std::move(resolvedPath)));
                assert(pair.second);
                return pair.first->second.c_str();
            }
        }
        resolvedPath.clear();

        if(stop) {
            break;
        }
    }

    // not found
    return nullptr;
}

void FilePathCache::removePath(const char *cmdName) {
    if(cmdName != nullptr) {
        auto iter = this->map.find(cmdName);
        if(iter != this->map.end()) {
            delete iter->first;
            this->map.erase(iter);
        }
    }
}

bool FilePathCache::isCached(const char *cmdName) const {
    return this->map.find(cmdName) != this->map.end();
}

void FilePathCache::clear() {
    for(auto &pair : this->map) {
        delete pair.first;
    }
    this->map.clear();
}


// ############################
// ##     RuntimeContext     ##
// ############################

RuntimeContext::RuntimeContext() :
        pool(), symbolTable(),
        trueObj(new Boolean_Object(this->pool.getBooleanType(), true)),
        falseObj(new Boolean_Object(this->pool.getBooleanType(), false)),
        emptyStrObj(new String_Object(this->pool.getStringType(), std::string())),
        dummy(new DummyObject()), thrownObject(),
        localStack(new DSValue[DEFAULT_LOCAL_SIZE]),
        localStackSize(DEFAULT_LOCAL_SIZE), stackTopIndex(0), stackBottomIndex(0),
        localVarOffset(0), toplevelPrinting(false), assertion(true),
        handle_STR(nullptr), handle_bt(nullptr), IFS_index(0),
        callableContextStack(), callStack(),
        pipelineEvaluator(DSValue::create<PipelineEvaluator>()), udcMap(), pathCache() { }

RuntimeContext::~RuntimeContext() {
    delete[] this->localStack;

    for(auto &pair : this->udcMap) {
        delete pair.second;
    }
}

/**
 * path must be full path
 */
static std::vector<std::string> createPathStack(const char *path) {
    std::vector<std::string> stack;
    if(*path == '/') {
        stack.push_back("/");
        path++;
    }

    for(const char *ptr = nullptr; (ptr = strchr(path, '/')) != nullptr;) {
        const unsigned int size = ptr - path;
        if(size == 0) {
            path++;
            continue;
        }
        stack.push_back(std::string(path, size));
        path += size;
    }
    if(*path != '\0') {
        stack.push_back(path);
    }
    return stack;
}

/**
 * path is not null
 */
static std::string expandDots(const char *basePath, const char *path) {
    assert(path != nullptr);
    std::vector<std::string> resolvedPathStack;
    auto pathStack(createPathStack(path));

    // fill resolvedPathStack
    if(!pathStack.empty() && pathStack.front() != "/") {
        if(basePath != nullptr) {
            resolvedPathStack = createPathStack(basePath);
        } else {
            size_t size = PATH_MAX;
            char buf[size];
            const char *cwd = getcwd(buf, size);
            if(cwd != nullptr) {
                resolvedPathStack = createPathStack(cwd);
            }
        }
    }

    for(auto &e : pathStack) {
        if(e == "..") {
            if(!resolvedPathStack.empty()) {
                resolvedPathStack.pop_back();
            }
        } else if(e != ".") {
            resolvedPathStack.push_back(std::move(e));
        }
    }

    // create path
    std::string str;
    const unsigned int size = resolvedPathStack.size();
    for(unsigned int i = 1; i < size; i++) {
        str += '/';
        str += std::move(resolvedPathStack[i]);
    }
    return str;
}

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

std::string RuntimeContext::logicalWorkingDir = initLogicalWorkingDir();

std::string RuntimeContext::getConfigRootDir() {
#ifdef X_CONFIG_DIR
    return std::string(X_CONFIG_DIR);
#else
    std::string path(getenv(ENV_HOME));
    path += "/.ydsh";
    return path;
#endif
}

std::string RuntimeContext::getIfaceDir() {
    std::string root(getConfigRootDir());
    root += "/dbus/iface";
    return root;
}

void RuntimeContext::updateScriptName(const char *name) {
    unsigned int index = this->getBuiltinVarIndex(BuiltinVarOffset::POS_0);
    this->setGlobal(index, DSValue::create<String_Object>(this->pool.getStringType(), std::string(name)));
}

void RuntimeContext::addScriptArg(const char *arg) {
    typeAs<Array_Object>(this->getScriptArgs())->append(
            DSValue::create<String_Object>(this->pool.getStringType(), std::string(arg)));
}

void RuntimeContext::initScriptArg() {
    unsigned int index = this->getBuiltinVarIndex(BuiltinVarOffset::ARGS);
    typeAs<Array_Object>(this->getGlobal(index))->refValues().clear();
}

void RuntimeContext::finalizeScritArg() {
    unsigned int index = this->getBuiltinVarIndex(BuiltinVarOffset::ARGS);
    auto *array = typeAs<Array_Object>(this->getGlobal(index));

    // update argument size
    const unsigned int size = array->getValues().size();
    index = this->getBuiltinVarIndex(BuiltinVarOffset::ARGS_SIZE);
    this->setGlobal(index, DSValue::create<Int_Object>(this->pool.getInt32Type(), size));

    unsigned int limit = 9;
    if(size < limit) {
        limit = size;
    }

    // update positional parameter
    for(index = 0; index < limit; index++) {
        unsigned int i = this->getBuiltinVarIndex(BuiltinVarOffset::POS_1) + index;
        this->setGlobal(i, array->getValues()[index]);
    }

    if(index < 9) {
        for(; index < 9; index++) {
            unsigned int i = this->getBuiltinVarIndex(BuiltinVarOffset::POS_1) + index;
            this->setGlobal(i, this->getEmptyStrObj());
        }
    }
}

void RuntimeContext::reserveGlobalVar(unsigned int size) {
    this->reserveLocalVar(size);
    this->localVarOffset = size;
}

void RuntimeContext::reserveLocalVar(unsigned int size) {
    if(size > this->localStackSize) {
        this->expandLocalStack(size);
    }
    this->stackTopIndex = size;
    this->stackBottomIndex = size;
}

void RuntimeContext::throwError() {
    this->thrownObject = this->pop();
    this->unwindOperandStack();
}

void RuntimeContext::throwError(DSType &errorType, const char *message) {
    this->throwError(errorType, std::string(message));
}

void RuntimeContext::throwError(DSType &errorType, std::string &&message) {
    this->push(Error_Object::newError(*this, errorType, DSValue::create<String_Object>(
            this->pool.getStringType(), std::move(message))));
    this->throwError();
}

void RuntimeContext::throwSystemError(int errorNum, std::string &&message) {
    if(errorNum == 0) {
        fatal("errno is not 0\n");
    }

    std::string str(std::move(message));
    str += ": ";
    str += strerror(errorNum);
    this->throwError(this->pool.getSystemErrorType(), std::move(str));
}

void RuntimeContext::raiseCircularReferenceError() {
    this->throwError(this->pool.getStackOverflowErrorType(), "caused by circular reference");
    throw NativeMethodError();
}


void RuntimeContext::expandLocalStack(unsigned int needSize) {
    unsigned int newSize = this->localStackSize;
    do {
        newSize += (newSize >> 1);
    } while(newSize < needSize);
    auto newTable = new DSValue[newSize];
    for(unsigned int i = 0; i < this->localStackSize; i++) {
        newTable[i] = std::move(this->localStack[i]);
    }
    delete[] this->localStack;
    this->localStack = newTable;
    this->localStackSize = newSize;
}

void RuntimeContext::unwindOperandStack() {
    while(this->stackTopIndex > this->stackBottomIndex) {
        this->popNoReturn();
    }
}

void RuntimeContext::saveAndSetStackState(unsigned int stackTopOffset,
                                          unsigned int paramSize, unsigned int maxVarSize) {
    const unsigned int oldLocalVarOffset = this->localVarOffset;
    const unsigned int oldStackBottomIndex = this->stackBottomIndex;
    const unsigned int oldStackTopIndex = this->stackTopIndex - stackTopOffset;

    // change stack state
    this->localVarOffset = this->stackTopIndex - paramSize + 1;
    this->reserveLocalVar(this->stackTopIndex + maxVarSize - paramSize + 3);

    // push old state
    this->localStack[this->stackTopIndex] = DSValue::createNum(oldLocalVarOffset);
    this->localStack[this->stackTopIndex - 1] = DSValue::createNum(oldStackBottomIndex);
    this->localStack[this->stackTopIndex - 2] = DSValue::createNum(oldStackTopIndex);
}

void RuntimeContext::restoreStackState() {
    this->unwindOperandStack();

    // pop old state
    auto v = this->pop();
    assert(v.kind() == DSValueKind::NUMBER);
    const unsigned int oldLocalVarOffset = static_cast<unsigned int>(v.value());

    v = this->pop();
    assert(v.kind() == DSValueKind::NUMBER);
    const unsigned int oldStackBottomIndex = static_cast<unsigned int>(v.value());

    v = this->pop();
    assert(v.kind() == DSValueKind::NUMBER);
    const unsigned int oldStackTopIndex = static_cast<unsigned int>(v.value());

    // restore state
    this->localVarOffset = oldLocalVarOffset;
    this->stackBottomIndex = oldStackBottomIndex;

    // unwind local variable
    while(this->stackTopIndex > oldStackTopIndex) {
        this->popNoReturn();
    }
}

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+---------+--------+   +--------+
 * | stack top | funcObj | param1 | ~ | paramN |
 * +-----------+---------+--------+   +--------+
 *                       | offset |   |        |
 */
EvalStatus RuntimeContext::applyFuncObject(unsigned int startPos, bool returnTypeIsVoid, unsigned int paramSize) {
    // call function
    auto *func = typeAs<FuncObject>(this->localStack[this->stackTopIndex - paramSize]);
    this->saveAndSetStackState(paramSize + 1, paramSize, func->getFuncNode()->getMaxVarNum());
    this->pushCallFrame(startPos);
    bool status = func->invoke(*this);
    this->popCallFrame();

    // restore stack state
    DSValue returnValue;
    if(status && !returnTypeIsVoid) {
        returnValue = std::move(this->localStack[this->stackTopIndex]);
    }

    this->restoreStackState();

    if(status && !returnTypeIsVoid) {
        this->push(std::move(returnValue));
    }
    return status ? EvalStatus::SUCCESS : EvalStatus::THROW;
}

/**
 * stack state in method call    stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             | offset           |   |        |
 */
EvalStatus RuntimeContext::callMethod(unsigned int startPos, const std::string &methodName, MethodHandle *handle) {
    /**
     * include receiver
     */
    unsigned int paramSize = handle->getParamTypes().size() + 1;
    const unsigned int recvIndex = this->stackTopIndex - handle->getParamTypes().size();

    // call method
    this->saveAndSetStackState(paramSize, paramSize, paramSize);
    this->pushCallFrame(startPos);

    bool status;
    try {
        // check method handle type
        if(!handle->isInterfaceMethod()) {  // call virtual method
            status = this->localStack[recvIndex]->
                    getType()->getMethodRef(handle->getMethodIndex())->invoke(*this);
        } else {    // call proxy method
            status = typeAs<ProxyObject>(this->localStack[recvIndex])->
                    invokeMethod(*this, methodName, handle);
        }
    } catch(const NativeMethodError &e) {
        status = false;
    }

    this->popCallFrame();

    // restore stack state
    DSValue returnValue;
    if(status && !handle->getReturnType()->isVoidType()) {
        returnValue = this->pop();
    }

    this->restoreStackState();

    if(status && !handle->getReturnType()->isVoidType()) {
        this->push(std::move(returnValue));
    }
    return status ? EvalStatus::SUCCESS : EvalStatus::THROW;
}

void RuntimeContext::newDSObject(DSType *type) {
    if(type->isBuiltinType()) {
        this->dummy->setType(type);
        this->push(this->dummy);
    } else {
        fatal("currently, DSObject allocation not supported\n");
    }
}

/**
 * stack state in constructor call     stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             |    new offset    |
 */
EvalStatus RuntimeContext::callConstructor(unsigned int startPos, unsigned int paramSize) {
    const unsigned int recvIndex = this->stackTopIndex - paramSize;

    // call constructor
    this->saveAndSetStackState(paramSize, paramSize + 1, paramSize + 1);
    this->pushCallFrame(startPos);
    bool status =
            this->localStack[recvIndex]->getType()->getConstructor()->invoke(*this);
    this->popCallFrame();

    // restore stack state
    this->restoreStackState();

    if(status) {
        return EvalStatus::SUCCESS;
    } else {
        return EvalStatus::THROW;
    }
}

EvalStatus RuntimeContext::toString(unsigned int startPos) {
    static const std::string methodName(OP_STR);

    if(this->handle_STR == nullptr) {
        this->handle_STR = this->pool.getAnyType().lookupMethodHandle(this->pool, methodName);
    }
    return this->callMethod(startPos, methodName, this->handle_STR);
}

void RuntimeContext::reportError() {
    std::cerr << "[runtime error]" << std::endl;
    if(this->pool.getErrorType().isSameOrBaseTypeOf(*this->thrownObject->getType())) {
        static const std::string methodName("backtrace");

        if(this->handle_bt == nullptr) {
            this->handle_bt = this->pool.getErrorType().lookupMethodHandle(this->pool, methodName);
        }
        this->loadThrownObject();
        this->callMethod(0, methodName, this->handle_bt);
    } else {
        this->loadThrownObject();
        if(this->toString(0) != EvalStatus::SUCCESS) {
            std::cerr << "cannot obtain string representation" << std::endl;
        } else {
            std::cerr << typeAs<String_Object>(this->pop())->getValue() << std::endl;
        }
    }
}

void RuntimeContext::fillInStackTrace(std::vector<StackTraceElement> &stackTrace) {
    static const unsigned long lowOrderMask = ~((1L << 32) - 1);
    static const unsigned long highOrderMask = ~(((1L << 32) - 1) << 32);

    for(auto iter = this->callStack.rbegin(); iter != this->callStack.rend(); ++iter) {
        unsigned long frame = *iter;
        unsigned long funcCtxIndex = (frame & lowOrderMask) >> 32;
        auto *node = this->callableContextStack[funcCtxIndex];
        const char *sourceName = node->getSourceName();
        unsigned long startPos = frame & highOrderMask;

        std::string callerName;
        if(dynamic_cast<FunctionNode *>(node) != nullptr) {
            callerName += "function ";
            callerName += static_cast<FunctionNode *>(node)->getFuncName();
        } else if(dynamic_cast<UserDefinedCmdNode *>(node) != nullptr) {
            callerName += "user-defined-command ";
            callerName += static_cast<UserDefinedCmdNode *>(node)->getCommandName();
        } else {
            callerName += "<toplevel>";
        }


        stackTrace.push_back(StackTraceElement(
                sourceName, node->getSourceInfoPtr()->getLineNum(startPos), std::move(callerName)));
    }
}

void RuntimeContext::printStackTop(DSType *stackTopType) {
    if(!stackTopType->isVoidType()) {
        if(this->toString(0) != EvalStatus::SUCCESS) {
            std::cerr << "cannot obtain string representation" << std::endl;
        } else {
            std::cout << "(" << this->pool.getTypeName(*stackTopType) << ") "
            << typeAs<String_Object>(this->pop())->getValue() << std::endl;
        }
    }
}

bool RuntimeContext::checkCast(unsigned int startPos, DSType *targetType) {
    if(!this->peek()->introspect(*this, targetType)) {
        DSType *stackTopType = this->pop()->getType();
        std::string str("cannot cast ");
        str += this->pool.getTypeName(*stackTopType);
        str += " to ";
        str += this->pool.getTypeName(*targetType);
        this->pushCallFrame(startPos);
        this->throwError(this->pool.getTypeCastErrorType(), std::move(str));
        this->popCallFrame();
        return false;
    }
    return true;
}

void RuntimeContext::instanceOf(DSType *targetType) {
    if(this->pop()->introspect(*this, targetType)) {
        this->push(this->trueObj);
    } else {
        this->push(this->falseObj);
    }
}

EvalStatus RuntimeContext::checkAssertion(unsigned int startPos) {
    if(!typeAs<Boolean_Object>(this->pop())->getValue()) {
        this->pushCallFrame(startPos);
        this->throwError(this->pool.getAssertFail(), "");
        this->popCallFrame();
        throw InternalError();
    }
    return EvalStatus::SUCCESS;
}

EvalStatus RuntimeContext::importEnv(unsigned int startPos, const std::string &envName,
                                     unsigned int index, bool isGlobal, bool hasDefault) {
    const char *env = getenv(envName.c_str());
    if(hasDefault) {
        DSValue value = this->pop();
        if(env == nullptr) {
            setenv(envName.c_str(), typeAs<String_Object>(value)->getValue(), 1);
        }
    }

    env = getenv(envName.c_str());
    if(env == nullptr) {
        std::string str("undefined environmental variable: ");
        str += envName;
        this->pushCallFrame(startPos);
        this->throwSystemError(EINVAL, std::move(str));
        this->popCallFrame();
        return EvalStatus::THROW;
    }

    DSValue name = DSValue::create<String_Object>(this->pool.getStringType(), envName);

    if(isGlobal) {
        this->setGlobal(index, std::move(name));
    } else {
        this->setLocal(index, std::move(name));
    }

    return EvalStatus::SUCCESS;
}

void RuntimeContext::exportEnv(const std::string &envName, unsigned int index, bool isGlobal) {
    // create string obejct for representing env name
    this->push(DSValue::create<String_Object>(this->getPool().getStringType(), envName));

    // store env name.
    if(isGlobal) {
        this->storeGlobal(index);
    } else {
        this->storeLocal(index);
    }

    // update env
    this->updateEnv(index, isGlobal);
}

void RuntimeContext::updateEnv(unsigned int index, bool isGlobal) {
    // pop stack top and get env value
    DSValue value = this->pop();

    // load variable (representing env name)
    if(isGlobal) {
        this->loadGlobal(index);
    } else {
        this->loadLocal(index);
    }

    DSValue name = this->pop();
    setenv(typeAs<String_Object>(name)->getValue(),
           typeAs<String_Object>(value)->getValue(), 1);//FIXME: check return value and throw
}

void RuntimeContext::loadEnv(unsigned int index, bool isGlobal) {
    // load variable (representing env name)
    if(isGlobal) {
        this->loadGlobal(index);
    } else {
        this->loadLocal(index);
    }

    DSValue name = this->pop();
    const char *value = getenv(typeAs<String_Object>(name)->getValue());
    assert(value != nullptr);
    this->push(DSValue::create<String_Object>(this->getPool().getStringType(), value));
}

void RuntimeContext::resetState() {
    this->callableContextStack.clear();
    this->callStack.clear();
    this->localVarOffset = 0;
    this->thrownObject.reset();
}

bool RuntimeContext::changeWorkingDir(const char *dest, const bool useLogical) {
    if(dest == nullptr) {
        return true;
    }

    const bool tryChdir = strlen(dest) != 0;
    std::string actualDest;
    if(tryChdir) {
        actualDest = expandDots(logicalWorkingDir.c_str(), dest);
        if(useLogical) {
            dest = actualDest.c_str();
        }
        if(chdir(dest) != 0) {
            return false;
        }
    }

    // update OLDPWD
    const char *oldpwd = getenv(ENV_PWD);
    assert(oldpwd != nullptr);
    setenv(ENV_OLDPWD, oldpwd, 1);

    // update PWD
    if(tryChdir) {
        if(useLogical) {
            setenv(ENV_PWD, actualDest.c_str(), 1);
        } else {
            size_t size = PATH_MAX;
            char buf[size];
            const char *cwd = getcwd(buf, size);
            if(cwd != nullptr) {
                setenv(ENV_PWD, cwd, 1);
            }
        }

        logicalWorkingDir = std::move(actualDest);
    }
    return true;
}

const char *RuntimeContext::getIFS() {
    if(this->IFS_index == 0) {
        auto handle = this->symbolTable.lookupHandle("IFS");
        this->IFS_index = handle->getFieldIndex();
        assert(handle != nullptr);
    }
    return typeAs<String_Object>(this->getGlobal(this->IFS_index))->getValue();
}

void RuntimeContext::updateExitStatus(unsigned int status) {
    unsigned int index = this->getBuiltinVarIndex(BuiltinVarOffset::EXIT_STATUS);
    this->setGlobal(index, DSValue::create<Int_Object>(this->pool.getInt32Type(), status));
}

void RuntimeContext::exitShell(unsigned int status) {
    std::string str("terminated by exit ");
    str += std::to_string(status);
    this->updateExitStatus(status);
    this->throwError(this->pool.getShellExit(), std::move(str));
    throw InternalError();
}

void RuntimeContext::addUserDefinedCommand(UserDefinedCmdNode *node) {
    if(!this->udcMap.insert(std::make_pair(node->getCommandName().c_str(), node)).second) {
        fatal("undefined defined command: %s\n", node->getCommandName().c_str());
    }
}

UserDefinedCmdNode *RuntimeContext::lookupUserDefinedCommand(const char *commandName) {
    auto iter = this->udcMap.find(commandName);
    if(iter != this->udcMap.end()) {
        return iter->second;
    }
    return nullptr;
}

int RuntimeContext::execUserDefinedCommand(UserDefinedCmdNode *node, DSValue *argv) {
    // copy arguments
    this->initScriptArg();
    for(unsigned int index = 1; argv[index]; index++) {
        typeAs<Array_Object>(this->getScriptArgs())->append(std::move(argv[index]));
    }
    this->finalizeScritArg();

    // clear procInvoker
    this->activePipeline().clear();

    this->pushFuncContext(node);
    this->reserveLocalVar(this->getLocalVarOffset() + node->getMaxVarNum());

    EvalStatus s;
    try {
        s = node->getBlockNode()->eval(*this);
    } catch(const InternalError &e) {
        s = EvalStatus::THROW;
    }

    this->popFuncContext();

    // get exit status
    switch(s) {
    case EvalStatus::SUCCESS:
        return typeAs<Int_Object>(this->getExitStatus())->getValue();
    case EvalStatus::RETURN:
        return typeAs<Int_Object>(this->pop())->getValue();
    case EvalStatus::THROW: {
        DSType &thrownType = *this->getThrownObject()->getType();
        if(this->pool.getInternalStatus().isSameOrBaseTypeOf(thrownType)) {
            if(thrownType == this->pool.getShellExit()) {
                return typeAs<Int_Object>(this->getExitStatus())->getValue();
            }
            if(thrownType == this->pool.getAssertFail()) {
                this->loadThrownObject();
                typeAs<Error_Object>(this->pop())->printStackTrace(*this);
                return 1;
            }
        }
        this->reportError();
        return 1;
    }
    default:
        fatal("broken user defined command\n");
    }
    return 0;
}

void RuntimeContext::pushNewPipeline() {
    if(this->pipelineEvaluator.get()->getRefcount() == 1) { // reuse cached evaluator object
        typeAs<PipelineEvaluator>(this->pipelineEvaluator)->clear();
        this->push(this->pipelineEvaluator);
    } else {
        this->push(DSValue::create<PipelineEvaluator>());
    }
}

PipelineEvaluator &RuntimeContext::activePipeline() {
    return *typeAs<PipelineEvaluator>(this->peek());
}

EvalStatus RuntimeContext::callPipedCommand(unsigned int startPos) {
    this->pushCallFrame(startPos);
    EvalStatus status = this->activePipeline().evalPipeline(*this);
    this->popCallFrame();

    if(status != EvalStatus::THROW) {
        this->popNoReturn();
    }
    return status;
}

static void format2digit(int num, std::string &out) {
    if(num < 10) {
        out += "0";
    }
    out += std::to_string(num);
}

const char *safeBasename(const char *str) {
    const char *ptr = strrchr(str, '/');
    return ptr == nullptr ? str : ptr + 1;
}

void RuntimeContext::interpretPromptString(const char *ps, std::string &output) {
    output.clear();

    struct tm *local = misc::getLocalTime();

    static const char *wdays[] = {
            "Sun", "Mon", "Tue", "Wed", "Thurs", "Fri", "Sat"
    };

    const unsigned int hostNameSize = 128;    // in linux environment, HOST_NAME_MAX is 64
    static char hostName[hostNameSize];
    if(gethostname(hostName, hostNameSize) !=  0) {
        hostName[0] = '\0';
    }

    for(unsigned int i = 0; ps[i] != '\0'; i++) {
        char ch = ps[i];
        if(ch == '\\' && ps[i + 1] != '\0') {
            switch(ps[++i]) {
            case 'a':
                ch = '\a';
                break;
            case 'd': {
                output += wdays[local->tm_wday];
                output += " ";
                output += std::to_string(local->tm_mon + 1);
                output += " ";
                output += std::to_string(local->tm_mday);
                continue;
            }
            case 'e':
                ch = '\033';
                break;
            case 'h': {
                for(unsigned int j = 0; hostName[j] != '\0' && hostName[j] != '.'; j++) {
                    output += hostName[j];
                }
                continue;
            }
            case 'H':
                output += hostName;
                continue;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 's':
                output += safeBasename(typeAs<String_Object>(this->getScriptName())->getValue());
                continue;
            case 't': {
                format2digit(local->tm_hour, output);
                output += ":";
                format2digit(local->tm_min, output);
                output += ":";
                format2digit(local->tm_sec, output);
                continue;
            }
            case 'T': {
                format2digit(local->tm_hour < 12 ? local->tm_hour : local->tm_hour - 12, output);
                output += ":";
                format2digit(local->tm_min, output);
                output += ":";
                format2digit(local->tm_sec, output);
                continue;
            }
            case '@': {
                format2digit(local->tm_hour < 12 ? local->tm_hour : local->tm_hour - 12, output);
                output += ":";
                format2digit(local->tm_min, output);
                output += " ";
                output += local->tm_hour < 12 ? "AM" : "PM";
                continue;
            }
            case 'u': {
                struct passwd *pw = getpwuid(geteuid());
                if(pw != nullptr) {
                    output += pw->pw_name;
                }
                continue;
            }
            case 'v': {
#define XSTR(v) #v
#define STR(v) XSTR(v)
                output += STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION);
                continue;
#undef XSTR
#undef STR
            }
            case 'V': {
                const unsigned int index = this->getBuiltinVarIndex(BuiltinVarOffset::VERSION);
                const char *str = typeAs<String_Object>(this->getGlobal(index))->getValue();
                output += str;
                continue;
            }
            case 'w': {
                const char *c = getenv(ENV_PWD);
                if(c != nullptr) {
                    const char *home = getenv(ENV_HOME);
                    if(home != nullptr && strstr(c, home) == c) {
                        output += '~';
                        c += strlen(home);
                    }
                    output += c;
                }
                continue;
            }
            case 'W':  {
                const char *cwd = getenv(ENV_PWD);
                if(cwd == nullptr) {
                    continue;
                }
                if(strcmp(cwd, "/") == 0) {
                    output += cwd;
                } else {
                    const char *home = getenv(ENV_HOME);
                    char *realHOME = home != nullptr ? realpath(home, nullptr) : nullptr;
                    char *realPWD = realpath(cwd, nullptr);

                    if(realHOME != nullptr && strcmp(realHOME, realPWD) == 0) {
                        output += "~";
                    } else {
                        output += safeBasename(cwd);
                    }

                    free(realHOME);
                    free(realPWD);
                }
                continue;
            }
            case '$':
                ch = (getuid() == 0 ? '#' : '$');
                break;
            case '\\':
                ch = '\\';
                break;
            case '[':
            case ']':
                continue;
            case '0': {
                int v = 0;
                for(unsigned int c = 0; c < 3; c++) {
                    if(isOctal(ps[i + 1])) {
                        v *= 8;
                        v += ps[++i] - '0';
                    } else {
                        break;
                    }
                }
                ch = (char) v;
                break;
            }
            case 'x': {
                if(isHex(ps[i + 1])) {
                    int v = toHex(ps[++i]);
                    if(isHex(ps[i + 1])) {
                        v *= 16;
                        v += toHex(ps[++i]);
                    }
                    ch = (char) v;
                    break;
                }
                i--;
                break;
            }
            default:
                i--;
                break;
            }
        }
        output += ch;
    }
}

pid_t RuntimeContext::xwaitpid(pid_t pid, int &status, int options) {
    pid_t ret = waitpid(pid, &status, options);
    if(WIFSIGNALED(status)) {
        fputc('\n', stdout);
    }
    return ret;
}

// for input completion

static void append(CStrBuffer &buf, const char *str) {
    // find inserting position
    for(auto iter = buf.begin(); iter != buf.end(); ++iter) {
        int r = strcmp(str, *iter);
        if(r <= 0) {
            if(r < 0) {
                buf.insert(iter, strdup(str));
            }
            return;
        }
    }

    // not found, append to last
    buf += strdup(str);
}

static void append(CStrBuffer &buf, const std::string &str) {
    append(buf, str.c_str());
}

static std::vector<std::string> computePathList(const char *pathVal) {
    std::vector<std::string> result;
    std::string buf;
    assert(pathVal != nullptr);

    for(unsigned int i = 0; pathVal[i] != '\0'; i++) {
        char ch = pathVal[i];
        if(ch == ':') {
            result.push_back(std::move(buf));
            buf = "";
        } else {
            buf += ch;
        }
    }
    if(!buf.empty()) {
        result.push_back(std::move(buf));
    }

    // expand tilde
    for(auto &s : result) {
        if(s[0] == '~') {
            std::string expanded = expandTilde(s.c_str());
            std::swap(s, expanded);
        }
    }

    return result;
}

static bool startsWith(const char *s1, const char *s2) {
    return s1 != nullptr && s2 != nullptr && strstr(s1, s2) == s1;
}

/**
 * append candidates to results.
 * token may be empty string.
 */
static void completeCommandName(RuntimeContext &ctx, const std::string &token, CStrBuffer &results) {
    // search user defined command
    for(auto &e : ctx.getUdcMap()) {
        const char *name = e.first;
        if(startsWith(name, token.c_str())) {
            append(results, name);
        }
    }

    // search builtin command
    const unsigned int bsize = getBuiltinCommandSize();
    for(unsigned int i = 0; i < bsize; i++) {
        const char *name = getBultinCommandName(i);
        if(startsWith(name, token.c_str())) {
            append(results, name);
        }
    }


    // search external command
    const char *path = getenv(ENV_PATH);
    if(path == nullptr) {
        return;
    }

    auto pathList(computePathList(path));
    for(const auto &p : pathList) {
        DIR *dir = opendir(p.c_str());
        if(dir == nullptr) {
            continue;
        }

        dirent *entry;
        while(true) {
            entry = readdir(dir);
            if(entry == nullptr) {
                break;
            }
            const char *name = entry->d_name;
            if(startsWith(name, token.c_str())) {
                std::string fullpath(p);
                fullpath += '/';
                fullpath += name;
                if(S_ISREG(getStMode(fullpath.c_str())) && access(fullpath.c_str(), X_OK) == 0) {
                    append(results, name);
                }
            }
        }
        closedir(dir);
    }
}

static void completeFileName(const std::string &token, CStrBuffer &results, bool onlyExec = true) {
    const auto s = token.find_last_of('/');

    // complete tilde
    if(token[0] == '~' && s == std::string::npos) {
        for(struct passwd *entry = getpwent(); entry != nullptr; entry = getpwent()) {
            if(startsWith(entry->pw_name, token.c_str() + 1)) {
                std::string name("~");
                name += entry->pw_name;
                name += '/';
                append(results, name);
            }
        }
        endpwent();
        return;
    }

    // complete file name

    /**
     * resolve directory path
     */
    std::string targetDir;
    if(s == 0) {
        targetDir = "/";
    } else if(s != std::string::npos) {
        targetDir = token.substr(0, s);
        if(targetDir[0] == '~') {
            targetDir = expandTilde(targetDir.c_str());
        }
    } else {
        targetDir = ".";
    }

    /**
     * resolve name
     */
    std::string name;
    if(s != std::string::npos) {
        name = token.substr(s + 1);
    } else {
        name = token;
    }

    DIR *dir = opendir(targetDir.c_str());
    if(dir == nullptr) {
        return;
    }

    dirent *entry;
    while(true) {
        entry = readdir(dir);
        if(entry == nullptr) {
            break;
        }

        if(startsWith(entry->d_name, name.c_str())) {
            if(name.empty() && (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)) {
                continue;
            }

            std::string fullpath(targetDir);
            fullpath += '/';
            fullpath += entry->d_name;

            if(onlyExec && S_ISREG(getStMode(fullpath.c_str())) && access(fullpath.c_str(), X_OK) != 0) {
                continue;
            }

            std::string name = entry->d_name;
            if(S_ISDIR(getStMode(fullpath.c_str()))) {
                name += '/';
            }
            append(results, name);
        }
    }
    closedir(dir);
}

static void completeGlobalVarName(RuntimeContext &ctx, const std::string &token, CStrBuffer &results) {
    const auto &symbolTable = ctx.getSymbolTable();

    for(auto iter = symbolTable.cbeginGlobal(); iter != symbolTable.cendGlobal(); ++iter) {
        if(!token.empty() && startsWith(iter->first.c_str(), token.c_str() + 1)) {
            append(results, iter->first);
        }
    }
}

static void completeExpectedToken(const std::string &token, CStrBuffer &results) {
    if(!token.empty()) {
        append(results, token);
    }
}

enum class CompletorKind {
    NONE,
    CMD,    // command name without '/'
    QCMD,   // command name with '/'
    FILE,
    VAR,    // complete global variable name
    EXPECT,
};

static bool isFileName(const std::string &str) {
    return !str.empty() && (str[0] == '~' || strchr(str.c_str(), '/') != nullptr);
}

static bool isRedirOp(TokenKind kind) {
    switch(kind) {
    case REDIR_IN_2_FILE:
    case REDIR_OUT_2_FILE:
    case REDIR_OUT_2_FILE_APPEND:
    case REDIR_ERR_2_FILE:
    case REDIR_ERR_2_FILE_APPEND:
    case REDIR_MERGE_ERR_2_OUT_2_FILE:
    case REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND:
    case REDIR_MERGE_ERR_2_OUT:
    case REDIR_MERGE_OUT_2_ERR:
        return true;
    default:
        return false;
    }
}

static bool isNewline(const Lexer &lexer, const std::pair<TokenKind, Token> &pair) {
    return pair.first == LINE_END && lexer.toTokenText(pair.second) != ";";
}

static CompletorKind selectWithCmd(const Lexer &lexer, const std::vector<std::pair<TokenKind, Token>> &tokenPairs,
                                   unsigned int cursor, unsigned int lastIndex,
                                   std::string &tokenStr, bool exactly = false) {
    TokenKind kind = tokenPairs[lastIndex].first;
    Token token = tokenPairs[lastIndex].second;

    if(exactly && isNewline(lexer, tokenPairs[lastIndex]) && lastIndex > 0) {
        lastIndex--;
        kind = tokenPairs[lastIndex].first;
        token = tokenPairs[lastIndex].second;
    }

    switch(kind) {
    case COMMAND:
        if(token.pos + token.size == cursor) {
            tokenStr = lexer.toTokenText(token);
            return isFileName(tokenStr) ? CompletorKind::QCMD : CompletorKind::CMD;
        }
        return CompletorKind::FILE;
    case CMD_ARG_PART:
        if(token.pos + token.size == cursor && lastIndex > 0) {
            auto prevKind = tokenPairs[lastIndex - 1].first;
            auto prevToken = tokenPairs[lastIndex - 1].second;

            /**
             * if previous token is redir op,
             * or if spaces exist between current and previous
             */
            if(isRedirOp(prevKind) || prevToken.pos + prevToken.size < token.pos) {
                tokenStr = lexer.toTokenText(token);
                return CompletorKind::FILE;
            }
            return CompletorKind::NONE;
        }
        return CompletorKind::FILE;
    default:
        if(!exactly && token.pos + token.size < cursor) {
            return CompletorKind::FILE;
        }
        return CompletorKind::NONE;
    }
}

static bool findKind(const std::vector<TokenKind> &values, TokenKind kind) {
    for(auto &e : values) {
        if(e == kind) {
            return true;
        }
    }
    return false;
}

static CompletorKind selectCompletor(const std::string &line, std::string &tokenStr) {
    CompletorKind kind = CompletorKind::NONE;

    const unsigned int cursor = line.size() - 1; //   ignore last newline

    // parse input line
    Lexer lexer("<line>", line.c_str());
    TokenTracker tracker;
    try {
        {
            Parser parser;
            parser.setTracker(&tracker);
            RootNode rootNode;
            parser.parse(lexer, rootNode);
        }

        const auto &tokenPairs = tracker.getTokenPairs();
        const unsigned int tokenSize = tokenPairs.size();

        assert(tokenSize > 0);

        unsigned int lastIndex = tokenSize - 1;

        if(lastIndex == 0) {
            goto END;
        }

        lastIndex--; // skip EOS

        switch(tokenPairs[lastIndex].first) {
        case RBC:
            kind = CompletorKind::CMD;
            goto END;
        case APPLIED_NAME:
        case SPECIAL_NAME: {
            Token token = tokenPairs[lastIndex].second;
            if(token.pos + token.size == cursor) {
                tokenStr = lexer.toTokenText(token);
                kind = CompletorKind::VAR;
                goto END;
            }
            break;
        }
        case LINE_END: {
            if(!isNewline(lexer, tokenPairs[lastIndex])) {  // terminate with ';'
                kind = CompletorKind::CMD;
                goto END;
            }

            lastIndex--; // skip LINE_END
            kind = selectWithCmd(lexer, tokenPairs, cursor, lastIndex, tokenStr);
            goto END;
        }
        default:
            break;
        }
    } catch(const ParseError &e) {
        LOG_L(DUMP_CONSOLE, [&](std::ostream &stream) {
            stream << "error kind: " << e.getErrorKind() << std::endl;
            stream << "kind: " << toString(e.getTokenKind())
            << ", token: " << e.getErrorToken()
            << ", text: " << lexer.toTokenText(e.getErrorToken()) << std::endl;
        });

        Token token = e.getErrorToken();
        auto &tokenPairs = tracker.getTokenPairs();
        if(token.pos + token.size < cursor) {
            goto END;
        }

        switch(e.getTokenKind()) {
        case EOS:
        case LINE_END: {
            if(lexer.toTokenText(token) == ";") {
                goto END;
            }

            if(strcmp(e.getErrorKind(), "NoViableAlter") == 0) {
                if(!tokenPairs.empty()) {
                    kind = selectWithCmd(lexer, tokenPairs, cursor, tokenPairs.size() - 1, tokenStr, true);
                    if(kind != CompletorKind::NONE) {
                        goto END;
                    }

                    if(!isNewline(lexer, tokenPairs.back()) && findKind(e.getExpectedTokens(), COMMAND)) {
                        kind = CompletorKind::CMD;
                        goto END;
                    }
                }

                if(findKind(e.getExpectedTokens(), CMD_ARG_PART)) {
                    kind = CompletorKind::FILE;
                    goto END;
                }
            } else if(strcmp(e.getErrorKind(), "TokenMismatched") == 0) {
                assert(e.getExpectedTokens().size() > 0);
                TokenKind expected = e.getExpectedTokens().back();
                LOG(DUMP_CONSOLE, "expected: " << toString(expected));

                if(!tokenPairs.empty()) {
                    kind = kind = selectWithCmd(lexer, tokenPairs, cursor, tokenPairs.size() - 1, tokenStr, true);
                    if(kind != CompletorKind::NONE) {
                        goto END;
                    }
                }

                std::string expectedStr = toString(expected);
                if(expectedStr.size() < 2 || (expectedStr.front() != '<' && expectedStr.back() != '>')) {
                    tokenStr = std::move(expectedStr);
                    kind = CompletorKind::EXPECT;
                    goto END;
                }
                if(expected == COMMAND) {
                    kind = CompletorKind::CMD;
                    goto END;
                }
            }
            break;
        }
        case INVALID: {
            std::string str = lexer.toTokenText(token);
            if(str == "$" && token.pos + token.size == cursor &&
                    findKind(e.getExpectedTokens(), APPLIED_NAME) &&
                    findKind(e.getExpectedTokens(), SPECIAL_NAME)) {
                tokenStr = std::move(str);
                kind = CompletorKind::VAR;
                goto END;
            }
            break;
        }
        default:
            break;
        }
    }

    END:
    LOG_L(DUMP_CONSOLE, [&](std::ostream &stream) {
        stream << "token size: " << tracker.getTokenPairs().size() << std::endl;
        for(auto &t : tracker.getTokenPairs()) {
            stream << "kind: " << toString(t.first)
            << ", token: " << t.second
            << ", text: " << lexer.toTokenText(t.second) << std::endl;
        }

        switch(kind) {
        case CompletorKind::NONE:
            stream << "ckind: NONE" << std::endl;
            break;
        case CompletorKind::CMD:
            stream << "ckind: CMD" << std::endl;
            break;
        case CompletorKind::QCMD:
            stream << "ckind: QCMD" << std::endl;
            break;
        case CompletorKind::FILE:
            stream << "ckind: FILE" << std::endl;
            break;
        case CompletorKind::VAR:
            stream << "ckind: VAR" << std::endl;
            break;
        case CompletorKind::EXPECT:
            stream << "ckind: EXPECT" << std::endl;
            break;
        }
    });

    return kind;
}

CStrBuffer RuntimeContext::completeLine(const std::string &line) {
    assert(!line.empty() && line.back() == '\n');

    CStrBuffer sbuf;
    std::string tokenStr;
    switch(selectCompletor(line, tokenStr)) {
    case CompletorKind::NONE:
        break;  // do nothing
    case CompletorKind::CMD:
        completeCommandName(*this, tokenStr, sbuf);
        break;
    case CompletorKind::QCMD:
        completeFileName(tokenStr, sbuf);
        break;
    case CompletorKind::FILE:
        completeFileName(tokenStr, sbuf, false);
        break;
    case CompletorKind::VAR:
        completeGlobalVarName(*this, tokenStr, sbuf);
        break;
    case CompletorKind::EXPECT:
        completeExpectedToken(tokenStr, sbuf);
        break;
    }
    return sbuf;
}



// implementation of some system util

std::string expandTilde(const char *path) {
    std::string expanded;
    for(; *path != '/' && *path != '\0'; path++) {
        expanded += *path;
    }

    // expand tilde
    if(expanded.size() == 1) {
        struct passwd *pw = getpwuid(getuid());
        if(pw != nullptr) {
            expanded = pw->pw_dir;
        }
    } else if(expanded == "~+") {
        expanded = getenv(ENV_PWD);
    } else if(expanded == "~-") {
        expanded = getenv(ENV_OLDPWD);
    } else {
        struct passwd *pw = getpwnam(expanded.c_str() + 1);
        if(pw != nullptr) {
            expanded = pw->pw_dir;
        }
    }

    // append rest
    if(*path != '\0') {
        expanded += path;
    }
    return expanded;
}

pid_t xfork() {
    pid_t pid = fork();
    if(pid == 0) {  // child process
        struct sigaction act;
        act.sa_handler = SIG_DFL;
        act.sa_flags = 0;
        sigemptyset(&act.sa_mask);

        /**
         * reset signal behavior
         */
        sigaction(SIGINT, &act, NULL);
        sigaction(SIGQUIT, &act, NULL);
        sigaction(SIGTSTP, &act, NULL);
    }
    return pid;
}

} // namespace core
} // namespace ydsh
