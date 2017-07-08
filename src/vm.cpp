/*
 * Copyright (C) 2016-2017 Nagisa Sekiguchi
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
#include <sys/wait.h>

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

static DSHistory initHistory() {
    return DSHistory {
            .capacity = 0,
            .size = 0,
            .data = nullptr,
    };
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
        option(DS_OPTION_ASSERT), codeStack(),
        pathCache(), terminationHook(nullptr), lineNum(1), prompt(),
        hook(nullptr), logicalWorkingDir(initLogicalWorkingDir()),
        baseTime(std::chrono::system_clock::now()), history(initHistory()) { }

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
        std::string str = UNDEF_ENV_ERROR;
        str += name;
        throwSystemError(state, EINVAL, std::move(str));
    }
}

static void clearOperandStack(DSState &st) {
    while(st.stackTopIndex > st.stackBottomIndex) {
        st.popNoReturn();
    }
}

static void reclaimLocals(DSState &state, unsigned char offset, unsigned char size) {
    auto *limit = state.callStack + state.localVarOffset + offset;
    auto *cur = limit + size - 1;
    while(cur >= limit) {
        (cur--)->reset();
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

static constexpr unsigned int READ_PIPE = 0;
static constexpr unsigned int WRITE_PIPE = 1;

static void forkAndCapture(bool isStr, DSState &state) {
    const unsigned short offset = read16(GET_CODE(state), state.pc() + 1);

    // capture stdout
    pid_t pipefds[2];

    if(pipe(pipefds) < 0) {
        perror("pipe creation failed\n");
        exit(1);    //FIXME: throw exception
    }

    pid_t pid = xfork(state, getpgid(0), false);
    if(pid > 0) {   // parent process
        close(pipefds[WRITE_PIPE]);

        DSValue obj;

        if(isStr) {  // capture stdout as String
            char buf[256];
            std::string str;
            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, arraySize(buf));
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
            const char *ifs = typeAs<String_Object>(getGlobal(state, toIndex(BuiltinVarOffset::IFS)))->getValue();
            unsigned int skipCount = 1;

            char buf[256];
            std::string str;
            obj = DSValue::create<Array_Object>(state.pool.getStringArrayType());
            Array_Object *array = typeAs<Array_Object>(obj);

            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, arraySize(buf));
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

using pipe_t = int[2];

static void initPipe(unsigned int size, pipe_t *pipes) {
    for(unsigned int i = 0; i < size; i++) {
        if(pipe(pipes[i]) < 0) {  // create pipe
            perror("pipe creation error");
            exit(1);
        }
    }
}

static void closeAllPipe(int size, pipe_t *pipefds) {
    for(int i = 0; i < size; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }
}

class RedirConfig : public DSObject {
private:
    bool restore;

    std::vector<std::pair<RedirOP, DSValue>> ops;

    int fds[3];

    NON_COPYABLE(RedirConfig);

public:
    RedirConfig() : DSObject(nullptr), restore(true), ops() {
        this->fds[0] = dup(STDIN_FILENO);
        this->fds[1] = dup(STDOUT_FILENO);
        this->fds[2] = dup(STDERR_FILENO);
    }

    ~RedirConfig() {
        if(this->restore) {
            dup2(this->fds[0], STDIN_FILENO);
            dup2(this->fds[1], STDOUT_FILENO);
            dup2(this->fds[2], STDERR_FILENO);
        }
        for(unsigned int i = 0; i < 3; i++) {
            close(this->fds[i]);
        }
    }

    void addRedirOp(RedirOP op, DSValue &&arg) {
        this->ops.push_back(std::make_pair(op, std::move(arg)));
    }

    void setRestore(bool restore) {
        this->restore = restore;
    }

    void redirect(DSState &st) const;
};

static int doIOHere(const String_Object &value) {
    pipe_t pipe[1];
    initPipe(1, pipe);

    dup2(pipe[0][READ_PIPE], STDIN_FILENO);

    if(value.size() <= PIPE_BUF) {
        write(pipe[0][WRITE_PIPE], value.getValue(), sizeof(char) * value.size());
        write(pipe[0][WRITE_PIPE], "\n", 1);
        closeAllPipe(1, pipe);
    } else {
        pid_t pid = fork();
        if(pid < 0) {
            return errno;
        } else if(pid == 0) {   // child
            pid = fork();   // double-fork (not wait IO-here process termination.)
            if(pid == 0) {  // child
                close(pipe[0][READ_PIPE]);
                dup2(pipe[0][WRITE_PIPE], STDOUT_FILENO);
                printf("%s\n", value.getValue());
            }
            exit(0);
        }
        closeAllPipe(1, pipe);
        waitpid(pid, nullptr, 0);
    }
    return 0;
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

static int redirectImpl(const std::pair<RedirOP, DSValue> &pair) {
    switch(pair.first) {
    case RedirOP::IN_2_FILE: {
        return redirectToFile(pair.second, "rb", STDIN_FILENO);
    }
    case RedirOP::OUT_2_FILE: {
        return redirectToFile(pair.second, "wb", STDOUT_FILENO);
    }
    case RedirOP::OUT_2_FILE_APPEND: {
        return redirectToFile(pair.second, "ab", STDOUT_FILENO);
    }
    case RedirOP::ERR_2_FILE: {
        return redirectToFile(pair.second, "wb", STDERR_FILENO);
    }
    case RedirOP::ERR_2_FILE_APPEND: {
        return redirectToFile(pair.second, "ab", STDERR_FILENO);
    }
    case RedirOP::MERGE_ERR_2_OUT_2_FILE: {
        int r = redirectToFile(pair.second, "wb", STDOUT_FILENO);
        if(r != 0) {
            return r;
        }
        dup2(STDOUT_FILENO, STDERR_FILENO); //FIXME: error check
        return 0;
    }
    case RedirOP::MERGE_ERR_2_OUT_2_FILE_APPEND: {
        int r = redirectToFile(pair.second, "ab", STDOUT_FILENO);
        if(r != 0) {
            return r;
        }
        dup2(STDOUT_FILENO, STDERR_FILENO);
        return 0;
    }
    case RedirOP::MERGE_ERR_2_OUT:
        dup2(STDOUT_FILENO, STDERR_FILENO);
        return 0;
    case RedirOP::MERGE_OUT_2_ERR:
        dup2(STDERR_FILENO, STDOUT_FILENO);
        return 0;
    case RedirOP::HERE_STR:
        return doIOHere(*typeAs<String_Object>(pair.second));
    }
    return 0;   // normally unreachable, but gcc requires this return statement.
}

void RedirConfig::redirect(DSState &st) const {
    for(auto &pair : this->ops) {
        int r = redirectImpl(pair);
        if(this->restore && r != 0) {
            std::string msg = REDIR_ERROR;
            if(pair.second && !typeAs<String_Object>(pair.second)->empty()) {
                msg += typeAs<String_Object>(pair.second)->getValue();
            }
            throwSystemError(st, r, std::move(msg));
        }
    }
}

static void flushStdFD() {
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);
}

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+---------------+--------------+
 * | stack top | param1(redir) | param2(argv) |
 * +-----------+---------------+--------------+
 *             |     offset    |
 */
static void callUserDefinedCommand(DSState &st, const DSCode *code, DSValue &&argvObj, DSValue &&restoreFD, bool setvar = true) {
    // reset exit status
    st.updateExitStatus(0);

    // push parameter
    st.push(std::move(restoreFD));  // push %%redir
    st.push(std::move(argvObj));    // push argv (@)

    // set stack stack
    windStackFrame(st, 2, 2, code);

    if(setvar) {    // set variable
        auto argv = typeAs<Array_Object>(st.getLocal(1));
        eraseFirst(*argv);
        const unsigned int argSize = argv->getValues().size();
        st.setLocal(2, DSValue::create<Int_Object>(st.pool.getInt32Type(), argSize));   // #
        st.setLocal(3, st.getGlobal(toIndex(BuiltinVarOffset::POS_0))); // 0
        unsigned int limit = 9;
        if(argSize < limit) {
            limit = argSize;
        }

        unsigned int index = 0;
        for(; index < limit; index++) {
            st.setLocal(index + 4, argv->getValues()[index]);
        }

        for(; index < 9; index++) {
            st.setLocal(index + 4, st.emptyStrObj);  // set remain
        }
    }
}

enum class CmdKind {
    USER_DEFINED,
    BUILTIN_S,
    BUILTIN,
    EXTERNAL,
};

struct Command {
    CmdKind kind;
    union {
        const DSCode *udc;
        builtin_command_t builtinCmd;
        const char *filePath;   // may be null if not found file
    };
};

class CmdResolver {
private:
    flag8_set_t mask;
    flag8_set_t searchOp;

    static CStringHashMap<NativeCode> map;

public:
    static constexpr flag8_t MASK_UDC      = 1 << 0;
    static constexpr flag8_t MASK_EXTERNAL = 1 << 1;

    CmdResolver(flag8_set_t mask, flag8_set_t op) : mask(mask), searchOp(op) {}
    CmdResolver() : CmdResolver(0, 0) {}
    ~CmdResolver() = default;

    Command operator()(DSState &state, const char *cmdName) const;
};

static NativeCode initCode(OpCode op) {
    unsigned char *code = reinterpret_cast<unsigned char *>(malloc(sizeof(unsigned char) * 3));
    code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
    code[1] = static_cast<unsigned char>(op);
    code[2] = static_cast<unsigned char>(OpCode::RETURN_V);
    return NativeCode(code);
}

static CStringHashMap<NativeCode> initSBuiltinMap() {
    CStringHashMap<NativeCode> map;
    map.insert(std::make_pair("command", initCode(OpCode::BUILTIN_CMD)));
    map.insert(std::make_pair("eval", initCode(OpCode::BUILTIN_EVAL)));
    return map;
}

static const DSCode *lookupUserDefinedCommand(const DSState &st, const char *commandName) {
    auto handle = st.symbolTable.lookupUdc(commandName);
    return handle == nullptr ? nullptr : &typeAs<FuncObject>(st.getGlobal(handle->getFieldIndex()))->getCode();
}

CStringHashMap<NativeCode> CmdResolver::map = initSBuiltinMap();

Command CmdResolver::operator()(DSState &state, const char *cmdName) const {
    Command cmd;

    // first, check user-defined command
    if(!hasFlag(this->mask, MASK_UDC)) {
        auto *udcObj = lookupUserDefinedCommand(state, cmdName);
        if(udcObj != nullptr) {
            cmd.kind = CmdKind::USER_DEFINED;
            cmd.udc = udcObj;
            return cmd;
        }
    }

    // second, check builtin command
    {
        builtin_command_t bcmd = lookupBuiltinCommand(cmdName);
        if(bcmd != nullptr) {
            cmd.kind = CmdKind::BUILTIN;
            cmd.builtinCmd = bcmd;
            return cmd;
        }

        auto iter = map.find(cmdName);
        if(iter != map.end()) {
            cmd.kind = CmdKind::BUILTIN_S;
            cmd.udc = &iter->second;
            return cmd;
        }
    }

    // resolve external command path
    cmd.kind = CmdKind::EXTERNAL;
    cmd.filePath = hasFlag(this->mask, MASK_EXTERNAL) ? nullptr : state.pathCache.searchPath(cmdName, this->searchOp);
    return cmd;
}

static void throwCmdError(DSState &state, const char *cmdName, int errnum) {
    std::string str = EXEC_ERROR;
    str += cmdName;
    if(errnum == ENOENT) {
        str += ": command not found";
        throwError(state, state.pool.getSystemErrorType(), std::move(str));
    }
    throwSystemError(state, errnum, std::move(str));
}

static int forkAndExec(DSState &state, const char *cmdName, Command cmd, char **const argv) {
    // setup self pipe
    int selfpipe[2];
    if(pipe(selfpipe) < 0) {
        perror("pipe creation error");
        exit(1);
    }
    if(fcntl(selfpipe[WRITE_PIPE], F_SETFD, fcntl(selfpipe[WRITE_PIPE], F_GETFD) | FD_CLOEXEC)) {
        perror("fcntl error");
        exit(1);
    }

    bool rootShell = state.isRootShell();
    pid_t pid = xfork(state, rootShell ? 0 : getpgid(0), rootShell);
    if(pid == -1) {
        perror("child process error");
        exit(1);
    } else if(pid == 0) {   // child
        xexecve(cmd.filePath, argv, nullptr);

        int errnum = errno;
        write(selfpipe[WRITE_PIPE], &errnum, sizeof(int));
        exit(-1);
    } else {    // parent process
        close(selfpipe[WRITE_PIPE]);
        int readSize;
        int errnum = 0;
        while((readSize = read(selfpipe[READ_PIPE], &errnum, sizeof(int))) == -1) {
            if(errno != EAGAIN && errno != EINTR) {
                break;
            }
        }
        close(selfpipe[READ_PIPE]);
        if(readSize > 0 && errnum == ENOENT) {  // remove cached path
            getPathCache(state).removePath(argv[0]);
        }

        int status;
        xwaitpid(state, pid, status, 0);
        if(state.isInteractive() && rootShell) {
            tcsetpgrp(STDIN_FILENO, getpgid(0));
        }
        if(errnum != 0) {
            state.updateExitStatus(1);
            throwCmdError(state, cmdName, errnum);
        }
        return status;
    }
}

static void pushExitStatus(DSState &state, int status) {
    state.updateExitStatus(status);
    state.push(status == 0 ? state.trueObj : state.falseObj);
}

static void callCommand(DSState &state, Command cmd, DSValue &&argvObj, DSValue &&redirConfig, bool needFork) {
    auto *array = typeAs<Array_Object>(argvObj);
    const unsigned int size = array->getValues().size();
    auto &first = array->getValues()[0];
    const char *cmdName = typeAs<String_Object>(first)->getValue();

    switch(cmd.kind) {
    case CmdKind::USER_DEFINED:
    case CmdKind::BUILTIN_S: {
        bool setvar = cmd.kind == CmdKind::USER_DEFINED;
        callUserDefinedCommand(state, cmd.udc, std::move(argvObj), std::move(redirConfig), setvar);
        return;
    }
    case CmdKind::BUILTIN: {
        if(needFork && redirConfig && strcmp(cmdName, "exec") == 0) { // when call exec command as single command
            typeAs<RedirConfig>(redirConfig)->setRestore(false);
        }
        int status = cmd.builtinCmd(state, *array);
        pushExitStatus(state, status);
        flushStdFD();
        return;
    }
    case CmdKind::EXTERNAL: {
        // create argv
        char *argv[size + 1];
        for(unsigned int i = 0; i < size; i++) {
            argv[i] = const_cast<char *>(str(array->getValues()[i]));
        }
        argv[size] = nullptr;

        if(needFork) {
            int status = forkAndExec(state, cmdName, cmd, argv);
            int r = 0;
            if(WIFEXITED(status)) {
                r = WEXITSTATUS(status);
            }
            if(WIFSIGNALED(status)) {
                r = WTERMSIG(status);
            }
            pushExitStatus(state, r);
        } else {
            xexecve(cmd.filePath, argv, nullptr);
            throwCmdError(state, cmdName, errno);
        }
        return;
    }
    }
}

int invalidOptionError(const Array_Object &obj, const GetOptState &s);

static void callBuiltinCommand(DSState &state, DSValue &&argvObj, DSValue &&redir) {
    auto &arrayObj = *typeAs<Array_Object>(argvObj);

    bool useDefaultPath = false;

    /**
     * if 0, ignore
     * if 1, show description
     * if 2, show detailed description
     */
    unsigned char showDesc = 0;

    GetOptState optState;
    for(int opt; (opt = optState(arrayObj, "pvV")) != -1;) {
        switch(opt) {
        case 'p':
            useDefaultPath = true;
            break;
        case 'v':
            showDesc = 1;
            break;
        case 'V':
            showDesc = 2;
            break;
        default:
            int s = invalidOptionError(arrayObj, optState);
            pushExitStatus(state, s);
            return;
        }
    }

    unsigned int index = optState.index;
    const unsigned int argc = arrayObj.getValues().size();
    if(index < argc) {
        if(showDesc == 0) { // execute command
            const char *cmdName = str(arrayObj.getValues()[index]);
            auto &values = arrayObj.refValues();
            values.erase(values.begin(), values.begin() + index);

            auto resolve = CmdResolver(CmdResolver::MASK_UDC, useDefaultPath ? FilePathCache::USE_DEFAULT_PATH : 0);
            callCommand(state, resolve(state, cmdName), std::move(argvObj), std::move(redir), true);
            return;
        } else {    // show command description
            unsigned int successCount = 0;
            for(; index < argc; index++) {
                const char *commandName = str(arrayObj.getValues()[index]);
                auto cmd = CmdResolver(0, FilePathCache::DIRECT_SEARCH)(state, commandName);
                switch(cmd.kind) {
                case CmdKind::USER_DEFINED: {
                    successCount++;
                    fputs(commandName, stdout);
                    if(showDesc == 2) {
                        fputs(" is an user-defined command", stdout);
                    }
                    fputc('\n', stdout);
                    continue;
                }
                case CmdKind::BUILTIN_S:
                case CmdKind::BUILTIN: {
                    successCount++;
                    fputs(commandName, stdout);
                    if(showDesc == 2) {
                        fputs(" is a shell builtin command", stdout);
                    }
                    fputc('\n', stdout);
                    continue;
                }
                case CmdKind::EXTERNAL: {
                    const char *path = cmd.filePath;
                    if(path != nullptr) {
                        if(showDesc == 1) {
                            printf("%s\n", path);
                        } else if(getPathCache(state).isCached(commandName)) {
                            printf("%s is hashed (%s)\n", commandName, path);
                        } else {
                            printf("%s is %s\n", commandName, path);
                        }
                        continue;
                    }
                    break;
                }
                }

                if(showDesc == 2) {
                    PERROR(arrayObj, "%s", commandName);
                }
            }
            pushExitStatus(state, successCount > 0 ? 0 : 1);
            return;
        }
    }
    pushExitStatus(state, 0);
}

struct Process {
    pid_t pid;
    int status;

    enum Kind {
        EXIT,
        SIGNAL,
    } kind;
};

static void callPipeline(DSState &state) {
    const unsigned int branchSize = read8(GET_CODE(state), state.pc() + 1);
    assert(branchSize > 1);
    const unsigned int size = branchSize - 1;

    int pipefds[size][2];
    initPipe(size, pipefds);

    // fork
    Process procs[size];
    const bool rootShell = state.isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    pid_t pid;
    unsigned int procIndex;
    for(procIndex = 0; procIndex < size && (pid = xfork(state, pgid, rootShell)) > 0; procIndex++) {
        procs[procIndex].pid = pid;
        if(!pgid) {
            pgid = pid;
        }
    }

    if(procIndex == size) { // parent
        closeAllPipe(size, pipefds);

        // wait for exit
        for(unsigned int i = 0; i < size; i++) {
            int status = 0;
            xwaitpid(state, procs[i].pid, status, 0);
            if(WIFEXITED(status)) {
                procs[i].kind = Process::EXIT;
                procs[i].status = WEXITSTATUS(status);

            }
            if(WIFSIGNALED(status)) {
                procs[i].kind = Process::SIGNAL;
                procs[i].status = WTERMSIG(status);
            }
        }
        if(state.isInteractive() && rootShell) {
            tcsetpgrp(STDIN_FILENO, getpgid(0));
        }
        pushExitStatus(state, procs[size - 1].status);

        /**
         * code layout
         *
         * +----------+-------------+-----------+    +------------+--------------+
         * | PIPELINE | size: 1byte | br1: 2yte | ~  | brN: 2byte | merge: 2byte |
         * +----------+-------------+-----------+    +------------+--------------+
         */
        // set pc to next instruction
        state.pc() += read16(GET_CODE(state), state.pc() + 2 + size * 2) - 1;
    } else if(pid == 0) {   // child
        if(procIndex == 0) {    // first process
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex > 0 && procIndex < size - 1) {   // other process.
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == size - 1) { // last process
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
        }
        closeAllPipe(size, pipefds);

        // set pc to next instruction
        state.pc() += read16(GET_CODE(state), state.pc() + 2 + procIndex * 2) - 1;
    } else {
        perror("child process error");
        exit(1);
    }
}

static void addCmdArg(DSState &state, bool skipEmptyStr) {
    /**
     * stack layout
     *
     * ===========> stack grow
     * +------+-------+-------+
     * | argv | redir | value |
     * +------+-------+-------+
     */
    DSValue value = state.pop();
    DSType *valueType = value->getType();

    Array_Object *argv = typeAs<Array_Object>(state.callStack[state.stackTopIndex - 1]);
    if(*valueType == state.pool.getStringType()) {  // String
        if(skipEmptyStr && typeAs<String_Object>(value)->empty()) {
            return;
        }
        argv->append(std::move(value));
        return;
    }

    assert(*valueType == state.pool.getStringArrayType());  // Array<String>
    Array_Object *arrayObj = typeAs<Array_Object>(value);
    for(auto &element : arrayObj->getValues()) {
        if(typeAs<String_Object>(element)->empty()) {
            continue;
        }
        argv->append(element);
    }
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
        const OpCode op = static_cast<OpCode>(GET_CODE(state)[++state.pc()]);
        if(state.hook != nullptr) {
            state.hook->vmFetchHook(state, op);
        }

        // dispatch instruction
        vmswitch(op) {
        vmcase(HALT) {
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
        vmcase(PUSH_NULL) {
            state.push(nullptr);
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
        vmcase(LOAD_CONST_T) {
            unsigned int index = read24(GET_CODE(state), state.pc() + 1);
            state.pc() += 3;
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
            unsigned char index = read8(GET_CODE(state), ++state.pc());
            state.loadLocal(index);
            break;
        }
        vmcase(STORE_LOCAL) {
            unsigned char index = read8(GET_CODE(state), ++state.pc());
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
            state.push(state.getExitStatus() == 0 ? state.trueObj : state.falseObj);
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
            const unsigned int offset = read16(GET_CODE(state), state.pc() + 1);
            const unsigned int savedIndex = state.pc() + 2;
            state.push(DSValue::createNum(savedIndex));
            state.pc() += offset - 1;
            break;
        }
        vmcase(EXIT_FINALLY) {
            switch(state.peek().kind()) {
            case DSValueKind::OBJECT:
            case DSValueKind::INVALID: {
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
            state.storeThrowObject();
            return false;
        }
        vmcase(CAPTURE_STR)
        vmcase(CAPTURE_ARRAY) {
            forkAndCapture(op == OpCode::CAPTURE_STR, state);
            break;
        }
        vmcase(PIPELINE) {
            callPipeline(state);
            break;
        }
        vmcase(EXPAND_TILDE) {
            std::string str = typeAs<String_Object>(state.pop())->getValue();
            expandTilde(str);
            state.push(DSValue::create<String_Object>(state.pool.getStringType(), std::move(str)));
            break;
        }
        vmcase(NEW_CMD) {
            auto v = state.pop();
            auto obj = DSValue::create<Array_Object>(state.pool.getStringArrayType());
            Array_Object *argv = typeAs<Array_Object>(obj);
            argv->append(std::move(v));
            state.push(std::move(obj));
            break;
        }
        vmcase(ADD_CMD_ARG) {
            unsigned char v = read8(GET_CODE(state), ++state.pc());
            addCmdArg(state, v > 0);
            break;
        }
        vmcase(CALL_CMD)
        vmcase(CALL_CMD_P) {
            bool needFork = op == OpCode::CALL_CMD;

            auto redir = state.pop();
            auto argv = state.pop();

            const char *cmdName = str(typeAs<Array_Object>(argv)->getValues()[0]);
            callCommand(state, CmdResolver()(state, cmdName), std::move(argv), std::move(redir), needFork);
            break;
        }
        vmcase(BUILTIN_CMD) {
            auto redir = std::move(state.getLocal(0));
            auto argv = std::move(state.getLocal(1));
            callBuiltinCommand(state, std::move(argv), std::move(redir));
            break;
        }
        vmcase(BUILTIN_EVAL) {
            auto redir = std::move(state.getLocal(0));
            auto argv = std::move(state.getLocal(1));

            eraseFirst(*typeAs<Array_Object>(argv));
            auto *array = typeAs<Array_Object>(argv);
            if(array->getValues().size()) {
                const char *cmdName = str(typeAs<Array_Object>(argv)->getValues()[0]);
                callCommand(state, CmdResolver()(state, cmdName), std::move(argv), std::move(redir), true);
            } else {
                pushExitStatus(state, 0);
            }
            break;
        }
        vmcase(NEW_REDIR) {
            state.push(DSValue::create<RedirConfig>());
            break;
        }
        vmcase(ADD_REDIR_OP) {
            unsigned char v = read8(GET_CODE(state), ++state.pc());
            auto value = state.pop();
            typeAs<RedirConfig>(state.peek())->addRedirOp(static_cast<RedirOP>(v), std::move(value));
            break;
        }
        vmcase(DO_REDIR) {
            typeAs<RedirConfig>(state.peek())->redirect(state);
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
        vmcase(UNWRAP) {
            if(state.peek().kind() == DSValueKind::INVALID) {
                throwError(state, state.pool.getUnwrappingErrorType(), std::string("invalid value"));
            }
            break;
        }
        vmcase(CHECK_UNWRAP) {
            bool b = state.pop().kind() != DSValueKind::INVALID;
            state.push(b ? state.trueObj : state.falseObj);
            break;
        }
        vmcase(TRY_UNWRAP) {
            unsigned short offset = read16(GET_CODE(state), state.pc() + 1);
            if(state.peek().kind() == DSValueKind::INVALID) {
                state.popNoReturn();
                state.pc() += 2;
            } else {
                state.pc() += offset - 1;
            }
            break;
        }
        vmcase(NEW_INVALID) {
            state.push(DSValue::createInvalid());
            break;
        }
        vmcase(RECLAIM_LOCAL) {
            unsigned char offset = read8(GET_CODE(state), ++state.pc());
            unsigned char size = read8(GET_CODE(state), ++state.pc());

            reclaimLocals(state, offset, size);
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
    if(state.hook != nullptr) {
        state.hook->vmThrowHook(state);
    }

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
                    reclaimLocals(state, entry.localOffset, entry.localSize);
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
    unsigned int localSize = static_cast<const CompiledCode *>(CODE(state))->getLocalVarNum();
    reclaimLocals(state, 0, localSize);
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
    auto cmd = CmdResolver(CmdResolver::MASK_EXTERNAL | CmdResolver::MASK_UDC, 0)(st, argv[0]);
    if(cmd.builtinCmd == nullptr) {
        fprintf(stderr, "ydsh: %s: not builtin command\n", argv[0]);
        st.updateExitStatus(1);
        return 1;
    }

    // init parameter
    std::vector<DSValue> values;
    for(; *argv != nullptr; argv++) {
        values.push_back(DSValue::create<String_Object>(st.pool.getStringType(), std::string(*argv)));
    }
    auto obj = DSValue::create<Array_Object>(st.pool.getStringArrayType(), std::move(values));

    st.resetState();
    callCommand(st, cmd, std::move(obj), DSValue(), false);
    if(st.codeStack.size()) {
        mainLoop(st);
    }
    st.popNoReturn();
    return st.getExitStatus();
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