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
#include "../misc/num.h"
#include "../misc/time.h"

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
        handle_OLDPWD(nullptr), handle_PWD(nullptr), handle_IFS(nullptr),
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
    if(this->handle_OLDPWD == nullptr) {
        this->handle_OLDPWD = this->symbolTable.lookupHandle(env_OLDPWD);
        assert(this->handle_OLDPWD != nullptr && this->handle_OLDPWD->isEnv());
    }

    const char *env_PWD = "PWD";
    if(this->handle_PWD == nullptr) {
        this->handle_PWD = this->symbolTable.lookupHandle(env_PWD);
        assert(this->handle_PWD != nullptr && this->handle_PWD->isEnv());
    }

    // update OLDPWD
    this->setGlobal(this->handle_OLDPWD->getFieldIndex(),
                    this->getGlobal(this->handle_PWD->getFieldIndex()));
    const char *oldpwd =
            typeAs<String_Object>(this->getGlobal(this->handle_OLDPWD->getFieldIndex()))->getValue();
    setenv(env_OLDPWD, oldpwd, 1);

    // update PWD
    if(!OLDPWD_only) {
        size_t size = PATH_MAX;
        char buf[size];
        char *cwd = getcwd(buf, size);
        if(cwd != nullptr && strcmp(cwd, oldpwd) != 0) {
            setenv(env_PWD, cwd, 1);
            this->setGlobal(this->handle_PWD->getFieldIndex(),
                            DSValue::create<String_Object>(this->pool.getStringType(), std::string(cwd)));
        }
    }
}

const char *RuntimeContext::getIFS() {
    if(this->handle_IFS == nullptr) {
        this->handle_IFS = this->symbolTable.lookupHandle("IFS");
        assert(this->handle_IFS != nullptr);
    }
    return typeAs<String_Object>(this->getGlobal(this->handle_IFS->getFieldIndex()))->getValue();
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
