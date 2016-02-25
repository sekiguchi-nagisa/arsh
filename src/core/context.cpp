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
        delete pair.first;
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
    const char *pathPrefix = getenv("PATH");
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
                    delete this->map.begin()->first;
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
        dummy(new DummyObject()),
        globalVarTable(new DSValue[DEFAULT_TABLE_SIZE]),
        tableSize(DEFAULT_TABLE_SIZE), thrownObject(),
        localStack(new DSValue[DEFAULT_LOCAL_SIZE]),
        localStackSize(DEFAULT_LOCAL_SIZE), stackTopIndex(0),
        localVarOffset(0), offsetStack(), toplevelPrinting(false), assertion(true),
        handle_STR(nullptr), handle_bt(nullptr),
        OLDPWD_index(0), PWD_index(0), IFS_index(0),
        callableContextStack(), callStack(), pipelineEvaluator(), udcMap(), pathCache() {
}

RuntimeContext::~RuntimeContext() {
    delete[] this->globalVarTable;
    delete[] this->localStack;

    for(auto &pair : this->udcMap) {
        delete pair.second;
    }
}

std::string RuntimeContext::getConfigRootDir() {
#ifdef X_CONFIG_DIR
    return std::string(X_CONFIG_DIR);
#else
    std::string path(getenv("HOME"));
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
    if(this->tableSize < size) {
        unsigned int newSize = this->tableSize;
        do {
            newSize *= 2;
        } while(newSize < size);
        DSValue *newTable = new DSValue[newSize];
        for(unsigned int i = 0; i < this->tableSize; i++) {
            newTable[i] = std::move(this->globalVarTable[i]);
        }
        delete[] this->globalVarTable;
        this->globalVarTable = newTable;
        this->tableSize = newSize;
    }
}

void RuntimeContext::reserveLocalVar(unsigned int size) {
    if(size > this->localStackSize) {
        this->expandLocalStack(size);
    }
    this->stackTopIndex = size;
}

void RuntimeContext::throwError(DSType &errorType, const char *message) {
    this->throwError(errorType, std::string(message));
}

void RuntimeContext::throwError(DSType &errorType, std::string &&message) {
    this->thrownObject = Error_Object::newError(*this, errorType, DSValue::create<String_Object>(
            this->pool.getStringType(), std::move(message)));
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
        newSize *= 2;
    } while(newSize < needSize);
    auto newTable = new DSValue[newSize];
    for(unsigned int i = 0; i < this->localStackSize; i++) {
        newTable[i] = std::move(this->localStack[i]);
    }
    delete[] this->localStack;
    this->localStack = newTable;
    this->localStackSize = newSize;
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
    unsigned int savedStackTopIndex = this->stackTopIndex - paramSize - 1;

    // call function
    this->saveAndSetOffset(savedStackTopIndex + 2);
    this->pushCallFrame(startPos);
    bool status = typeAs<FuncObject>(this->localStack[savedStackTopIndex + 1])->invoke(*this);
    this->popCallFrame();

    // restore stack state
    DSValue returnValue;
    if(!returnTypeIsVoid) {
        returnValue = std::move(this->localStack[this->stackTopIndex]);
    }

    this->restoreOffset();
    for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
        this->popNoReturn();
    }

    if(returnValue) {
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

    unsigned int savedStackTopIndex = this->stackTopIndex - paramSize;

    // call method
    this->saveAndSetOffset(savedStackTopIndex + 1);
    this->pushCallFrame(startPos);

    bool status;
    try {
        // check method handle type
        if(!handle->isInterfaceMethod()) {  // call virtual method
            status = this->localStack[savedStackTopIndex + 1]->
                    getType()->getMethodRef(handle->getMethodIndex())->invoke(*this);
        } else {    // call proxy method
            status = typeAs<ProxyObject>(this->localStack[savedStackTopIndex + 1])->
                    invokeMethod(*this, methodName, handle);
        }
    } catch(const NativeMethodError &e) {
        status = false;
    }

    this->popCallFrame();

    // restore stack state
    DSValue returnValue;
    if(!handle->getReturnType()->isVoidType()) {
        returnValue = std::move(this->localStack[this->stackTopIndex]);
    }

    this->restoreOffset();
    for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
        this->popNoReturn();
    }

    if(returnValue) {
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
    unsigned int savedStackTopIndex = this->stackTopIndex - paramSize;

    // call constructor
    this->saveAndSetOffset(savedStackTopIndex);
    this->pushCallFrame(startPos);
    bool status =
            this->localStack[savedStackTopIndex]->getType()->getConstructor()->invoke(*this);
    this->popCallFrame();

    // restore stack state
    this->restoreOffset();
    for(unsigned int i = this->stackTopIndex; i > savedStackTopIndex; i--) {
        this->popNoReturn();
    }

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

    DSValue value(new String_Object(this->pool.getStringType(), std::string(env)));

    if(isGlobal) {
        this->setGlobal(index, std::move(value));
    } else {
        this->setLocal(index, std::move(value));
    }
    return EvalStatus::SUCCESS;
}

void RuntimeContext::exportEnv(const std::string &envName, unsigned int index, bool isGlobal) {
    setenv(envName.c_str(),
           typeAs<String_Object>(this->peek())->getValue(), 1);    //FIXME: check return value and throw
    if(isGlobal) {
        this->storeGlobal(index);
    } else {
        this->storeLocal(index);
    }
}

void RuntimeContext::resetState() {
    this->callableContextStack.clear();
    this->callStack.clear();
    this->localVarOffset = 0;
    this->offsetStack.clear();
    this->thrownObject.reset();
}

void RuntimeContext::updateWorkingDir(bool OLDPWD_only) {
    // check handle
    const char *env_OLDPWD = "OLDPWD";
    if(this->OLDPWD_index == 0) {
        auto handle = this->symbolTable.lookupHandle(env_OLDPWD);
        this->OLDPWD_index = handle->getFieldIndex();
        assert(handle != nullptr && handle->isEnv());
    }

    const char *env_PWD = "PWD";
    if(this->PWD_index == 0) {
        auto handle = this->symbolTable.lookupHandle(env_PWD);
        this->PWD_index = handle->getFieldIndex();
        assert(handle != nullptr && handle->isEnv());
    }

    // update OLDPWD
    this->setGlobal(this->OLDPWD_index, this->getGlobal(this->PWD_index));
    const char *oldpwd =
            typeAs<String_Object>(this->getGlobal(this->OLDPWD_index))->getValue();
    setenv(env_OLDPWD, oldpwd, 1);

    // update PWD
    if(!OLDPWD_only) {
        size_t size = PATH_MAX;
        char buf[size];
        char *cwd = getcwd(buf, size);
        if(cwd != nullptr && strcmp(cwd, oldpwd) != 0) {
            setenv(env_PWD, cwd, 1);
            this->setGlobal(this->PWD_index,
                            DSValue::create<String_Object>(this->pool.getStringType(), std::string(cwd)));
        }
    }
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

EvalStatus RuntimeContext::callPipedCommand(unsigned int startPos) {
    this->pushCallFrame(startPos);
    EvalStatus status = this->activePipeline().evalPipeline(*this);
    this->popCallFrame();

    // pop stack top
    const unsigned int oldIndex = this->activePipeline().getStackTopIndex();
    for(unsigned int i = this->getStackTopIndex(); i > oldIndex; i--) {
        this->popNoReturn();
    }

    this->activePipeline().clear();
    return status;
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

static void format2digit(int num, std::string &out) {
    if(num < 10) {
        out += "0";
    }
    out += std::to_string(num);
}

static std::string basename(const std::string &path) {
    return path.substr(path.find_last_of('/') + 1);
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
                output += basename(typeAs<String_Object>(this->getScriptName())->getValue());
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
                const char *c = getenv("USER");
                output += c != nullptr ? c : "";
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
                const char *c = getenv("PWD");
                output += c != nullptr ? c : "";
                continue;
            }
            case 'W':  {
                const char *cwd = getenv("PWD");
                if(cwd == nullptr) {
                    continue;
                }
                if(strcmp(cwd, "/") == 0) {
                    output += cwd;
                } else {
                    std::string name(basename(getenv("PWD")));
                    if(getenv("HOME") != nullptr && name == basename(getenv("HOME"))) {
                        output += "~";
                    } else {
                        output += name;
                    }
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
static void completeCommandName(RuntimeContext &ctx, const std::string &token,
                                CStrBuffer &results) {
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
    const char *path = getenv("PATH");
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
                if(S_ISREG(getStMode(fullpath.c_str())) &&access(fullpath.c_str(), X_OK) == 0) {
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

static CompletorKind selectCompletor(const std::string &line, std::string &tokenStr) {
    CompletorKind kind = CompletorKind::NONE;

    const unsigned int cursor = line.size() - 1; //   ignore last newline

    Lexer lexer("<line>", line.c_str());
    TokenTracker tracker;
    try {
        Parser parser;
        parser.setTracker(&tracker);
        RootNode rootNode;
        parser.parse(lexer, rootNode);

        const auto &tokenPairs = tracker.getTokenPairs();
        const unsigned int tokenSize = tokenPairs.size();

        if(tokenSize > 2) {
            unsigned int lastIndex = tokenSize - 1;
            lastIndex--;    // skip EOS

            if(tokenPairs[lastIndex].first == RBC) {
                kind = CompletorKind::CMD;
                goto END;
            }

            if(tokenPairs[lastIndex].first == TokenKind::LINE_END) {
                if(lexer.toTokenText(tokenPairs[lastIndex].second) == ";") {    // terminate with ';'
                    kind = CompletorKind::CMD;
                    goto END;
                }

                lastIndex--; // skip LINE_END
                TokenKind k = tokenPairs[lastIndex].first;
                Token token = tokenPairs[lastIndex].second;

                if(k == COMMAND) {
                    if(token.pos + token.size == cursor) {
                        tokenStr = lexer.toTokenText(token);
                        kind = isFileName(tokenStr) ? CompletorKind::QCMD : CompletorKind::CMD;
                        goto END;
                    }
                }

                if(k == CMD_ARG_PART) {
                    if(token.pos + token.size == cursor) {
                        assert(lastIndex > 0);
                        auto prevKind = tokenPairs[lastIndex - 1].first;
                        auto prevToken = tokenPairs[lastIndex - 1].second;

                        /**
                         * if previous token is redir op,
                         * or if spaces exist between current and previous
                         */
                        if(isRedirOp(prevKind) || prevToken.pos + prevToken.size < token.pos) {
                            tokenStr = lexer.toTokenText(token);
                            kind = CompletorKind::FILE;
                            goto END;
                        }
                    }
                }

                if(token.pos + token.size < cursor) {
                    kind = CompletorKind::FILE;
                    goto END;
                }
            }
        }
    } catch(const ParseError &e) {
        LOG(DUMP_CONSOLE, "error kind: " << e.getErrorKind());


        if(strcmp(e.getErrorKind(), "NoViableAlter") == 0) {
            if(strstr(e.getMessage().c_str(), toString(COMMAND)) != nullptr) {
                kind = CompletorKind::CMD;
                goto END;
            }
            if(strstr(e.getMessage().c_str(), toString(CMD_ARG_PART)) != nullptr) {
                kind = CompletorKind::FILE;
                goto END;
            }
        } else if(strcmp(e.getErrorKind(), "TokenMismatched") == 0) {
            std::string t = "expected: ";
            std::string expected = e.getMessage().substr(e.getMessage().find(t) + t.size());
            LOG(DUMP_CONSOLE, "expected: " << expected);
            if(expected.size() < 2 || (expected.front() != '<' && expected.back() != '>')) {
                tokenStr = std::move(expected);
                kind = CompletorKind::EXPECT;
                goto END;
            }
        }
    }

    END:
    LOG_L(DUMP_CONSOLE, [&](std::ostream &stream) {
        stream << "token size: " << tracker.getTokenPairs().size() << std::endl;
        for(auto &t : tracker.getTokenPairs()) {
            stream << "kind: " << toString(t.first) << ", token: "
            << t.second << ", text: " << lexer.toTokenText(t.second) << std::endl;
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
    case CompletorKind::EXPECT:
        completeExpectedToken(tokenStr, sbuf);
        break;
    }

    // sort and deduplicate
    std::sort(sbuf.begin(), sbuf.end(), [](char *x, char *y) {
        return strcmp(x, y) < 0;
    });
    auto iter = std::unique(sbuf.begin(), sbuf.end(), [](char *x, char *y) {
        return strcmp(x, y) == 0;
    });
    sbuf.erase(iter, sbuf.end());

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
        expanded = getenv("PWD");
    } else if(expanded == "~-") {
        expanded = getenv("OLDPWD");
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
