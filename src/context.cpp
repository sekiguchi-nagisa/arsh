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
#include "parser.h"
#include "misc/num.h"
#include "misc/time.h"
#include "misc/files.h"

namespace ydsh {

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
            free(const_cast<char *>(iter->first));
            this->map.erase(iter);
        }
    }
}

bool FilePathCache::isCached(const char *cmdName) const {
    return this->map.find(cmdName) != this->map.end();
}

void FilePathCache::clear() {
    for(auto &pair : this->map) {
        free(const_cast<char *>(pair.first));
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
        callStack(new DSValue[DEFAULT_STACK_SIZE]),
        callStackSize(DEFAULT_STACK_SIZE), globalVarSize(0),
        stackTopIndex(0), stackBottomIndex(0), localVarOffset(0), pc_(0),
        traceExit(false), IFS_index(0),
        codeStack_(), pipelineEvaluator(nullptr),
        pathCache(), terminationHook(nullptr) { }

RuntimeContext::~RuntimeContext() {
    delete[] this->callStack;
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

void RuntimeContext::finalizeScriptArg() {
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
    this->globalVarSize = size;
    this->localVarOffset = size;
}

void RuntimeContext::reserveLocalVar(unsigned int size) {
    this->stackTopIndex = size;
    if(size >= this->callStackSize) {
        this->expandLocalStack();
    }
    this->stackBottomIndex = size;
}

DSValue RuntimeContext::newError(DSType &errorType, std::string &&message) {
    return Error_Object::newError(*this, errorType, DSValue::create<String_Object>(
            this->pool.getStringType(), std::move(message)));
}

void RuntimeContext::throwException(DSValue &&except) {
    this->thrownObject = std::move(except);
    throw DSExcepton();
}

void RuntimeContext::throwError(DSType &errorType, const char *message) {
    this->throwError(errorType, std::string(message));
}

void RuntimeContext::throwError(DSType &errorType, std::string &&message) {
    this->throwException(this->newError(errorType, std::move(message)));
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

void RuntimeContext::expandLocalStack() {
    const unsigned int needSize = this->stackTopIndex;
    if(needSize >= MAXIMUM_STACK_SIZE) {
        this->stackTopIndex = this->callStackSize - 1;
        this->throwError(this->pool.getStackOverflowErrorType(), "local stack size reaches limit");
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

void RuntimeContext::clearOperandStack() {
    while(this->stackTopIndex > this->stackBottomIndex) {
        this->popNoReturn();
    }
}

void RuntimeContext::windStackFrame(unsigned int stackTopOffset, unsigned int paramSize, const DSCode *code) {
    const unsigned int maxVarSize = code->is(CodeKind::NATIVE)? paramSize :
                                    static_cast<const CompiledCode *>(code)->getLocalVarNum();

    const unsigned int oldLocalVarOffset = this->localVarOffset;
    const unsigned int oldStackBottomIndex = this->stackBottomIndex;
    const unsigned int oldStackTopIndex = this->stackTopIndex - stackTopOffset;
    const unsigned int oldPC = this->pc_;

    // change stack state
    const unsigned int localVarOffset = this->stackTopIndex - paramSize + 1;
    this->reserveLocalVar(this->stackTopIndex + maxVarSize - paramSize + 4);
    this->localVarOffset = localVarOffset;
    this->pc_ = 0;

    // push old state
    this->callStack[this->stackTopIndex] = DSValue::createNum(oldLocalVarOffset);
    this->callStack[this->stackTopIndex - 1] = DSValue::createNum(oldStackBottomIndex);
    this->callStack[this->stackTopIndex - 2] = DSValue::createNum(oldStackTopIndex);
    this->callStack[this->stackTopIndex - 3] = DSValue::createNum(oldPC);

    // push callable
    this->codeStack().push_back(code);
    this->skipHeader();
}

void RuntimeContext::unwindStackFrame() {
    this->clearOperandStack();

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

    v = this->pop();
    assert(v.kind() == DSValueKind::NUMBER);
    const unsigned int oldPC = static_cast<unsigned int>(v.value());


    // restore state
    this->localVarOffset = oldLocalVarOffset;
    this->stackBottomIndex = oldStackBottomIndex;
    this->pc_ = oldPC;

    // unwind local variable
    while(this->stackTopIndex > oldStackTopIndex) {
        this->popNoReturn();
    }

    // pop callable
    this->codeStack().pop_back();
}

void RuntimeContext::skipHeader() {
    assert(!this->codeStack_.empty());
    this->pc() = 0;
    if(this->codeStack().back()->is(CodeKind::TOPLEVEL)) {
        const CompiledCode *code = static_cast<const CompiledCode *>(this->codeStack().back());

        unsigned short varNum = code->getLocalVarNum();
        unsigned short gvarNum = code->getGlobalVarNum();

        this->reserveGlobalVar(gvarNum);
        this->reserveLocalVar(this->getLocalVarOffset() + varNum);
    }
    this->pc() += this->codeStack().back()->getCodeOffset() - 1;
}

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+---------+--------+   +--------+
 * | stack top | funcObj | param1 | ~ | paramN |
 * +-----------+---------+--------+   +--------+
 *                       | offset |   |        |
 */
void RuntimeContext::applyFuncObject(unsigned int paramSize) {
    auto *func = typeAs<FuncObject>(this->callStack[this->stackTopIndex - paramSize]);
    this->windStackFrame(paramSize + 1, paramSize, &func->getCode());
}

/**
 * stack state in method call    stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             | offset           |   |        |
 */
void RuntimeContext::callMethod(unsigned short index, unsigned short paramSize) {
    const unsigned int actualParamSize = paramSize + 1; // include receiver
    const unsigned int recvIndex = this->stackTopIndex - paramSize;

    this->windStackFrame(actualParamSize, actualParamSize,
                         this->callStack[recvIndex]->getType()->getMethodRef(index));
}

void RuntimeContext::newDSObject(DSType *type) {
    if(!type->isRecordType()) {
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
void RuntimeContext::callConstructor(unsigned short paramSize) {
    const unsigned int recvIndex = this->stackTopIndex - paramSize;

    this->windStackFrame(paramSize, paramSize + 1,
                         this->callStack[recvIndex]->getType()->getConstructor());
}

const NativeCode *getNativeCode(unsigned int index);

/**
 * stack state in method call    stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             | offset           |   |        |
 */
void RuntimeContext::invokeMethod(unsigned short constPoolIndex) {
    assert(!this->codeStack().back()->is(CodeKind::NATIVE));
    const CompiledCode *code = static_cast<const CompiledCode *>(this->codeStack().back());

    auto pair = decodeMethodDescriptor(typeAs<String_Object>(code->getConstPool()[constPoolIndex])->getValue());
    const char *methodName = pair.first;
    auto handle = pair.second;
    const unsigned int actualParamSize = handle->getParamTypes().size() + 1;    // include receiver
    const unsigned int recvIndex = this->stackTopIndex - handle->getParamTypes().size();

    this->windStackFrame(actualParamSize, actualParamSize, getNativeCode(0));
    DSValue ret = typeAs<ProxyObject>(this->callStack[recvIndex])->invokeMethod(*this, methodName, handle);
    this->unwindStackFrame();

    if(ret) {
        this->push(std::move(ret));
    }
}

void RuntimeContext::invokeGetter(unsigned short constPoolIndex) {
    assert(!this->codeStack().back()->is(CodeKind::NATIVE));
    const CompiledCode *code = static_cast<const CompiledCode *>(this->codeStack().back());

    auto tuple = decodeFieldDescriptor(typeAs<String_Object>(code->getConstPool()[constPoolIndex])->getValue());
    const DSType *recvType = std::get<0>(tuple);
    const char *fieldName = std::get<1>(tuple);
    const DSType *fieldType = std::get<2>(tuple);
    const unsigned int recvIndex = this->stackTopIndex;

    this->windStackFrame(1, 1, getNativeCode(0));
    DSValue ret = typeAs<ProxyObject>(
            this->callStack[recvIndex])->invokeGetter(*this, recvType, fieldName, fieldType);
    this->unwindStackFrame();

    assert(ret);
    this->push(std::move(ret));
}

/**
 * stack state in setter call    stack grow ===>
 *
 * +-----------+--------+-------+
 * | stack top |  recv  | value |
 * +-----------+--------+-------+
 *             | offset |       |
 */
void RuntimeContext::invokeSetter(unsigned short constPoolIndex) {
    assert(!this->codeStack().back()->is(CodeKind::NATIVE));
    const CompiledCode *code = static_cast<const CompiledCode *>(this->codeStack().back());

    auto tuple = decodeFieldDescriptor(typeAs<String_Object>(code->getConstPool()[constPoolIndex])->getValue());
    const DSType *recvType = std::get<0>(tuple);
    const char *fieldName = std::get<1>(tuple);
    const DSType *fieldType = std::get<2>(tuple);
    const unsigned int recvIndex = this->stackTopIndex - 1;

    this->windStackFrame(2, 2, getNativeCode(0));
    typeAs<ProxyObject>(this->callStack[recvIndex])->invokeSetter(*this, recvType, fieldName, fieldType);
    this->unwindStackFrame();
}

void RuntimeContext::handleUncaughtException(DSValue &&except) {
    std::cerr << "[runtime error]" << std::endl;
    const bool bt = this->pool.getErrorType().isSameOrBaseTypeOf(*except->getType());
    auto *handle = except->getType()->lookupMethodHandle(this->pool, bt ? "backtrace" : OP_STR);

    try {
        DSValue ret = ydsh::callMethod(*this, handle, std::move(except), std::vector<DSValue>());
        if(!bt) {
            std::cerr << typeAs<String_Object>(ret)->getValue() << std::endl;
        }
    } catch(const DSExcepton &) {
        std::cerr << "cannot obtain string representation" << std::endl;
    }

    if(typeAs<Int_Object>(this->getGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::SHELL_PID)))->getValue() !=
            typeAs<Int_Object>(this->getGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::PID)))->getValue()) {
        exit(1);    // in child process.
    }
}

void RuntimeContext::fillInStackTrace(std::vector<StackTraceElement> &stackTrace) {
    unsigned int callableDepth = this->codeStack_.size();

    unsigned int curPC = this->pc();
    unsigned int curBottomIndex = this->stackBottomIndex;

    while(callableDepth) {
        auto &callable = this->codeStack()[--callableDepth];
        if(!callable->is(CodeKind::NATIVE)) {
            const CompiledCode *cc = static_cast<const CompiledCode *>(callable);

            // create stack trace element
            const char *sourceName = cc->getSrcInfo()->getSourceName().c_str();
            unsigned int pos = getSourcePos(cc->getSourcePosEntries(), curPC);

            std::string callableName;
            switch(callable->getKind()) {
            case CodeKind::TOPLEVEL:
                callableName += "<toplevel>";
                break;
            case CodeKind::FUNCTION:
                callableName += "function ";
                callableName += cc->getName();
                break;
            case CodeKind::USER_DEFINED_CMD:
                callableName += "command ";
                callableName += cc->getName();
                break;
            default:
                break;
            }

            stackTrace.push_back(StackTraceElement(
                    sourceName, cc->getSrcInfo()->getLineNum(pos), std::move(callableName)));
        }

        // unwind state
        if(callableDepth) {
            const unsigned int offset = curBottomIndex;
            curPC = static_cast<unsigned int>(this->callStack[offset - 3].value());
            curBottomIndex = static_cast<unsigned int>(this->callStack[offset - 1].value());
        }
    }
}

void RuntimeContext::resetState() {
    this->localVarOffset = this->globalVarSize;
    this->thrownObject.reset();
    this->codeStack_.clear();
}

bool RuntimeContext::changeWorkingDir(const char *dest, const bool useLogical) {
    if(dest == nullptr) {
        return true;
    }

    const bool tryChdir = strlen(dest) != 0;
    std::string actualDest;
    if(tryChdir) {
        if(useLogical) {
            actualDest = expandDots(logicalWorkingDir.c_str(), dest);
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
            logicalWorkingDir = std::move(actualDest);
        } else {
            size_t size = PATH_MAX;
            char buf[size];
            const char *cwd = getcwd(buf, size);
            if(cwd != nullptr) {
                setenv(ENV_PWD, cwd, 1);
            }
            logicalWorkingDir = cwd;
        }
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
    this->thrownObject = this->newError(this->pool.getShellExit(), std::move(str));

    // invoke termination hook
    if(this->terminationHook != nullptr) {
        const unsigned int lineNum =
                getOccuredLineNum(typeAs<Error_Object>(this->getThrownObject())->getStackTrace());
        this->terminationHook(DS_EXEC_STATUS_EXIT, lineNum);
    }

    // print stack trace
    if(this->traceExit) {
        this->loadThrownObject();
        typeAs<Error_Object>(this->pop())->printStackTrace(*this);
    }

    exit(status);
}

FuncObject *RuntimeContext::lookupUserDefinedCommand(const char *commandName) {
    auto handle = this->symbolTable.lookupUdc(commandName);
    return handle == nullptr ? nullptr : typeAs<FuncObject>(this->getGlobal(handle->getFieldIndex()));
}

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+-------+--------+
 * | stack top | param |
 * +-----------+-------+--------+
 *             | offset|
 */
void RuntimeContext::callUserDefinedCommand(const FuncObject *obj, DSValue *argArray) {
    // create parameter (@)
    std::vector<DSValue> values;
    for(int i = 1; argArray[i]; i++) {
        values.push_back(std::move(argArray[i]));
    }
    this->push(DSValue::create<Array_Object>(this->pool.getStringArrayType(), std::move(values)));

    // set stack stack
    this->windStackFrame(1, 1, &obj->getCode());

    // set variable
    auto argv = typeAs<Array_Object>(this->getLocal(0));
    const unsigned int argSize = argv->getValues().size();
    this->setLocal(1, DSValue::create<Int_Object>(this->pool.getInt32Type(), argSize));   // #
    this->setLocal(2, this->getGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::POS_0))); // 0
    unsigned int limit = 9;
    if(argSize < limit) {
        limit = argSize;
    }

    unsigned int index;
    for(index = 0; index < limit; index++) {
        this->setLocal(index + 3, argv->getValues()[index]);
    }

    for(; index < 9; index++) {
        this->setLocal(index + 3, this->getEmptyStrObj());  // set remain
    }
}

static void format2digit(int num, std::string &out) {
    if(num < 10) {
        out += "0";
    }
    out += std::to_string(num);
}

static const char *safeBasename(const char *str) {
    const char *ptr = strrchr(str, '/');
    return ptr == nullptr ? str : ptr + 1;
}

void RuntimeContext::interpretPromptString(const char *ps, std::string &output) {
    output.clear();

    struct tm *local = getLocalTime();

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

pid_t RuntimeContext::xfork() {
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

        // update PID, PPID
        this->setGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::PID),
                        DSValue::create<Int_Object>(this->pool.getUint32Type(), getpid()));
        this->setGlobal(this->getBuiltinVarIndex(BuiltinVarOffset::PPID),
                        DSValue::create<Int_Object>(this->pool.getUint32Type(), getppid()));
    }
    return pid;
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
    for(auto iter = ctx.getSymbolTable().cbeginGlobal(); iter != ctx.getSymbolTable().cendGlobal(); ++iter) {
        const char *name = iter->first.c_str();
        if(startsWith(name, SymbolTable::cmdSymbolPrefix)) {
            name += strlen(SymbolTable::cmdSymbolPrefix);
            if(startsWith(name, token.c_str())) {
                append(results, name);
            }
        }
    }

    // search builtin command
    const unsigned int bsize = getBuiltinCommandSize();
    for(unsigned int i = 0; i < bsize; i++) {
        const char *name = getBuiltinCommandName(i);
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
        targetDir = expandDots(RuntimeContext::getLogicalWorkingDir(), targetDir.c_str());
    } else {
        targetDir = expandDots(RuntimeContext::getLogicalWorkingDir(), ".");
    }
    LOG(DUMP_CONSOLE, "targetDir = " << targetDir);

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

            std::string fileName = entry->d_name;
            if(S_ISDIR(getStMode(fullpath.c_str()))) {
                fileName += '/';
            }
            append(results, fileName);
        }
    }
    closedir(dir);
}

static void completeGlobalVarName(RuntimeContext &ctx, const std::string &token, CStrBuffer &results) {
    const auto &symbolTable = ctx.getSymbolTable();

    for(auto iter = symbolTable.cbeginGlobal(); iter != symbolTable.cendGlobal(); ++iter) {
        const char *varName = iter->first.c_str();
        if(!token.empty() && !startsWith(varName, SymbolTable::cmdSymbolPrefix)
           && startsWith(varName, token.c_str() + 1)) {
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
                    kind = selectWithCmd(lexer, tokenPairs, cursor, tokenPairs.size() - 1, tokenStr, true);
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

} // namespace ydsh
