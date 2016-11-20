/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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

#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>

#include "opcode.h"
#include "vm.h"
#include "logger.h"
#include "symbol.h"
#include "misc/files.h"


// #####################
// ##     DSState     ##
// #####################

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
        hook(nullptr), logicalWorkingDir(initLogicalWorkingDir()), baseTime(std::chrono::system_clock::now()) { }

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


extern char **environ;

namespace ydsh {

#define vmswitch(V) switch(V)

#if 0
#define vmcase(code) case OpCode::code: {fprintf(stderr, "pc: %u, code: %s\n", ctx.pc(), #code); }
#else
#define vmcase(code) case OpCode::code:
#endif

#define CODE(ctx) (ctx.codeStack.back())
#define GET_CODE(ctx) (CODE(ctx)->getCode())
#define CONST_POOL(ctx) (static_cast<const CompiledCode *>(CODE(ctx))->getConstPool())

/* runtime api */
static void checkCast(DSState &state, DSType *targetType) {
    if(!state.peek()->introspect(state, targetType)) {
        DSType *stackTopType = state.pop()->getType();
        std::string str("cannot cast ");
        str += state.pool.getTypeName(*stackTopType);
        str += " to ";
        str += state.pool.getTypeName(*targetType);
        throwError(state, state.pool.getTypeCastErrorType(), std::move(str));
    }
}

static void checkAssertion(DSState &state) {
    auto msg(state.pop());
    assert(typeAs<String_Object>(msg)->getValue() != nullptr);

    if(!typeAs<Boolean_Object>(state.pop())->getValue()) {
        auto except = Error_Object::newError(state, state.pool.getAssertFail(), std::move(msg));

        // invoke termination hook
        if(state.terminationHook != nullptr) {
            const unsigned int lineNum = getOccurredLineNum(typeAs<Error_Object>(except)->getStackTrace());
            state.terminationHook(DS_ERROR_KIND_ASSERTION_ERROR, lineNum);
        }

        // print stack trace
        typeAs<Error_Object>(except)->printStackTrace(state);
        exit(1);
    }
}

/**
 * convert errno to SystemError.
 * errorNum must not be 0.
 * format message '%s: %s', message, strerror(errorNum)
 */
static void throwSystemError(DSState &st, int errorNum, std::string &&message) {
    if(errorNum == 0) {
        fatal("errno is not 0\n");
    }

    std::string str(std::move(message));
    str += ": ";
    str += strerror(errorNum);
    throwError(st, st.pool.getSystemErrorType(), std::move(str));
}

static void importEnv(DSState &state, bool hasDefault) {
    DSValue dValue;
    if(hasDefault) {
        dValue = state.pop();
    }
    DSValue nameObj(state.pop());
    const char *name = typeAs<String_Object>(nameObj)->getValue();

    const char *env = getenv(name);
    if(hasDefault && env == nullptr) {
        setenv(name, typeAs<String_Object>(dValue)->getValue(), 1);
    }

    env = getenv(name);
    if(env == nullptr) {
        std::string str("undefined environmental variable: ");
        str += name;
        throwSystemError(state, EINVAL, std::move(str)); //FIXME: exception
    }
}

static void clearOperandStack(DSState &st) {
    while(st.stackTopIndex > st.stackBottomIndex) {
        st.popNoReturn();
    }
}

/**
 * set stackTopIndex.
 * if this->localStackSize < size, expand callStack.
 */
static void reserveLocalVar(DSState &st, unsigned int size) {
    st.stackTopIndex = size;
    if(size >= st.callStackSize) {
        st.expandLocalStack();
    }
    st.stackBottomIndex = size;
}

/**
 * reserve global variable entry and set local variable offset.
 */
static void reserveGlobalVar(DSState &st, unsigned int size) {
    reserveLocalVar(st, size);
    st.globalVarSize = size;
    st.localVarOffset = size;
}

static void skipHeader(DSState &st) {
    assert(!st.codeStack.empty());
    st.pc_ = 0;
    st.pc_ += st.codeStack.back()->getCodeOffset() - 1;
}

static void windStackFrame(DSState &st, unsigned int stackTopOffset, unsigned int paramSize, const DSCode *code) {
    const unsigned int maxVarSize = code->is(CodeKind::NATIVE)? paramSize :
                                    static_cast<const CompiledCode *>(code)->getLocalVarNum();

    const unsigned int oldLocalVarOffset = st.localVarOffset;
    const unsigned int oldStackBottomIndex = st.stackBottomIndex;
    const unsigned int oldStackTopIndex = st.stackTopIndex - stackTopOffset;
    const unsigned int oldPC = st.pc_;

    // change stack state
    const unsigned int localVarOffset = st.stackTopIndex - paramSize + 1;
    reserveLocalVar(st, st.stackTopIndex + maxVarSize - paramSize + 4);
    st.localVarOffset = localVarOffset;
    st.pc_ = 0;

    // push old state
    st.callStack[st.stackTopIndex] = DSValue::createNum(oldLocalVarOffset);
    st.callStack[st.stackTopIndex - 1] = DSValue::createNum(oldStackBottomIndex);
    st.callStack[st.stackTopIndex - 2] = DSValue::createNum(oldStackTopIndex);
    st.callStack[st.stackTopIndex - 3] = DSValue::createNum(oldPC);

    // push callable
    st.codeStack.push_back(code);
    skipHeader(st);
}

static void unwindStackFrame(DSState &st) {
    // check stack layout
    assert(st.callStack[st.stackBottomIndex].kind() == DSValueKind::NUMBER);
    assert(st.callStack[st.stackBottomIndex - 1].kind() == DSValueKind::NUMBER);
    assert(st.callStack[st.stackBottomIndex - 2].kind() == DSValueKind::NUMBER);
    assert(st.callStack[st.stackBottomIndex - 3].kind() == DSValueKind::NUMBER);

    // pop old state
    const unsigned int oldLocalVarOffset = static_cast<unsigned int>(st.callStack[st.stackBottomIndex].value());
    const unsigned int oldStackBottomIndex = static_cast<unsigned int>(st.callStack[st.stackBottomIndex - 1].value());
    const unsigned int oldStackTopIndex = static_cast<unsigned int>(st.callStack[st.stackBottomIndex - 2].value());
    const unsigned int oldPC = static_cast<unsigned int>(st.callStack[st.stackBottomIndex - 3].value());

    // restore state
    st.localVarOffset = oldLocalVarOffset;
    st.stackBottomIndex = oldStackBottomIndex;
    st.pc_ = oldPC;

    // unwind operand and local variable
    while(st.stackTopIndex > oldStackTopIndex) {
        st.popNoReturn();
    }

    // pop callable
    st.codeStack.pop_back();
}

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+---------+--------+   +--------+
 * | stack top | funcObj | param1 | ~ | paramN |
 * +-----------+---------+--------+   +--------+
 *                       | offset |   |        |
 */
static void applyFuncObject(DSState &st, unsigned int paramSize) {
    auto *func = typeAs<FuncObject>(st.callStack[st.stackTopIndex - paramSize]);
    windStackFrame(st, paramSize + 1, paramSize, &func->getCode());
}

/**
 * stack state in method call    stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             | offset           |   |        |
 */
static void callMethod(DSState &st, unsigned short index, unsigned short paramSize) {
    const unsigned int actualParamSize = paramSize + 1; // include receiver
    const unsigned int recvIndex = st.stackTopIndex - paramSize;

    windStackFrame(st, actualParamSize, actualParamSize, st.callStack[recvIndex]->getType()->getMethodRef(index));
}


/**
 * stack state in constructor call     stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             |    new offset    |
 */
static void callConstructor(DSState &st, unsigned short paramSize) {
    const unsigned int recvIndex = st.stackTopIndex - paramSize;

    windStackFrame(st, paramSize, paramSize + 1, st.callStack[recvIndex]->getType()->getConstructor());
}

const NativeCode *getNativeCode(unsigned int index);

/**
 * invoke interface method.
 *
 * stack state in method call    stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             | offset           |   |        |
 *
 * @param st
 * @param constPoolIndex
 * indicates the constant pool index of descriptor object.
 */
static void invokeMethod(DSState &st, unsigned short constPoolIndex) {
    assert(!st.codeStack.back()->is(CodeKind::NATIVE));
    const CompiledCode *code = static_cast<const CompiledCode *>(st.codeStack.back());

    auto pair = decodeMethodDescriptor(typeAs<String_Object>(code->getConstPool()[constPoolIndex])->getValue());
    const char *methodName = pair.first;
    auto handle = pair.second;
    const unsigned int actualParamSize = handle->getParamTypes().size() + 1;    // include receiver
    const unsigned int recvIndex = st.stackTopIndex - handle->getParamTypes().size();

    windStackFrame(st, actualParamSize, actualParamSize, getNativeCode(0));
    DSValue ret = typeAs<ProxyObject>(st.callStack[recvIndex])->invokeMethod(st, methodName, handle);
    unwindStackFrame(st);

    if(ret) {
        st.push(std::move(ret));
    }
}

/**
 * invoke interface getter.
 * @param st
 * @param constPoolIndex
 * indicates the constant pool index of descriptor object.
 */
static void invokeGetter(DSState &st, unsigned short constPoolIndex) {
    assert(!st.codeStack.back()->is(CodeKind::NATIVE));
    const CompiledCode *code = static_cast<const CompiledCode *>(st.codeStack.back());

    auto tuple = decodeFieldDescriptor(typeAs<String_Object>(code->getConstPool()[constPoolIndex])->getValue());
    const DSType *recvType = std::get<0>(tuple);
    const char *fieldName = std::get<1>(tuple);
    const DSType *fieldType = std::get<2>(tuple);
    const unsigned int recvIndex = st.stackTopIndex;

    windStackFrame(st, 1, 1, getNativeCode(0));
    DSValue ret = typeAs<ProxyObject>(st.callStack[recvIndex])->invokeGetter(st, recvType, fieldName, fieldType);
    unwindStackFrame(st);

    assert(ret);
    st.push(std::move(ret));
}

/**
 * invoke interface setter.
 *
 * stack state in setter call    stack grow ===>
 *
 * +-----------+--------+-------+
 * | stack top |  recv  | value |
 * +-----------+--------+-------+
 *             | offset |       |
 *
 * @param st
 * @param constPoolIndex
 * indicates the constant pool index of descriptor object
 */
static void invokeSetter(DSState &st, unsigned short constPoolIndex) {
    assert(!st.codeStack.back()->is(CodeKind::NATIVE));
    const CompiledCode *code = static_cast<const CompiledCode *>(st.codeStack.back());

    auto tuple = decodeFieldDescriptor(typeAs<String_Object>(code->getConstPool()[constPoolIndex])->getValue());
    const DSType *recvType = std::get<0>(tuple);
    const char *fieldName = std::get<1>(tuple);
    const DSType *fieldType = std::get<2>(tuple);
    const unsigned int recvIndex = st.stackTopIndex - 1;

    windStackFrame(st, 2, 2, getNativeCode(0));
    typeAs<ProxyObject>(st.callStack[recvIndex])->invokeSetter(st, recvType, fieldName, fieldType);
    unwindStackFrame(st);
}



/* for substitution */

static bool isSpace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

static bool isFieldSep(const char *ifs, int ch) {
    for(unsigned int i = 0; ifs[i] != '\0'; i++) {
        if(ifs[i] == ch) {
            return true;
        }
    }
    return false;
}

static bool hasSpace(const char *ifs) {
    for(unsigned int i = 0; ifs[i] != '\0'; i++) {
        if(isSpace(ifs[i])) {
            return true;
        }
    }
    return false;
}

static void forkAndCapture(bool isStr, DSState &state) {
    const unsigned short offset = read16(GET_CODE(state), state.pc() + 1);

    // capture stdout
    pid_t pipefds[2];

    if(pipe(pipefds) < 0) {
        perror("pipe creation failed\n");
        exit(1);    //FIXME: throw exception
    }

    pid_t pid = xfork(state);
    if(pid > 0) {   // parent process
        close(pipefds[WRITE_PIPE]);

        DSValue obj;

        if(isStr) {  // capture stdout as String
            static const int bufSize = 256;
            char buf[bufSize];
            std::string str;
            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, bufSize);
                if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if(readSize <= 0) {
                    break;
                }
                str.append(buf, readSize);
            }

            // remove last newlines
            std::string::size_type pos = str.find_last_not_of('\n');
            if(pos == std::string::npos) {
                str.clear();
            } else {
                str.erase(pos + 1);
            }

            obj = DSValue::create<String_Object>(state.pool.getStringType(), std::move(str));
        } else {    // capture stdout as String Array
            const char *ifs = getIFS(state);
            unsigned int skipCount = 1;

            static const int bufSize = 256;
            char buf[bufSize];
            std::string str;
            obj = DSValue::create<Array_Object>(state.pool.getStringArrayType());
            Array_Object *array = typeAs<Array_Object>(obj);

            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, bufSize);
                if(readSize == -1 && (errno == EINTR || errno == EAGAIN)) {
                    continue;
                }
                if(readSize <= 0) {
                    break;
                }

                for(int i = 0; i < readSize; i++) {
                    char ch = buf[i];
                    bool fieldSep = isFieldSep(ifs, ch);
                    if(fieldSep && skipCount > 0) {
                        if(isSpace(ch)) {
                            continue;
                        }
                        if(--skipCount == 1) {
                            continue;
                        }
                    }
                    skipCount = 0;
                    if(fieldSep) {
                        array->append(DSValue::create<String_Object>(state.pool.getStringType(), std::move(str)));
                        str = "";
                        skipCount = isSpace(ch) ? 2 : 1;
                        continue;
                    }
                    str += ch;
                }
            }

            // remove last newline
            while(!str.empty() && str.back() == '\n') {
                str.pop_back();
            }

            // append remain
            if(!str.empty() || !hasSpace(ifs)) {
                array->append(DSValue::create<String_Object>(state.pool.getStringType(), std::move(str)));
            }
        }
        close(pipefds[READ_PIPE]);

        // wait exit
        int status;
        xwaitpid(state, pid, status, 0);
        if(WIFEXITED(status)) {
            state.updateExitStatus(WEXITSTATUS(status));
        }
        if(WIFSIGNALED(status)) {
            state.updateExitStatus(WTERMSIG(status));
        }

        // push object
        state.push(std::move(obj));

        state.pc() += offset - 1;
    } else if(pid == 0) {   // child process
        dup2(pipefds[WRITE_PIPE], STDOUT_FILENO);
        close(pipefds[READ_PIPE]);
        close(pipefds[WRITE_PIPE]);

        state.pc() += 2;
    } else {
        perror("fork failed");
        exit(1);    //FIXME: throw exception
    }
}


/* for pipeline evaluation */

/**
 * if filePath is null, not execute and set ENOENT.
 * argv is not null.
 * envp may be null.
 * if success, not return.
 */
void xexecve(const char *filePath, char **argv, char *const *envp) {
    if(filePath == nullptr) {
        errno = ENOENT;
        return;
    }

    // set env
    setenv("_", filePath, 1);
    if(envp == nullptr) {
        envp = environ;
    }

    LOG_L(DUMP_EXEC, [&](std::ostream &stream) {
        stream << "execve(" << filePath << ", [";
        for(unsigned int i = 0; argv[i] != nullptr; i++) {
            if(i > 0) {
                stream << ", ";
            }
            stream << argv[i];
        }
        stream << "])";
    });

    // execute external command
    execve(filePath, argv, envp);
}


// for error reporting
struct ChildError {
    /**
     * index of redirect option having some error.
     * if 0, has no error in redirection.
     */
    unsigned int redirIndex;

    /**
     * error number of occurred error.
     */
    int errorNum;

    ChildError() : redirIndex(0), errorNum(0) { }
    ~ChildError() = default;

    operator bool() const {
        return errorNum == 0 && redirIndex == 0;
    }
};

class ProcState {
public:
    enum ProcKind : unsigned int {
        EXTERNAL,
        BUILTIN,
        USER_DEFINED,
    };

    enum ExitKind : unsigned int {
        NORMAL,
        INTR,
    };

private:
    unsigned int argOffset_;

    /**
     * if not have redirect option, offset is 0.
     */
    unsigned int redirOffset_;

    ProcKind procKind_;

    union {
        void *dummy_;
        FuncObject *udcObj_;
        builtin_command_t builtinCmd_;
        const char *filePath_;   // may be null if not found file
    };


    /**
     * following fields are valid, if parent process.
     */

    ExitKind kind_;
    pid_t pid_;
    int exitStatus_;

public:
    ProcState() = default;

    ProcState(unsigned int argOffset, unsigned int redirOffset, ProcKind procKind, void *ptr) :
            argOffset_(argOffset), redirOffset_(redirOffset),
            procKind_(procKind), dummy_(ptr),
            kind_(NORMAL), pid_(0), exitStatus_(0) { }

    ~ProcState() = default;

    /**
     * only called, if parent process.
     */
    void set(ExitKind kind, int exitStatus) {
        this->kind_ = kind;
        this->exitStatus_ = exitStatus;
    }

    /**
     * only called, if parent process.
     */
    void setPid(pid_t pid) {
        this->pid_ = pid;
    }

    unsigned int argOffset() const {
        return this->argOffset_;
    }

    unsigned int redirOffset() const {
        return this->redirOffset_;
    }

    ProcKind procKind() const {
        return this->procKind_;
    }

    FuncObject *udcObj() const {
        return this->udcObj_;
    }

    builtin_command_t builtinCmd() const {
        return this->builtinCmd_;
    }

    const char *filePath() const {
        return this->filePath_;
    }

    ExitKind kind() const {
        return this->kind_;
    }

    pid_t pid() const {
        return this->pid_;
    }

    int exitStatus() const {
        return this->exitStatus_;
    }
};

using pipe_t = int[2];

class PipelineState : public DSObject {
public:
    /**
     * commonly stored object is String_Object.
     */
    std::vector<DSValue> argArray;

    /**
     * pair's second must be String_Object
     */
    std::vector<std::pair<RedirectOP, DSValue>> redirOptions;

    std::vector<ProcState> procStates;

    pipe_t *selfpipes;

    NON_COPYABLE(PipelineState);

    PipelineState() : DSObject(nullptr), argArray(), redirOptions(), procStates(), selfpipes(nullptr) {}

    ~PipelineState();

    void clear() {
        this->argArray.clear();
        this->redirOptions.clear();
        this->procStates.clear();
        delete[] this->selfpipes;
        this->selfpipes = nullptr;
    }

    void redirect(DSState &state, unsigned int procIndex, int errorPipe);

    DSValue *getARGV(unsigned int procIndex);
    const char *getCommandName(unsigned int procIndex);

    void checkChildError(DSState &state, const std::pair<unsigned int, ChildError> &errorPair);
};

// ###########################
// ##     PipelineState     ##
// ###########################

PipelineState::~PipelineState() {
    delete[] this->selfpipes;
}

static void closeAllPipe(int size, int pipefds[][2]) {
    for(int i = 0; i < size; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }
}

/**
 * if failed, return non-zero value(errno)
 */
static int redirectToFile(const DSValue &fileName, const char *mode, int targetFD) {
    FILE *fp = fopen(typeAs<String_Object>(fileName)->getValue(), mode);
    if(fp == NULL) {
        return errno;
    }
    int fd = fileno(fp);
    dup2(fd, targetFD);
    fclose(fp);
    return 0;
}

/**
 * do redirection and report error.
 * if errorPipe is -1, report error.
 * if errorPipe is not -1, report error and exit 1
 */
void PipelineState::redirect(DSState &state, unsigned int procIndex, int errorPipe) {
#define CHECK_ERROR(result) do { occurredError = (result); if(occurredError != 0) { goto ERR; } } while(false)

    int occurredError = 0;

    unsigned int startIndex = this->procStates[procIndex].redirOffset();
    for(; this->redirOptions[startIndex].first != RedirectOP::DUMMY; startIndex++) {
        auto &pair = this->redirOptions[startIndex];
        switch(pair.first) {
        case IN_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "rb", STDIN_FILENO));
            break;
        }
        case OUT_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDOUT_FILENO));
            break;
        }
        case OUT_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDOUT_FILENO));
            break;
        }
        case ERR_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDERR_FILENO));
            break;
        }
        case ERR_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDERR_FILENO));
            break;
        }
        case MERGE_ERR_2_OUT_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDOUT_FILENO));
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        }
        case MERGE_ERR_2_OUT_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDOUT_FILENO));
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        }
        case MERGE_ERR_2_OUT: {
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        }
        case MERGE_OUT_2_ERR: {
            dup2(STDERR_FILENO, STDOUT_FILENO);
            break;
        }
        default:
            fatal("unsupported redir option: %d\n", pair.first);
        }
    }

    ERR:
    if(occurredError != 0) {
        ChildError e;
        e.redirIndex = startIndex;
        e.errorNum = occurredError;

        if(errorPipe == -1) {
            this->checkChildError(state, std::make_pair(0, e));
        }
        write(errorPipe, &e, sizeof(ChildError));
        exit(0);
    }

#undef CHECK_ERROR
}

DSValue *PipelineState::getARGV(unsigned int procIndex) {
    return this->argArray.data() + this->procStates[procIndex].argOffset();
}

static void saveStdFD(int (&origFds)[3]) {
    origFds[0] = dup(STDIN_FILENO);
    origFds[1] = dup(STDOUT_FILENO);
    origFds[2] = dup(STDERR_FILENO);
}

static void restoreStdFD(int (&origFds)[3]) {
    dup2(origFds[0], STDIN_FILENO);
    dup2(origFds[1], STDOUT_FILENO);
    dup2(origFds[2], STDERR_FILENO);

    for(unsigned int i = 0; i < 3; i++) {
        close(origFds[i]);
    }
}

static void flushStdFD() {
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);
}

static PipelineState &activePipeline(DSState &state) {
    return *typeAs<PipelineState>(state.peek());
}

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+-------+--------+
 * | stack top | param |
 * +-----------+-------+--------+
 *             | offset|
 */
void callUserDefinedCommand(DSState &st, const FuncObject *obj, DSValue *argArray) {
    // create parameter (@)
    std::vector<DSValue> values;
    for(int i = 1; argArray[i]; i++) {
        values.push_back(std::move(argArray[i]));
    }
    st.push(DSValue::create<Array_Object>(st.pool.getStringArrayType(), std::move(values)));

    // set stack stack
    windStackFrame(st, 1, 1, &obj->getCode());

    // set variable
    auto argv = typeAs<Array_Object>(st.getLocal(0));
    const unsigned int argSize = argv->getValues().size();
    st.setLocal(1, DSValue::create<Int_Object>(st.pool.getInt32Type(), argSize));   // #
    st.setLocal(2, st.getGlobal(toIndex(BuiltinVarOffset::POS_0))); // 0
    unsigned int limit = 9;
    if(argSize < limit) {
        limit = argSize;
    }

    unsigned int index;
    for(index = 0; index < limit; index++) {
        st.setLocal(index + 3, argv->getValues()[index]);
    }

    for(; index < 9; index++) {
        st.setLocal(index + 3, st.emptyStrObj);  // set remain
    }
}

static void callCommand(DSState &state, unsigned short procIndex) {
    // reset exit status
    state.updateExitStatus(0);

    auto &pipeline = activePipeline(state);
    const unsigned int procSize = pipeline.procStates.size();

    const auto &procState = pipeline.procStates[procIndex];
    DSValue *ptr = pipeline.getARGV(procIndex);
    const auto procKind = procState.procKind();
    if(procKind == ProcState::ProcKind::USER_DEFINED) { // invoke user-defined command
        auto *udcObj = procState.udcObj();
        closeAllPipe(procSize, pipeline.selfpipes);
        callUserDefinedCommand(state, udcObj, ptr);
        return;
    } else {
        // create argv
        unsigned int argc = 1;
        for(; ptr[argc]; argc++);
        char *argv[argc + 1];
        for(unsigned int i = 0; i < argc; i++) {
            argv[i] = const_cast<char *>(typeAs<String_Object>(ptr[i])->getValue());
        }
        argv[argc] = nullptr;

        if(procKind == ProcState::ProcKind::BUILTIN) {  // invoke builtin command
            const bool inParent = procSize == 1 && procIndex == 0;
            builtin_command_t cmd_ptr = procState.builtinCmd();
            if(inParent) {
                const bool restoreFD = strcmp(argv[0], "exec") != 0;

                int origFDs[3];
                if(restoreFD) {
                    saveStdFD(origFDs);
                }

                pipeline.redirect(state, 0, -1);

                const int pid = getpid();
                state.updateExitStatus(cmd_ptr(state, argc, argv));

                if(pid == getpid()) {   // in parent process (if call command or eval, may be child)
                    // flush and restore
                    flushStdFD();
                    if(restoreFD) {
                        restoreStdFD(origFDs);
                    }

                    state.pc()++; // skip next instruction (EXIT_CHILD)
                }
            } else {
                closeAllPipe(procSize, pipeline.selfpipes);
                state.updateExitStatus(cmd_ptr(state, argc, argv));
            }
            return;
        } else {    // invoke external command
            xexecve(procState.filePath(), argv, nullptr);

            ChildError e;
            e.errorNum = errno;

            write(pipeline.selfpipes[procIndex][WRITE_PIPE], &e, sizeof(ChildError));
            exit(1);
        }
    }
}

/**
 * initialize pipe and selfpipe
 */
static void initPipe(PipelineState &pipeline, unsigned int size, pipe_t *pipes) {
    pipeline.selfpipes = new pipe_t[size];

    for(unsigned int i = 0; i < size; i++) {
        if(pipe(pipes[i]) < 0) {  // create pipe
            perror("pipe creation error");
            exit(1);
        }
        if(pipe(pipeline.selfpipes[i]) < 0) {    // create self-pipe for error reporting
            perror("pipe creation error");
            exit(1);
        }
        if(fcntl(pipeline.selfpipes[i][WRITE_PIPE], F_SETFD,
                 fcntl(pipeline.selfpipes[i][WRITE_PIPE], F_GETFD) | FD_CLOEXEC)) {
            perror("fcntl error");
            exit(1);
        }
    }
}

static void callPipeline(DSState &state) {
    auto &pipeline = activePipeline(state);
    const unsigned int procSize = pipeline.procStates.size();

    // check builtin command
    if(procSize == 1 && pipeline.procStates[0].procKind() == ProcState::ProcKind::BUILTIN) {
        // set pc to next instruction
        state.pc() += read16(GET_CODE(state), state.pc() + 2) - 1;
        return;
    }

    int pipefds[procSize][2];
    initPipe(pipeline, procSize, pipefds);


    // fork
    pid_t pid;
    std::pair<unsigned int, ChildError> errorPair;
    unsigned int procIndex;
    unsigned int actualProcSize = 0;
    for(procIndex = 0; procIndex < procSize && (pid = xfork(state)) > 0; procIndex++) {
        actualProcSize++;
        pipeline.procStates[procIndex].setPid(pid);

        // check error via self-pipe
        int readSize;
        ChildError childError;
        close(pipeline.selfpipes[procIndex][WRITE_PIPE]);
        while((readSize = read(pipeline.selfpipes[procIndex][READ_PIPE], &childError, sizeof(childError))) == -1) {
            if(errno != EAGAIN && errno != EINTR) {
                break;
            }
        }
        if(readSize > 0 && !childError) {   // if error happened, stop forking.
            errorPair.first = procIndex;
            errorPair.second = childError;

            if(childError.errorNum == ENOENT) { // if file not found, remove path cache
                const char *cmdName = pipeline.getCommandName(procIndex);
                state.pathCache.removePath(cmdName);
            }
            procIndex = procSize;
            break;
        }
    }

    if(procIndex == procSize) {   // parent process
        // close unused pipe
        closeAllPipe(procSize, pipefds);
        closeAllPipe(procSize, pipeline.selfpipes);

        // wait for exit
        for(unsigned int i = 0; i < actualProcSize; i++) {
            int status = 0;
            xwaitpid(state, pipeline.procStates[i].pid(), status, 0);
            if(WIFEXITED(status)) {
                pipeline.procStates[i].set(ProcState::NORMAL, WEXITSTATUS(status));
            }
            if(WIFSIGNALED(status)) {
                pipeline.procStates[i].set(ProcState::INTR, WTERMSIG(status));
            }
        }

        state.updateExitStatus(pipeline.procStates[actualProcSize - 1].exitStatus());
        pipeline.checkChildError(state, errorPair);

        // set pc to next instruction
        unsigned int byteSize = read8(GET_CODE(state), state.pc() + 1);
        state.pc() += read16(GET_CODE(state), state.pc() + 2 + (byteSize - 1) * 2) - 1;
    } else if(pid == 0) { // child process
        if(procIndex == 0) {    // first process
            if(procSize > 1) {
                dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
            }
        }
        if(procIndex > 0 && procIndex < procSize - 1) {   // other process.
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == procSize - 1) { // last process
            if(procSize > 1) {
                dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            }
        }

        pipeline.redirect(state, procIndex, pipeline.selfpipes[procIndex][WRITE_PIPE]);

        closeAllPipe(procSize, pipefds);

        // set pc to next instruction
        state.pc() += read16(GET_CODE(state), state.pc() + 2 + procIndex * 2) - 1;
    } else {
        perror("child process error");
        exit(1);
    }
}

const char *PipelineState::getCommandName(unsigned int procIndex) {
    return typeAs<String_Object>(this->getARGV(procIndex)[0])->getValue();
}

void PipelineState::checkChildError(DSState &state, const std::pair<unsigned int, ChildError> &errorPair) {
    if(!errorPair.second) {
        auto &pair = this->redirOptions[errorPair.second.redirIndex];

        std::string msg;
        if(pair.first == RedirectOP::DUMMY) {  // execution error
            msg += "execution error: ";
            msg += this->getCommandName(errorPair.first);
        } else {    // redirection error
            msg += "io redirection error: ";
            if(pair.second && typeAs<String_Object>(pair.second)->size() != 0) {
                msg += typeAs<String_Object>(pair.second)->getValue();
            }
        }
        state.updateExitStatus(1);
        throwSystemError(state, errorPair.second.errorNum, std::move(msg));
    }
}


/**
 * stack top value must be String_Object and it represents command name.
 */
static void openProc(DSState &state) {
    DSValue value = state.pop();

    // resolve proc kind (external command, builtin command or user-defined command)
    const char *commandName = typeAs<String_Object>(value)->getValue();
    ProcState::ProcKind procKind = ProcState::EXTERNAL;
    void *ptr = nullptr;

    // first, check user-defined command
    {
        auto *udcObj = lookupUserDefinedCommand(state, commandName);
        if(udcObj != nullptr) {
            procKind = ProcState::ProcKind::USER_DEFINED;
            ptr = udcObj;
        }
    }

    // second, check builtin command
    if(ptr == nullptr) {
        builtin_command_t bcmd = lookupBuiltinCommand(commandName);
        if(bcmd != nullptr) {
            procKind = ProcState::ProcKind::BUILTIN;
            ptr = (void *)bcmd;
        }
    }

    // resolve external command path
    if(ptr == nullptr) {
        ptr = (void *)state.pathCache.searchPath(commandName);
    }

    auto &pipeline = activePipeline(state);
    unsigned int argOffset = pipeline.argArray.size();
    unsigned int redirOffset = pipeline.redirOptions.size();
    pipeline.procStates.push_back(ProcState(argOffset, redirOffset, procKind, ptr));

    pipeline.argArray.push_back(std::move(value));
}

static void closeProc(DSState &state) {
    auto &pipeline = activePipeline(state);
    pipeline.argArray.push_back(DSValue());
    pipeline.redirOptions.push_back(std::make_pair(RedirectOP::DUMMY, DSValue()));
}

/**
 * stack top value must be String_Object or Array_Object.
 */
static void addArg(DSState &state, bool skipEmptyString) {
    DSValue value = state.pop();
    DSType *valueType = value->getType();
    if(*valueType == state.pool.getStringType()) {  // String
        if(skipEmptyString && typeAs<String_Object>(value)->empty()) {
            return;
        }
        activePipeline(state).argArray.push_back(std::move(value));
        return;
    }

    assert(*valueType == state.pool.getStringArrayType());  // Array<String>
    Array_Object *arrayObj = typeAs<Array_Object>(value);
    for(auto &element : arrayObj->getValues()) {
        if(typeAs<String_Object>(element)->empty()) {
            continue;
        }
        activePipeline(state).argArray.push_back(element);
    }
}

/**
 * stack top value must be String_Object.
 */
static void addRedirOption(DSState &state, RedirectOP op) {
    DSValue value = state.pop();
    assert(*value->getType() == state.pool.getStringType());
    activePipeline(state).redirOptions.push_back(std::make_pair(op, value));
}

// prototype of DBus related api
void DBusInitSignal(DSState &st);

/**
 *
 * @param st
 * @return
 * last elements indicates resolved signal handler
 */
std::vector<DSValue> DBusWaitSignal(DSState &st);


static bool mainLoop(DSState &state) {
    while(true) {
        // fetch next opcode
        OpCode op = static_cast<OpCode>(GET_CODE(state)[++state.pc()]);
        if(state.hook != nullptr) {
            state.hook->vmFetchHook(state, op);
        }

        // dispatch instruction
        vmswitch(op) {
        vmcase(NOP) {
            break;
        }
        vmcase(STOP_EVAL) {
            return true;
        }
        vmcase(ASSERT) {
            checkAssertion(state);
            break;
        }
        vmcase(PRINT) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;

            auto stackTopType = reinterpret_cast<DSType *>(v);
            assert(!stackTopType->isVoidType());
            auto *strObj = typeAs<String_Object>(state.peek());
            printf("(%s) ", state.pool.getTypeName(*stackTopType).c_str());
            fwrite(strObj->getValue(), sizeof(char), strObj->size(), stdout);
            fputc('\n', stdout);
            fflush(stdout);
            state.popNoReturn();
            break;
        }
        vmcase(INSTANCE_OF) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;

            auto *targetType = reinterpret_cast<DSType *>(v);
            if(state.pop()->introspect(state, targetType)) {
                state.push(state.trueObj);
            } else {
                state.push(state.falseObj);
            }
            break;
        }
        vmcase(CHECK_CAST) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;
            checkCast(state, reinterpret_cast<DSType *>(v));
            break;
        }
        vmcase(PUSH_TRUE) {
            state.push(state.trueObj);
            break;
        }
        vmcase(PUSH_FALSE) {
            state.push(state.falseObj);
            break;
        }
        vmcase(PUSH_ESTRING) {
            state.push(state.emptyStrObj);
            break;
        }
        vmcase(LOAD_CONST) {
            unsigned char index = read8(GET_CODE(state), ++state.pc());
            state.push(CONST_POOL(state)[index]);
            break;
        }
        vmcase(LOAD_CONST_W) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.push(CONST_POOL(state)[index]);
            break;
        }
        vmcase(LOAD_FUNC) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.loadGlobal(index);

            auto *func = typeAs<FuncObject>(state.peek());
            if(func->getType() == nullptr) {
                auto *handle = state.symbolTable.lookupHandle(func->getCode().getName());
                assert(handle != nullptr);
                func->setType(handle->getFieldType(state.pool));
            }
            break;
        }
        vmcase(LOAD_GLOBAL) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.loadGlobal(index);
            break;
        }
        vmcase(STORE_GLOBAL) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.storeGlobal(index);
            break;
        }
        vmcase(LOAD_LOCAL) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.loadLocal(index);
            break;
        }
        vmcase(STORE_LOCAL) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.storeLocal(index);
            break;
        }
        vmcase(LOAD_FIELD) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.loadField(index);
            break;
        }
        vmcase(STORE_FIELD) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.storeField(index);
            break;
        }
        vmcase(IMPORT_ENV) {
            unsigned char b = read8(GET_CODE(state), ++state.pc());
            importEnv(state, b > 0);
            break;
        }
        vmcase(LOAD_ENV) {
            DSValue name = state.pop();
            const char *value = getenv(typeAs<String_Object>(name)->getValue());
            assert(value != nullptr);
            state.push(DSValue::create<String_Object>(state.pool.getStringType(), value));
            break;
        }
        vmcase(STORE_ENV) {
            DSValue value(state.pop());
            DSValue name(state.pop());

            setenv(typeAs<String_Object>(name)->getValue(),
                   typeAs<String_Object>(value)->getValue(), 1);//FIXME: check return value and throw
            break;
        }
        vmcase(POP) {
            state.popNoReturn();
            break;
        }
        vmcase(DUP) {
            state.dup();
            break;
        }
        vmcase(DUP2) {
            state.dup2();
            break;
        }
        vmcase(SWAP) {
            state.swap();
            break;
        }
        vmcase(NEW_STRING) {
            state.push(DSValue::create<String_Object>(state.pool.getStringType()));
            break;
        }
        vmcase(APPEND_STRING) {
            DSValue v(state.pop());
            typeAs<String_Object>(state.peek())->append(std::move(v));
            break;
        }
        vmcase(NEW_ARRAY) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;
            state.push(DSValue::create<Array_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(APPEND_ARRAY) {
            DSValue v(state.pop());
            typeAs<Array_Object>(state.peek())->append(std::move(v));
            break;
        }
        vmcase(NEW_MAP) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;
            state.push(DSValue::create<Map_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(APPEND_MAP) {
            DSValue value(state.pop());
            DSValue key(state.pop());
            typeAs<Map_Object>(state.peek())->add(std::make_pair(std::move(key), std::move(value)));
            break;
        }
        vmcase(NEW_TUPLE) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;
            state.push(DSValue::create<Tuple_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(NEW) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;

            DSType *type = reinterpret_cast<DSType *>(v);
            if(!type->isRecordType()) {
                state.dummy->setType(type);
                state.push(state.dummy);
            } else {
                fatal("currently, DSObject allocation not supported\n");
            }
            break;
        }
        vmcase(CALL_INIT) {
            unsigned short paramSize = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            callConstructor(state, paramSize);
            break;
        }
        vmcase(CALL_METHOD) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            unsigned short paramSize = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            callMethod(state, index, paramSize);
            break;
        }
        vmcase(CALL_FUNC) {
            unsigned short paramSize = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            applyFuncObject(state, paramSize);
            break;
        }
        vmcase(CALL_NATIVE) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;
            native_func_t func = (native_func_t) v;
            DSValue returnValue = func(state);
            if(returnValue) {
                state.push(std::move(returnValue));
            }
            break;
        }
        vmcase(INVOKE_METHOD) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            invokeMethod(state, index);
            break;
        }
        vmcase(INVOKE_GETTER) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            invokeGetter(state, index);
            break;
        }
        vmcase(INVOKE_SETTER) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            invokeSetter(state, index);
            break;
        }
        vmcase(RETURN) {
            unwindStackFrame(state);
            if(state.codeStack.empty()) {
                return true;
            }
            break;
        }
        vmcase(RETURN_V) {
            DSValue v(state.pop());
            unwindStackFrame(state);
            state.push(std::move(v));
            if(state.codeStack.empty()) {
                return true;
            }
            break;
        }
        vmcase(RETURN_UDC) {
            auto v = state.pop();
            unwindStackFrame(state);
            state.updateExitStatus(typeAs<Int_Object>(v)->getValue());
            if(state.codeStack.empty()) {
                return true;
            }
            break;
        }
        vmcase(BRANCH) {
            unsigned short offset = read16(GET_CODE(state), state.pc() + 1);
            if(typeAs<Boolean_Object>(state.pop())->getValue()) {
                state.pc() += 2;
            } else {
                state.pc() += offset - 1;
            }
            break;
        }
        vmcase(GOTO) {
            unsigned int index = read32(GET_CODE(state), state.pc() + 1);
            state.pc() = index - 1;
            break;
        }
        vmcase(THROW) {
            state.throwException(state.pop());
        }
        vmcase(ENTER_FINALLY) {
            const unsigned int index = read32(GET_CODE(state), state.pc() + 1);
            const unsigned int savedIndex = state.pc() + 4;
            state.push(DSValue::createNum(savedIndex));
            state.pc() = index - 1;
            break;
        }
        vmcase(EXIT_FINALLY) {
            switch(state.peek().kind()) {
            case DSValueKind::OBJECT: {
                state.throwException(state.pop());
                break;
            }
            case DSValueKind::NUMBER: {
                unsigned int index = static_cast<unsigned int>(state.pop().value());
                state.pc() = index;
                break;
            }
            }
            break;
        }
        vmcase(COPY_INT) {
            DSType *type = state.pool.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            int v = typeAs<Int_Object>(state.pop())->getValue();
            state.push(DSValue::create<Int_Object>(*type, v));
            break;
        }
        vmcase(TO_BYTE) {
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            v &= 0xFF;  // fill higher bits (8th ~ 31) with 0
            state.push(DSValue::create<Int_Object>(state.pool.getByteType(), v));
            break;
        }
        vmcase(TO_U16) {
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            state.push(DSValue::create<Int_Object>(state.pool.getUint16Type(), v));
            break;
        }
        vmcase(TO_I16) {
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            if(v & 0x8000) {    // if 15th bit is 1, fill higher bits with 1
                v |= 0xFFFF0000;
            }
            state.push(DSValue::create<Int_Object>(state.pool.getInt16Type(), v));
            break;
        }
        vmcase(NEW_LONG) {
            DSType *type = state.pool.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            unsigned long l = v;
            state.push(DSValue::create<Long_Object>(*type, l));
            break;
        }
        vmcase(COPY_LONG) {
            DSType *type = state.pool.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            long v = typeAs<Long_Object>(state.pop())->getValue();
            state.push(DSValue::create<Long_Object>(*type, v));
            break;
        }
        vmcase(I_NEW_LONG) {
            DSType *type = state.pool.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            int v = typeAs<Int_Object>(state.pop())->getValue();
            long l = v;
            state.push(DSValue::create<Long_Object>(*type, l));
            break;
        }
        vmcase(NEW_INT) {
            DSType *type = state.pool.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            unsigned long l = typeAs<Long_Object>(state.pop())->getValue();
            unsigned int v = static_cast<unsigned int>(l);
            state.push(DSValue::create<Int_Object>(*type, v));
            break;
        }
        vmcase(U32_TO_D) {
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            double d = static_cast<double>(v);
            state.push(DSValue::create<Float_Object>(state.pool.getFloatType(), d));
            break;
        }
        vmcase(I32_TO_D) {
            int v = typeAs<Int_Object>(state.pop())->getValue();
            double d = static_cast<double>(v);
            state.push(DSValue::create<Float_Object>(state.pool.getFloatType(), d));
            break;
        }
        vmcase(U64_TO_D) {
            unsigned long v = typeAs<Long_Object>(state.pop())->getValue();
            double d = static_cast<double>(v);
            state.push(DSValue::create<Float_Object>(state.pool.getFloatType(), d));
            break;
        }
        vmcase(I64_TO_D) {
            long v = typeAs<Long_Object>(state.pop())->getValue();
            double d = static_cast<double>(v);
            state.push(DSValue::create<Float_Object>(state.pool.getFloatType(), d));
            break;
        }
        vmcase(D_TO_U32) {
            double d = typeAs<Float_Object>(state.pop())->getValue();
            unsigned int v = static_cast<unsigned int>(d);
            state.push(DSValue::create<Int_Object>(state.pool.getUint32Type(), v));
            break;
        }
        vmcase(D_TO_I32) {
            double d = typeAs<Float_Object>(state.pop())->getValue();
            int v = static_cast<int>(d);
            state.push(DSValue::create<Int_Object>(state.pool.getInt32Type(), v));
            break;
        }
        vmcase(D_TO_U64) {
            double d = typeAs<Float_Object>(state.pop())->getValue();
            unsigned long v = static_cast<unsigned long>(d);
            state.push(DSValue::create<Long_Object>(state.pool.getUint64Type(), v));
            break;
        }
        vmcase(D_TO_I64) {
            double d = typeAs<Float_Object>(state.pop())->getValue();
            long v = static_cast<long>(d);
            state.push(DSValue::create<Long_Object>(state.pool.getInt64Type(), v));
            break;
        }
        vmcase(SUCCESS_CHILD) {
            exit(state.getExitStatus());
        }
        vmcase(FAILURE_CHILD) {
            state.setThrownObject(state.pop());
            return false;
        }
        vmcase(CAPTURE_STR) {
            forkAndCapture(true, state);
            break;
        }
        vmcase(CAPTURE_ARRAY) {
            forkAndCapture(false, state);
            break;
        }
        vmcase(NEW_PIPELINE) {
            if(!state.getPipeline()) {
                state.getPipeline() = DSValue::create<PipelineState>();
            }

            if(state.getPipeline().get()->getRefcount() == 1) {   // reuse cached object
                typeAs<PipelineState>(state.getPipeline())->clear();
                state.push(state.getPipeline());
            } else {
                state.push(DSValue::create<PipelineState>());
            }
            break;
        }
        vmcase(CALL_PIPELINE) {
            callPipeline(state);
            break;
        }
        vmcase(OPEN_PROC) {
            openProc(state);
            break;
        }
        vmcase(CLOSE_PROC) {
            closeProc(state);
            break;
        }
        vmcase(ADD_CMD_ARG) {
            unsigned char v = read8(GET_CODE(state), ++state.pc());
            addArg(state, v > 0);
            break;
        }
        vmcase(ADD_REDIR_OP) {
            unsigned char v = read8(GET_CODE(state), ++state.pc());
            addRedirOption(state, static_cast<RedirectOP>(v));
            break;
        }
        vmcase(EXPAND_TILDE) {
            std::string str = typeAs<String_Object>(state.pop())->getValue();
            expandTilde(str);
            state.push(DSValue::create<String_Object>(state.pool.getStringType(), std::move(str)));
            break;
        }
        vmcase(CALL_CMD) {
            unsigned char v = read8(GET_CODE(state), ++state.pc());
            callCommand(state, v);
            break;
        }
        vmcase(POP_PIPELINE) {
            state.popNoReturn();
            if(state.getExitStatus() == 0) {
                state.push(state.trueObj);
            } else {
                state.push(state.falseObj);
            }
            break;
        }
        vmcase(DBUS_INIT_SIG) {
            DBusInitSignal(state);
            break;
        }
        vmcase(DBUS_WAIT_SIG) {
            auto v = DBusWaitSignal(state);
            for(auto &e : v) {
                state.push(std::move(e));
            }
            applyFuncObject(state, v.size() - 1);
            break;
        }
        vmcase(RAND) {
            int v = rand();
            state.push(DSValue::create<Int_Object>(state.pool.getUint32Type(), v));
            break;
        }
        vmcase(GET_SECOND) {
            auto now = std::chrono::system_clock::now();
            auto diff = now - state.baseTime;
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(diff);
            unsigned long v = typeAs<Long_Object>(state.getGlobal(toIndex(BuiltinVarOffset::SECONDS)))->getValue();
            v += sec.count();
            state.push(DSValue::create<Long_Object>(state.pool.getUint64Type(), v));
            break;
        }
        vmcase(SET_SECOND) {
            state.baseTime = std::chrono::system_clock::now();
            state.storeGlobal(toIndex(BuiltinVarOffset::SECONDS));
            break;
        }
        }
    }
}

/**
 * if found exception handler, return true.
 * otherwise return false.
 */
static bool handleException(DSState &state) {
    while(!state.codeStack.empty()) {
        if(!CODE(state)->is(CodeKind::NATIVE)) {
            auto *cc = static_cast<const CompiledCode *>(CODE(state));

            // search exception entry
            const unsigned int occurredPC = state.pc();
            const DSType *occurredType = state.getThrownObject()->getType();

            for(unsigned int i = 0; cc->getExceptionEntries()[i].type != nullptr; i++) {
                const ExceptionEntry &entry = cc->getExceptionEntries()[i];
                if(occurredPC >= entry.begin && occurredPC < entry.end
                   && entry.type->isSameOrBaseTypeOf(*occurredType)) {
                    state.pc() = entry.dest - 1;
                    clearOperandStack(state);
                    state.loadThrownObject();
                    return true;
                }
            }
        }
        if(state.codeStack.size() == 1) {
            break;  // when top level
        }
        unwindStackFrame(state);
    }
    return false;
}


/**
 * stub of D-Bus related method and api
 */

#ifndef USE_DBUS

void DBusInitSignal(DSState &) {  }   // do nothing

std::vector<DSValue> DBusWaitSignal(DSState &st) {
    throwError(st, st.pool.getErrorType(), "not support method");
    return std::vector<DSValue>();
}

DSValue newDBusObject(TypePool &pool) {
    auto v = DSValue::create<DummyObject>();
    v->setType(&pool.getDBusType());
    return v;
}

DSValue dbus_systemBus(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue dbus_sessionBus(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue dbus_available(DSState &ctx) {
    return ctx.falseObj;
}

DSValue dbus_getSrv(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue dbus_getPath(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue dbus_getIface(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue dbus_introspect(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue bus_service(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue bus_listNames(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue bus_listActiveNames(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

DSValue service_object(DSState &ctx) {
    throwError(ctx, ctx.pool.getErrorType(), "not support method");
    return DSValue();
}

#endif


} // namespace ydsh


bool vmEval(DSState &state, CompiledCode &code) {
    state.resetState();

    state.codeStack.push_back(&code);
    skipHeader(state);

    // reserve local and global variable slot
    {
        assert(state.codeStack.back()->is(CodeKind::TOPLEVEL));
        const CompiledCode *code = static_cast<const CompiledCode *>(state.codeStack.back());

        unsigned short varNum = code->getLocalVarNum();
        unsigned short gvarNum = code->getGlobalVarNum();

        reserveGlobalVar(state, gvarNum);
        reserveLocalVar(state, state.localVarOffset + varNum);
    }

    while(true) {
        try {
            bool ret = mainLoop(state);
            state.codeStack.clear();
            return ret;
        } catch(const DSException &) {
            if(handleException(state)) {
                continue;
            }
            return false;
        }
    }
}

int execBuiltinCommand(DSState &st, char *const argv[]) {
    builtin_command_t cmd = lookupBuiltinCommand(argv[0]);
    if(cmd == nullptr) {
        fprintf(stderr, "ydsh: %s: not builtin command\n", argv[0]);
        st.updateExitStatus(1);
        return 1;
    }

    int argc;
    for(argc = 0; argv[argc] != nullptr; argc++);

    int s = cmd(st, argc, argv);
    st.updateExitStatus(s);
    flushStdFD();
    return s;
}

DSValue callMethod(DSState &state, const MethodHandle *handle, DSValue &&recv, std::vector<DSValue> &&args) {
    assert(handle != nullptr);
    assert(handle->getParamTypes().size() == args.size());

    state.resetState();

    // push argument
    state.push(std::move(recv));
    const unsigned int size = args.size();
    for(unsigned int i = 0; i < size; i++) {
        state.push(std::move(args[i]));
    }

    callMethod(state, handle->getMethodIndex(), args.size());
    mainLoop(state);
    DSValue ret;
    if(!handle->getReturnType()->isVoidType()) {
        ret = state.pop();
    }
    return ret;
}