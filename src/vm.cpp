/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
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
#include <cerrno>
#include <random>

#include "opcode.h"
#include "vm.h"
#include "cmd.h"
#include "logger.h"
#include "constant.h"
#include "misc/files.h"
#include "misc/num.h"

// ##########################
// ##     SignalVector     ##
// ##########################

struct SigEntryComp {
    using Entry = std::pair<int, DSValue>;

    bool operator()(const Entry &x, int y) const {
        return x.first < y;
    }

    bool operator()(int x, const Entry &y) const {
        return x < y.first;
    }
};

void SignalVector::insertOrUpdate(int sigNum, const DSValue &func) {
    auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
    if(iter != this->data.end() && iter->first == sigNum) {
        if(func) {
            iter->second = func;    // update
        } else {
            this->data.erase(iter); // remove
        }
    } else if(func) {
        this->data.insert(iter, std::make_pair(sigNum, func));  // insert
    }
}

DSValue SignalVector::lookup(int sigNum) const {
    auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
    if(iter != this->data.end() && iter->first == sigNum) {
        return iter->second;
    }
    return nullptr;
}

// #####################
// ##     DSState     ##
// #####################

static std::string initLogicalWorkingDir() {
    const char *dir = getenv(ENV_PWD);
    if(dir == nullptr || !S_ISDIR(getStMode(dir))) {
        char *ptr = realpath(".", nullptr);
        std::string str = ptr == nullptr ? "" : ptr;
        free(ptr);
        return str;
    }
    if(dir[0] == '/') {
        return std::string(dir);
    }
    return expandDots(nullptr, dir);
}

static DSHistory initHistory() {
    return DSHistory {
            .capacity = 0,
            .size = 0,
            .data = nullptr,
    };
}

DSState::DSState() :
        trueObj(DSValue::create<Boolean_Object>(this->symbolTable.get(TYPE::Boolean), true)),
        falseObj(DSValue::create<Boolean_Object>(this->symbolTable.get(TYPE::Boolean), false)),
        emptyStrObj(DSValue::create<String_Object>(this->symbolTable.get(TYPE::String), std::string())),
        emptyFDObj(DSValue::create<UnixFD_Object>(this->symbolTable.get(TYPE::UnixFD), -1)),
        callStack(new DSValue[DEFAULT_STACK_SIZE]), logicalWorkingDir(initLogicalWorkingDir()),
        baseTime(std::chrono::system_clock::now()), history(initHistory()) { }

void DSState::reserveLocalStackImpl(unsigned int needSize) {
    unsigned int newSize = this->callStackSize;
    do {
        newSize += (newSize >> 1u);
    } while(newSize < needSize);
    auto newTable = new DSValue[newSize];
    for(unsigned int i = 0; i < this->stackTopIndex + 1; i++) {
        newTable[i] = std::move(this->callStack[i]);
    }
    delete[] this->callStack;
    this->callStack = newTable;
    this->callStackSize = newSize;
}

flag32_set_t DSState::eventDesc = 0;

unsigned int DSState::pendingSigIndex = 1;

SigSet DSState::pendingSigSet;

static void signalHandler(int sigNum) { // when called this handler, all signals are blocked due to signal mask
    DSState::pendingSigSet.add(sigNum);
    setFlag(DSState::eventDesc, DSState::VM_EVENT_SIGNAL);
}

void DSState::installSignalHandler(int sigNum, UnsafeSigOp op, const DSValue &handler, bool setSIGCHLD) {
    if(sigNum == SIGCHLD && !setSIGCHLD) {
        return;
    }

    // set posix signal handler
    struct sigaction action{};
    action.sa_flags = SA_RESTART;
    sigfillset(&action.sa_mask);

    switch(op) {
    case UnsafeSigOp::DFL:
        action.sa_handler = SIG_DFL;
        break;
    case UnsafeSigOp::IGN:
        action.sa_handler = SIG_IGN;
        break;
    case UnsafeSigOp::SET:
        action.sa_handler = signalHandler;
        break;
    }
    sigaction(sigNum, &action, nullptr);

    // register handler
    if(sigNum != SIGCHLD) {
        this->sigVector.insertOrUpdate(sigNum, handler);
    }
}


extern char **environ;

namespace ydsh {

#define CODE(ctx) ((ctx).code)
#define GET_CODE(ctx) (CODE(ctx)->getCode())
#define CONST_POOL(ctx) (static_cast<const CompiledCode *>(CODE(ctx))->getConstPool())

/* runtime api */
static bool checkCast(DSState &state, DSType *targetType) {
    if(!state.peek()->introspect(state, targetType)) {
        DSType *stackTopType = state.pop()->getType();
        std::string str("cannot cast ");
        str += state.symbolTable.getTypeName(*stackTopType);
        str += " to ";
        str += state.symbolTable.getTypeName(*targetType);
        raiseError(state, state.symbolTable.get(TYPE::TypeCastError), std::move(str));
        return false;
    }
    return true;
}

static bool checkAssertion(DSState &state) {
    auto msg(state.pop());
    assert(typeAs<String_Object>(msg)->getValue() != nullptr);

    if(!typeAs<Boolean_Object>(state.pop())->getValue()) {
        state.updateExitStatus(1);
        auto except = Error_Object::newError(state, state.symbolTable.get(TYPE::_AssertFail), std::move(msg));
        state.setThrownObject(std::move(except));
        return false;
    }
    return true;
}

static void exitShell(DSState &st, unsigned int status) {
    if(hasFlag(st.option, DS_OPTION_INTERACTIVE)) {
        st.jobTable.send(SIGHUP);
    }

    std::string str("terminated by exit ");
    str += std::to_string(status);
    status %= 256;
    st.updateExitStatus(status);
    raiseError(st, st.symbolTable.get(TYPE::_ShellExit), std::move(str));
}

static const char *loadEnv(DSState &state, bool hasDefault) {
    DSValue dValue;
    if(hasDefault) {
        dValue = state.pop();
    }
    DSValue nameObj(state.pop());
    const char *name = typeAs<String_Object>(nameObj)->getValue();

    const char *env = getenv(name);
    if(env == nullptr && hasDefault) {
        setenv(name, typeAs<String_Object>(dValue)->getValue(), 1);
        env = getenv(name);
    }

    if(env == nullptr) {
        std::string str = UNDEF_ENV_ERROR;
        str += name;
        raiseSystemError(state, EINVAL, std::move(str));
        return nullptr;
    }
    return env;
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
 * reserve global variable entry and set local variable offset.
 */
static void reserveGlobalVar(DSState &st) {
    unsigned int size = st.symbolTable.getMaxGVarIndex();
    st.reserveLocalStack(size - st.globalVarSize);
    st.globalVarSize = size;
    st.localVarOffset = size;
    st.stackTopIndex = size;
    st.stackBottomIndex = size;
}

static bool windStackFrame(DSState &st, unsigned int stackTopOffset, unsigned int paramSize, const DSCode *code) {
    const unsigned int maxVarSize = code->is(CodeKind::NATIVE) ? paramSize :
                                    static_cast<const CompiledCode *>(code)->getLocalVarNum();
    const unsigned int localVarOffset = st.stackTopIndex - paramSize + 1;
    const unsigned int operandSize = code->is(CodeKind::NATIVE) ? 4 :
                                     static_cast<const CompiledCode *>(code)->getStackDepth();

    if(st.controlStack.size() == DSState::MAX_CONTROL_STACK_SIZE) {
        raiseError(st, st.symbolTable.get(TYPE::StackOverflowError), "local stack size reaches limit");
        return false;
    }

    // save current control frame
    st.stackTopIndex -= stackTopOffset;
    st.controlStack.push_back(st.getFrame());
    st.stackTopIndex += stackTopOffset;

    // reallocate stack
    st.reserveLocalStack(maxVarSize - paramSize + operandSize);

    // prepare control frame
    st.code = code;
    st.stackTopIndex = st.stackTopIndex + maxVarSize - paramSize;
    st.stackBottomIndex = st.stackTopIndex;
    st.localVarOffset = localVarOffset;
    st.pc_ = st.code->getCodeOffset() - 1;
    return true;
}

static void unwindStackFrame(DSState &st) {
    auto frame = st.controlStack.back();

    st.code = frame.code;
    st.stackBottomIndex = frame.stackBottomIndex;
    st.localVarOffset = frame.localVarOffset;
    st.pc_= frame.pc;

    unsigned int oldStackTopIndex = frame.stackTopIndex;
    while(st.stackTopIndex > oldStackTopIndex) {
        st.popNoReturn();
    }
    st.controlStack.pop_back();
}

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+---------+--------+   +--------+
 * | stack top | funcObj | param1 | ~ | paramN |
 * +-----------+---------+--------+   +--------+
 *                       | offset |   |        |
 */
static bool applyFuncObject(DSState &st, unsigned int paramSize) {
    auto *func = typeAs<FuncObject>(st.callStack[st.stackTopIndex - paramSize]);
    return windStackFrame(st, paramSize + 1, paramSize, &func->getCode());
}

/**
 * stack state in method call    stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             | offset           |   |        |
 */
static bool callMethod(DSState &st, unsigned short index, unsigned short paramSize) {
    const unsigned int actualParamSize = paramSize + 1; // include receiver
    const unsigned int recvIndex = st.stackTopIndex - paramSize;

    return windStackFrame(st, actualParamSize, actualParamSize, st.callStack[recvIndex]->getType()->getMethodRef(index));
}


/**
 * stack state in constructor call     stack grow ===>
 *
 * +-----------+------------------+   +--------+
 * | stack top | param1(receiver) | ~ | paramN |
 * +-----------+------------------+   +--------+
 *             |    new offset    |
 */
static bool callConstructor(DSState &st, unsigned short paramSize) {
    const unsigned int recvIndex = st.stackTopIndex - paramSize;

    return windStackFrame(st, paramSize, paramSize + 1, st.callStack[recvIndex]->getType()->getConstructor());
}

const NativeCode *getNativeCode(unsigned int index);


/* for substitution */

static DSValue readAsStr(const DSState &state, int fd) {
    char buf[256];
    std::string str;
    while(true) {
        int readSize = read(fd, buf, arraySize(buf));
        if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }
        if(readSize <= 0) {
            break;
        }
        str.append(buf, readSize);
    }

    // remove last newlines
    for(; !str.empty() && str.back() == '\n'; str.pop_back());

    return DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), std::move(str));
}

static DSValue readAsStrArray(const DSState &state, int fd) {
    auto *ifsObj = typeAs<String_Object>(getGlobal(state, toIndex(BuiltinVarOffset::IFS)));
    const char *ifs = ifsObj->getValue();
    const unsigned ifsSize = ifsObj->size();
    unsigned int skipCount = 1;

    char buf[256];
    std::string str;
    auto obj = DSValue::create<Array_Object>(state.symbolTable.get(TYPE::StringArray));
    auto *array = typeAs<Array_Object>(obj);

    while(true) {
        int readSize = read(fd, buf, arraySize(buf));
        if(readSize == -1 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        if(readSize <= 0) {
            break;
        }

        for(int i = 0; i < readSize; i++) {
            char ch = buf[i];
            bool fieldSep = isFieldSep(ifsSize, ifs, ch);
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
                array->append(DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), std::move(str)));
                str = "";
                skipCount = isSpace(ch) ? 2 : 1;
                continue;
            }
            str += ch;
        }
    }

    // remove last newline
    for(; !str.empty() && str.back() == '\n'; str.pop_back());

    // append remain
    if(!str.empty() || !hasSpace(ifsSize, ifs)) {
        array->append(DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), std::move(str)));
    }

    return obj;
}

static void tryToDup(int srcFd, int targetFd) {
    if(srcFd > -1) {
        dup2(srcFd, targetFd);
    }
}

static void tryToClose(int fd) {
    if(fd > -1) {
        close(fd);
    }
}

static void tryToClose(int (&pipefds)[2]) {
    tryToClose(pipefds[0]);
    tryToClose(pipefds[1]);
}

static void tryToPipe(int (&pipefds)[2], bool openPipe) {
    if(openPipe) {
        if(pipe(pipefds) < 0) {
            perror("pipe creation failed\n");
            exit(1);    //FIXME: throw exception
        }
    } else {
        pipefds[0] = -1;
        pipefds[1] = -1;
    }
}

struct PipeSet {
    int in[2];
    int out[2];
};

// FIXME: error reporting
static PipeSet initPipeSet(ForkKind kind) {
    bool useInPipe = false;
    bool useOutPipe = false;

    switch(kind) {
    case ForkKind::STR:
    case ForkKind::ARRAY:
        useOutPipe = true;
        break;
    case ForkKind::IN_PIPE:
        useInPipe = true;
        break;
    case ForkKind::OUT_PIPE:
        useOutPipe = true;
        break;
    case ForkKind::COPROC:
        useInPipe = true;
        useOutPipe = true;
        break;
    case ForkKind::JOB:
    case ForkKind::DISOWN:
        break;
    }

    PipeSet set;
    tryToPipe(set.in, useInPipe);
    tryToPipe(set.out, useOutPipe);
    return set;
}

static void redirInToNull() {
    int fd = open("/dev/null", O_WRONLY);
    dup2(fd, STDIN_FILENO);
    close(fd);
}

static DSValue newFD(const DSState &st, int &fd) {
    if(fd < 0) {
        return st.emptyFDObj;
    }
    int v = fd;
    fd = -1;
    return DSValue::create<UnixFD_Object>(st.symbolTable.get(TYPE::UnixFD), v);
}

static constexpr unsigned int READ_PIPE = 0;
static constexpr unsigned int WRITE_PIPE = 1;

static void flushStdFD() {
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);
}


static bool forkAndEval(DSState &state) {
    const auto forkKind = static_cast<ForkKind >(read8(GET_CODE(state), ++state.pc()));
    const unsigned short offset = read16(GET_CODE(state), state.pc() + 1);

    // flush standard stream due to prevent mixing io buffer
    flushStdFD();

    // set in/out pipe
    auto pipeset = initPipeSet(forkKind);
    pid_t pgid = state.isRootShell() ? 0 : getpgid(0);
    auto proc = Proc::fork(state, pgid, false);
    if(proc.pid() > 0) {   // parent process
        tryToClose(pipeset.in[READ_PIPE]);
        tryToClose(pipeset.out[WRITE_PIPE]);

        DSValue obj;

        switch(forkKind) {
        case ForkKind::STR:
        case ForkKind::ARRAY: {
            tryToClose(pipeset.in[WRITE_PIPE]);
            const bool isStr = forkKind == ForkKind::STR;
            obj = isStr ? readAsStr(state, pipeset.out[READ_PIPE]) : readAsStrArray(state, pipeset.out[READ_PIPE]);
            tryToClose(pipeset.out[READ_PIPE]);
            auto waitOp = state.isRootShell() && state.isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
            int ret = proc.wait(waitOp);   // wait exit
            state.updateExitStatus(ret);
            break;
        }
        case ForkKind::IN_PIPE:
        case ForkKind::OUT_PIPE: {
            auto entry = JobImpl::create(proc);
            state.jobTable.attach(entry, true);
            int &fd = forkKind == ForkKind::IN_PIPE ? pipeset.in[WRITE_PIPE] : pipeset.out[READ_PIPE];
            obj = newFD(state, fd);
            break;
        }
        case ForkKind::COPROC:
        case ForkKind::JOB:
        case ForkKind::DISOWN: {
            bool disown = forkKind == ForkKind::DISOWN;
            auto entry = JobImpl::create(proc);
            state.jobTable.attach(entry, disown);
            obj = DSValue::create<Job_Object>(
                    state.symbolTable.get(TYPE::Job),
                    entry,
                    newFD(state, pipeset.in[WRITE_PIPE]),
                    newFD(state, pipeset.out[READ_PIPE])
            );
            break;
        }
        }

        // push object
        if(obj) {
            state.push(std::move(obj));
        }

        state.pc() += offset - 1;
    } else if(proc.pid() == 0) {   // child process
        tryToDup(pipeset.in[READ_PIPE], STDIN_FILENO);
        tryToClose(pipeset.in);
        tryToDup(pipeset.out[WRITE_PIPE], STDOUT_FILENO);
        tryToClose(pipeset.out);

        if(forkKind == ForkKind::DISOWN || (forkKind == ForkKind::JOB && !state.isJobControl())) {
            redirInToNull();
        }

        state.pc() += 2;
    } else {
        raiseSystemError(state, EAGAIN, "fork failed");
        return false;
    }
    return true;
}


/* for pipeline evaluation */

/**
 * if filePath is null, not execute and set ENOENT.
 * argv is not null.
 * envp may be null.
 * if success, not return.
 */
static void xexecve(const char *filePath, char **argv, char *const *envp) {
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

class PipelineState : public DSObject {
private:
    DSState &state;
    Job entry;

public:
    NON_COPYABLE(PipelineState);

    PipelineState(DSState &state, Job &&entry) :
            DSObject(nullptr), state(state), entry(std::move(entry)) {}

    ~PipelineState() override {
        auto waitOp = state.isRootShell() && state.isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
        bool r = this->entry->restoreStdin();
        this->entry->wait(waitOp);
        if(r) {
            tryToForeground(this->state);
        }
        if(this->entry->available()) {
            // job is still running, attach to JobTable
            this->state.jobTable.attach(this->entry);
        }
        this->state.jobTable.updateStatus();
    }
};

static unsigned int getChangedFD(RedirOP op) {
    switch(op) {
#define GEN_CASE(ENUM, BITS) case RedirOP::ENUM: return BITS;
    EACH_RedirOP(GEN_CASE)
#undef GEN_CASE
    case RedirOP::NOP:
        return 0;
    }
    return 0;  // normally unreachable. due to suppress gcc warning
}

class RedirConfig : public DSObject {
private:
    unsigned int backupFDset{0};   // if corresponding bit is set, backup old fd

    std::vector<std::pair<RedirOP, DSValue>> ops;

    int oldFds[3];

public:
    NON_COPYABLE(RedirConfig);

    RedirConfig() : DSObject(nullptr), oldFds{-1, -1, -1} {}

    ~RedirConfig() override {
        this->restoreFDs();
        for(int fd : this->oldFds) {
            close(fd);
        }
    }

    void addRedirOp(RedirOP op, DSValue &&arg) {
        this->ops.emplace_back(op, std::move(arg));
        this->backupFDset |= getChangedFD(op);
    }

    void ignoreBackup() {
        this->backupFDset = 0;
    }

    bool redirect(DSState &st);

private:
    void backupFDs() {
        for(unsigned int i = 0; i < 3; i++) {
            if(this->backupFDset & (1 << i)) {
                this->oldFds[i] = dup(i);
            }
        }
    }

    void restoreFDs() {
        for(unsigned int i = 0; i < 3; i++) {
            if(this->backupFDset & (1 << i)) {
                dup2(this->oldFds[i], i);
            }
        }
    }
};

static int doIOHere(const String_Object &value) {
    pipe_t pipe[1];
    initPipe(1, pipe);

    dup2(pipe[0][READ_PIPE], STDIN_FILENO);

    if(value.size() + 1 <= PIPE_BUF) {
        int errnum = 0;
        if(write(pipe[0][WRITE_PIPE], value.getValue(), sizeof(char) * value.size()) < 0) {
            errnum = errno;
        }
        if(errnum == 0 && write(pipe[0][WRITE_PIPE], "\n", 1) < 0) {
            errnum = errno;
        }
        closeAllPipe(1, pipe);
        return errnum;
    } else {
        pid_t pid = fork();
        if(pid < 0) {
            return errno;
        }
        if(pid == 0) {   // child
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
        return 0;
    }
}

/**
 * if failed, return non-zero value(errno)
 */
static int redirectToFile(const SymbolTable &symbolTable, const DSValue &fileName, const char *mode, int targetFD) {
    auto &type = *fileName->getType();
    if(type == symbolTable.get(TYPE::String)) {
        FILE *fp = fopen(typeAs<String_Object>(fileName)->getValue(), mode);
        if(fp == nullptr) {
            return errno;
        }
        int fd = fileno(fp);
        if(dup2(fd, targetFD) < 0) {
            int e = errno;
            fclose(fp);
            return e;
        }
        fclose(fp);
    } else {
        assert(type == symbolTable.get(TYPE::UnixFD));
        int fd = typeAs<UnixFD_Object>(fileName)->getValue();
        if(strchr(mode, 'a') != nullptr) {
            if(lseek(fd, 0, SEEK_END) == -1) {
                return errno;
            }
        }
        if(dup2(fd, targetFD) < 0) {
            return errno;
        }
    }
    return 0;
}

static int redirectImpl(const SymbolTable &symbolTable, const std::pair<RedirOP, DSValue> &pair) {
    switch(pair.first) {
    case RedirOP::IN_2_FILE: {
        return redirectToFile(symbolTable, pair.second, "rb", STDIN_FILENO);
    }
    case RedirOP::OUT_2_FILE: {
        return redirectToFile(symbolTable, pair.second, "wb", STDOUT_FILENO);
    }
    case RedirOP::OUT_2_FILE_APPEND: {
        return redirectToFile(symbolTable, pair.second, "ab", STDOUT_FILENO);
    }
    case RedirOP::ERR_2_FILE: {
        return redirectToFile(symbolTable, pair.second, "wb", STDERR_FILENO);
    }
    case RedirOP::ERR_2_FILE_APPEND: {
        return redirectToFile(symbolTable, pair.second, "ab", STDERR_FILENO);
    }
    case RedirOP::MERGE_ERR_2_OUT_2_FILE: {
        int r = redirectToFile(symbolTable, pair.second, "wb", STDOUT_FILENO);
        if(r != 0) {
            return r;
        }
        if(dup2(STDOUT_FILENO, STDERR_FILENO) < 0) { return errno; }
        return 0;
    }
    case RedirOP::MERGE_ERR_2_OUT_2_FILE_APPEND: {
        int r = redirectToFile(symbolTable, pair.second, "ab", STDOUT_FILENO);
        if(r != 0) {
            return r;
        }
        if(dup2(STDOUT_FILENO, STDERR_FILENO) < 0) { return errno; }
        return 0;
    }
    case RedirOP::MERGE_ERR_2_OUT:
        if(dup2(STDOUT_FILENO, STDERR_FILENO) < 0) { return errno; }
        return 0;
    case RedirOP::MERGE_OUT_2_ERR:
        if(dup2(STDERR_FILENO, STDOUT_FILENO) < 0) { return errno; }
        return 0;
    case RedirOP::HERE_STR:
        return doIOHere(*typeAs<String_Object>(pair.second));
    case RedirOP::NOP:
        return 0;   // do nothing
    }
    return 0;   // normally unreachable, but gcc requires this return statement.
}

bool RedirConfig::redirect(DSState &st) {
    this->backupFDs();
    for(auto &pair : this->ops) {
        int r = redirectImpl(st.symbolTable, pair);
        if(this->backupFDset > 0 && r != 0) {
            std::string msg = REDIR_ERROR;
            if(pair.second) {
                auto *type = pair.second->getType();
                if(*type == st.symbolTable.get(TYPE::String)) {
                    if(!typeAs<String_Object>(pair.second)->empty()) {
                        msg += ": ";
                        msg += typeAs<String_Object>(pair.second)->getValue();
                    }
                } else if(*type == st.symbolTable.get(TYPE::UnixFD)) {
                    msg += ": ";
                    msg += std::to_string(typeAs<UnixFD_Object>(pair.second)->getValue());
                }
            }
            raiseSystemError(st, r, std::move(msg));
            return false;
        }
    }
    return true;
}

static constexpr flag8_t UDC_ATTR_SETVAR    = 1 << 0;
static constexpr flag8_t UDC_ATTR_NEED_FORK = 1 << 1;

/**
 * stack state in function apply    stack grow ===>
 *
 * +-----------+---------------+--------------+
 * | stack top | param1(redir) | param2(argv) |
 * +-----------+---------------+--------------+
 *             |     offset    |
 */
static bool callUserDefinedCommand(DSState &st, const DSCode *code,
                                   DSValue &&argvObj, DSValue &&restoreFD, const flag8_set_t attr) {
    if(hasFlag(attr, UDC_ATTR_SETVAR)) {
        // reset exit status
        st.updateExitStatus(0);
    }

    // set stack stack
    if(!windStackFrame(st, 0, 0, code)) {
        return false;
    }

    // set parameter
    st.setLocal(UDC_PARAM_ATTR, DSValue::createNum(attr));
    st.setLocal(UDC_PARAM_REDIR, std::move(restoreFD));
    st.setLocal(UDC_PARAM_ARGV, std::move(argvObj));

    if(hasFlag(attr, UDC_ATTR_SETVAR)) {    // set variable
        auto argv = typeAs<Array_Object>(st.getLocal(UDC_PARAM_ARGV));
        eraseFirst(*argv);
        const unsigned int argSize = argv->getValues().size();
        st.setLocal(UDC_PARAM_ARGV + 1, DSValue::create<Int_Object>(st.symbolTable.get(TYPE::Int32), argSize));   // #
        st.setLocal(UDC_PARAM_ARGV + 2, st.getGlobal(toIndex(BuiltinVarOffset::POS_0))); // 0
        unsigned int limit = 9;
        if(argSize < limit) {
            limit = argSize;
        }

        unsigned int index = 0;
        for(; index < limit; index++) {
            st.setLocal(index + UDC_PARAM_ARGV + 3, argv->getValues()[index]);
        }

        for(; index < 9; index++) {
            st.setLocal(index + UDC_PARAM_ARGV + 3, st.emptyStrObj);  // set remain
        }
    }
    return true;
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

public:
    static constexpr flag8_t MASK_UDC      = 1 << 0;
    static constexpr flag8_t MASK_EXTERNAL = 1 << 1;

    CmdResolver(flag8_set_t mask, flag8_set_t op) : mask(mask), searchOp(op) {}
    CmdResolver() : CmdResolver(0, 0) {}
    ~CmdResolver() = default;

    Command operator()(DSState &state, const char *cmdName) const;
};

static NativeCode initCode(OpCode op) {
    auto *code = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * 3));
    code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
    code[1] = static_cast<unsigned char>(op);
    code[2] = static_cast<unsigned char>(OpCode::RETURN_V);
    return NativeCode(code);
}

static NativeCode initExit() {
    auto *code = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * 4));
    code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
    code[1] = static_cast<unsigned char>(OpCode::LOAD_LOCAL);
    code[2] = UDC_PARAM_ARGV;
    code[3] = static_cast<unsigned char>(OpCode::EXIT_SHELL);
    return NativeCode(code);
}

static const DSCode *lookupUserDefinedCommand(const DSState &st, const char *commandName) {
    auto handle = st.symbolTable.lookupUdc(commandName);
    return handle == nullptr ? nullptr : &typeAs<FuncObject>(st.getGlobal(handle->getIndex()))->getCode();
}

Command CmdResolver::operator()(DSState &state, const char *cmdName) const {
    Command cmd{};

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

        static std::pair<const char *, NativeCode> sb[] = {
                {"command", initCode(OpCode::BUILTIN_CMD)},
                {"eval", initCode(OpCode::BUILTIN_EVAL)},
                {"exec", initCode(OpCode::BUILTIN_EXEC)},
                {"exit", initExit()},
        };
        for(auto &e : sb) {
            if(strcmp(cmdName, e.first) == 0) {
                cmd.kind = CmdKind::BUILTIN_S;
                cmd.udc = &e.second;
                return cmd;
            }
        }
    }

    // resolve external command path
    cmd.kind = CmdKind::EXTERNAL;
    cmd.filePath = hasFlag(this->mask, MASK_EXTERNAL) ? nullptr : state.pathCache.searchPath(cmdName, this->searchOp);
    return cmd;
}

static void raiseCmdError(DSState &state, const char *cmdName, int errnum) {
    std::string str = EXEC_ERROR;
    str += cmdName;
    if(errnum == ENOENT) {
        str += ": command not found";
        raiseError(state, state.symbolTable.get(TYPE::SystemError), std::move(str));
    } else {
        raiseSystemError(state, errnum, std::move(str));
    }
}

static int forkAndExec(DSState &state, const char *cmdName, Command cmd, char **const argv, DSValue &&redirConfig) {
    // setup self pipe
    int selfpipe[2];
    if(pipe(selfpipe) < 0) {
        perror("pipe creation error");
        exit(1);
    }
    if(fcntl(selfpipe[WRITE_PIPE], F_SETFD, fcntl(selfpipe[WRITE_PIPE], F_GETFD) | FD_CLOEXEC) != 0) {
        perror("fcntl error");
        exit(1);
    }

    bool rootShell = state.isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    auto proc = Proc::fork(state, pgid, rootShell);
    if(proc.pid() == -1) {
        raiseCmdError(state, cmdName, EAGAIN);
        return 1;
    } else if(proc.pid() == 0) {   // child
        xexecve(cmd.filePath, argv, nullptr);

        int errnum = errno;
        int r = write(selfpipe[WRITE_PIPE], &errnum, sizeof(int));
        (void) r;   //FIXME:
        exit(-1);
    } else {    // parent process
        close(selfpipe[WRITE_PIPE]);
        redirConfig = nullptr;  // restore redirconfig

        int readSize;
        int errnum = 0;
        while((readSize = read(selfpipe[READ_PIPE], &errnum, sizeof(int))) == -1) {
            if(errno != EAGAIN && errno != EINTR) {
                break;
            }
        }
        close(selfpipe[READ_PIPE]);
        if(readSize > 0 && errnum == ENOENT) {  // remove cached path
            state.pathCache.removePath(argv[0]);
        }

        // wait process or job termination
        int status;
        auto waitOp = rootShell && state.isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
        status = proc.wait(waitOp);
        if(proc.state() != Proc::TERMINATED) {
            state.jobTable.attach(JobImpl::create(proc));
        }
        tryToForeground(state);
        state.jobTable.updateStatus();
        if(errnum != 0) {
            raiseCmdError(state, cmdName, errnum);
            return 1;
        }
        return status;
    }
}

static bool callCommand(DSState &state, Command cmd, DSValue &&argvObj, DSValue &&redirConfig, flag8_set_t attr = 0) {
    auto *array = typeAs<Array_Object>(argvObj);
    const unsigned int size = array->getValues().size();
    auto &first = array->getValues()[0];
    const char *cmdName = typeAs<String_Object>(first)->getValue();

    switch(cmd.kind) {
    case CmdKind::USER_DEFINED:
    case CmdKind::BUILTIN_S: {
        if(cmd.kind == CmdKind::USER_DEFINED) {
            setFlag(attr, UDC_ATTR_SETVAR);
        }
        return callUserDefinedCommand(state, cmd.udc, std::move(argvObj), std::move(redirConfig), attr);
    }
    case CmdKind::BUILTIN: {
        int status = cmd.builtinCmd(state, *array);
        state.pushExitStatus(status);
        flushStdFD();
        return true;
    }
    case CmdKind::EXTERNAL: {
        // create argv
        char *argv[size + 1];
        for(unsigned int i = 0; i < size; i++) {
            argv[i] = const_cast<char *>(str(array->getValues()[i]));
        }
        argv[size] = nullptr;

        if(hasFlag(attr, UDC_ATTR_NEED_FORK)) {
            int status = forkAndExec(state, cmdName, cmd, argv, std::move(redirConfig));
            state.pushExitStatus(status);
        } else {
            xexecve(cmd.filePath, argv, nullptr);
            raiseCmdError(state, cmdName, errno);
        }
        return !state.getThrownObject();
    }
    }
    return true;    // normally unreachable, but need to suppress gcc warning.
}

int invalidOptionError(const Array_Object &obj, const GetOptState &s);

static bool callBuiltinCommand(DSState &state, DSValue &&argvObj, DSValue &&redir, flag8_set_t attr) {
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
            state.pushExitStatus(s);
            return true;
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
            return callCommand(state, resolve(state, cmdName), std::move(argvObj), std::move(redir), attr);
        }

        // show command description
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
                    successCount++;
                    if(showDesc == 1) {
                        printf("%s\n", path);
                    } else if(state.pathCache.isCached(commandName)) {
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
        state.pushExitStatus(successCount > 0 ? 0 : 1);
        return true;
    }
    state.pushExitStatus(0);
    return true;
}

static void callBuiltinExec(DSState &state, DSValue &&array, DSValue &&redir) {
    auto &argvObj = *typeAs<Array_Object>(array);
    bool clearEnv = false;
    const char *progName = nullptr;
    GetOptState optState;

    if(redir) {
        typeAs<RedirConfig>(redir)->ignoreBackup();
    }

    for(int opt; (opt = optState(argvObj, "ca:")) != -1;) {
        switch(opt) {
        case 'c':
            clearEnv = true;
            break;
        case 'a':
            progName = optState.optArg;
            break;
        default:
            int s = invalidOptionError(argvObj, optState);
            state.pushExitStatus(s);
            return;
        }
    }

    unsigned int index = optState.index;
    const unsigned int argc = argvObj.getValues().size();
    if(index < argc) { // exec
        char *argv2[argc - index + 1];
        for(unsigned int i = index; i < argc; i++) {
            argv2[i - index] = const_cast<char *>(str(argvObj.getValues()[i]));
        }
        argv2[argc - index] = nullptr;

        const char *filePath = getPathCache(state).searchPath(argv2[0], FilePathCache::DIRECT_SEARCH);
        if(progName != nullptr) {
            argv2[0] = const_cast<char *>(progName);
        }

        char *envp[] = {nullptr};
        xexecve(filePath, argv2, clearEnv ? envp : nullptr);
        PERROR(argvObj, "%s", str(argvObj.getValues()[index]));
        exit(1);
    }
    state.pushExitStatus(0);
}

/**
 *
 * @param state
 * @param lastPipe
 * if true, evaluate last pipe in parent shell
 * @return
 * if has error, return false.
 */
static bool callPipeline(DSState &state, bool lastPipe) {
    /**
     * ls | grep .
     * ==> pipeSize == 1, procSize == 2
     *
     * if lastPipe is true,
     *
     * ls | { grep . ;}
     * ==> pipeSize == 1, procSize == 1
     */
    const unsigned int procSize = read8(GET_CODE(state), state.pc() + 1) - 1;
    const unsigned int pipeSize = procSize - (lastPipe ? 0 : 1);

    assert(pipeSize > 0);

    int pipefds[pipeSize][2];
    initPipe(pipeSize, pipefds);

    // fork
    Proc childs[procSize];
    const bool rootShell = state.isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    Proc proc;

    unsigned int procIndex;
    for(procIndex = 0; procIndex < procSize && (proc = Proc::fork(state, pgid, rootShell)).pid() > 0; procIndex++) {
        childs[procIndex] = proc;
        if(pgid == 0) {
            pgid = proc.pid();
        }
    }

    /**
     * code layout
     *
     * +----------+-------------+------------------+    +--------------------+
     * | PIPELINE | size: 1byte | br1(child): 2yte | ~  | brN(parent): 2byte |
     * +----------+-------------+------------------+    +--------------------+
     */
    if(proc.pid() == 0) {   // child
        if(procIndex == 0) {    // first process
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex > 0 && procIndex < pipeSize) {   // other process.
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == pipeSize && !lastPipe) {    // last process
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
        }
        closeAllPipe(pipeSize, pipefds);

        // set pc to next instruction
        state.pc() += read16(GET_CODE(state), state.pc() + 2 + procIndex * 2) - 1;
    } else if(procIndex == procSize) { // parent (last pipeline)
        if(lastPipe) {
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
        }
        closeAllPipe(pipeSize, pipefds);

        auto jobEntry = JobImpl::create(procSize, childs, lastPipe);
        if(lastPipe) {
            state.push(DSValue::create<PipelineState>(state, std::move(jobEntry)));
        } else {
            // job termination
            auto waitOp = rootShell && state.isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
            int status = jobEntry->wait(waitOp);
            if(jobEntry->available()) {
                state.jobTable.attach(jobEntry);
            }
            tryToForeground(state);
            state.jobTable.updateStatus();
            state.pushExitStatus(status);
        }

        // set pc to next instruction
        state.pc() += read16(GET_CODE(state), state.pc() + 2 + procIndex * 2) - 1;
    } else {
        raiseSystemError(state, EAGAIN, "fork failed");
        return false;
    }
    return true;
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

    auto *argv = typeAs<Array_Object>(state.callStack[state.stackTopIndex - 1]);
    if(*valueType == state.symbolTable.get(TYPE::String)) {  // String
        if(skipEmptyStr && typeAs<String_Object>(value)->empty()) {
            return;
        }
        argv->append(std::move(value));
        return;
    }

    if(*valueType == state.symbolTable.get(TYPE::UnixFD)) { // UnixFD
        if(!state.peek()) {
            state.pop();
            state.push(DSValue::create<RedirConfig>());
        }
        auto fdPath = typeAs<UnixFD_Object>(value)->toString(state, nullptr);
        auto strObj = DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), std::move(fdPath));
        typeAs<RedirConfig>(state.peek())->addRedirOp(RedirOP::NOP, std::move(value));
        argv->append(std::move(strObj));
        return;
    }

    assert(*valueType == state.symbolTable.get(TYPE::StringArray));  // Array<String>
    auto *arrayObj = typeAs<Array_Object>(value);
    for(auto &element : arrayObj->getValues()) {
        if(typeAs<String_Object>(element)->empty()) {
            continue;
        }
        argv->append(element);
    }
}

static NativeCode initSignalTrampoline() noexcept {
    auto *code = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * 9));
    code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
    code[1] = static_cast<unsigned char>(OpCode::LOAD_LOCAL);
    code[2] = static_cast<unsigned char>(1);
    code[3] = static_cast<unsigned char>(OpCode::LOAD_LOCAL);
    code[4] = static_cast<unsigned char>(2);
    code[5] = static_cast<unsigned char>(OpCode::CALL_FUNC);
    code[6] = code[7] = 0;
    write16(code + 6, 1);
    code[8] = static_cast<unsigned char>(OpCode::RETURN_SIG);
    return NativeCode(code);
}

static auto signalTrampoline = initSignalTrampoline();

static bool kickSignalHandler(DSState &st, int sigNum, DSValue &&func) {
    st.reserveLocalStack(3);
    st.push(st.getGlobal(toIndex(BuiltinVarOffset::EXIT_STATUS)));
    st.push(std::move(func));
    st.push(DSValue::create<Int_Object>(st.symbolTable.get(TYPE::Signal), sigNum));

    return windStackFrame(st, 3, 3, &signalTrampoline);
}

static int popPendingSig() {
    assert(!DSState::pendingSigSet.empty());
    int sigNum;
    do {
        sigNum = DSState::pendingSigIndex++;
        if(DSState::pendingSigIndex == NSIG) {
            DSState::pendingSigIndex = 1;
        }
    } while(!DSState::pendingSigSet.has(sigNum));
    DSState::pendingSigSet.del(sigNum);
    return sigNum;
}

static bool checkVMEvent(DSState &state) {
    if(hasFlag(DSState::eventDesc, DSState::VM_EVENT_SIGNAL) &&
            !hasFlag(DSState::eventDesc, DSState::VM_EVENT_MASK)) {
        SignalGuard guard;

        int sigNum = popPendingSig();
        if(DSState::pendingSigSet.empty()) {
            DSState::pendingSigIndex = 1;
            unsetFlag(DSState::eventDesc, DSState::VM_EVENT_SIGNAL);
        }

        auto handler = state.sigVector.lookup(sigNum);
        if(handler != nullptr) {
            setFlag(DSState::eventDesc, DSState::VM_EVENT_MASK);
            if(!kickSignalHandler(state, sigNum, std::move(handler))) {
                return false;
            }
        }
    }

    if(state.hook != nullptr) {
        assert(hasFlag(DSState::eventDesc, DSState::VM_EVENT_HOOK));
        auto op = static_cast<OpCode>(GET_CODE(state)[state.pc() + 1]);
        state.hook->vmFetchHook(state, op);
    }
    return true;
}


#define vmdispatch(V) switch(V)

#if 0
#define vmcase(code) case OpCode::code: {fprintf(stderr, "pc: %u, code: %s\n", state.pc(), #code); }
#else
#define vmcase(code) case OpCode::code:
#endif

#define vmnext continue
#define vmerror return false

#define TRY(E) do { if(!(E)) { vmerror; } } while(false)

static bool mainLoop(DSState &state) {
    while(true) {
        if(DSState::eventDesc != 0u) {
            TRY(checkVMEvent(state));
        }

        // fetch next opcode
        const auto op = static_cast<OpCode>(GET_CODE(state)[++state.pc()]);

        // dispatch instruction
        vmdispatch(op) {
        vmcase(HALT) {
            unwindStackFrame(state);
            return true;
        }
        vmcase(ASSERT) {
            TRY(checkAssertion(state));
            vmnext;
        }
        vmcase(PRINT) {
            unsigned int v = read32(GET_CODE(state), state.pc() + 1);
            state.pc() += 4;

            auto &stackTopType = state.symbolTable.get(v);
            assert(!stackTopType.isVoidType());
            auto *strObj = typeAs<String_Object>(state.peek());
            printf("(%s) ", state.symbolTable.getTypeName(stackTopType));
            fwrite(strObj->getValue(), sizeof(char), strObj->size(), stdout);
            fputc('\n', stdout);
            fflush(stdout);
            state.popNoReturn();
            vmnext;
        }
        vmcase(INSTANCE_OF) {
            unsigned int v = read32(GET_CODE(state), state.pc() + 1);
            state.pc() += 4;

            auto &targetType = state.symbolTable.get(v);
            if(state.pop()->introspect(state, &targetType)) {
                state.push(state.trueObj);
            } else {
                state.push(state.falseObj);
            }
            vmnext;
        }
        vmcase(CHECK_CAST) {
            unsigned int v = read32(GET_CODE(state), state.pc() + 1);
            state.pc() += 4;
            TRY(checkCast(state, &state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(PUSH_NULL) {
            state.push(nullptr);
            vmnext;
        }
        vmcase(PUSH_TRUE) {
            state.push(state.trueObj);
            vmnext;
        }
        vmcase(PUSH_FALSE) {
            state.push(state.falseObj);
            vmnext;
        }
        vmcase(PUSH_ESTRING) {
            state.push(state.emptyStrObj);
            vmnext;
        }
        vmcase(LOAD_CONST) {
            unsigned char index = read8(GET_CODE(state), ++state.pc());
            state.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_CONST_W) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_CONST_T) {
            unsigned int index = read24(GET_CODE(state), state.pc() + 1);
            state.pc() += 3;
            state.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_GLOBAL) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.loadGlobal(index);
            vmnext;
        }
        vmcase(STORE_GLOBAL) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.storeGlobal(index);
            vmnext;
        }
        vmcase(LOAD_LOCAL) {
            unsigned char index = read8(GET_CODE(state), ++state.pc());
            state.loadLocal(index);
            vmnext;
        }
        vmcase(STORE_LOCAL) {
            unsigned char index = read8(GET_CODE(state), ++state.pc());
            state.storeLocal(index);
            vmnext;
        }
        vmcase(LOAD_FIELD) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.loadField(index);
            vmnext;
        }
        vmcase(STORE_FIELD) {
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            state.storeField(index);
            vmnext;
        }
        vmcase(IMPORT_ENV) {
            unsigned char b = read8(GET_CODE(state), ++state.pc());
            TRY(loadEnv(state, b > 0));
            vmnext;
        }
        vmcase(LOAD_ENV) {
            const char *value = loadEnv(state, false);
            TRY(value);
            state.push(DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), value));
            vmnext;
        }
        vmcase(STORE_ENV) {
            DSValue value = state.pop();
            DSValue name = state.pop();

            setenv(typeAs<String_Object>(name)->getValue(),
                   typeAs<String_Object>(value)->getValue(), 1);//FIXME: check return value and throw
            vmnext;
        }
        vmcase(POP) {
            state.popNoReturn();
            vmnext;
        }
        vmcase(DUP) {
            state.dup();
            vmnext;
        }
        vmcase(DUP2) {
            state.dup2();
            vmnext;
        }
        vmcase(SWAP) {
            state.swap();
            vmnext;
        }
        vmcase(NEW_STRING) {
            state.push(DSValue::create<String_Object>(state.symbolTable.get(TYPE::String)));
            vmnext;
        }
        vmcase(APPEND_STRING) {
            DSValue v = state.pop();
            typeAs<String_Object>(state.peek())->append(std::move(v));
            vmnext;
        }
        vmcase(NEW_ARRAY) {
            unsigned int v = read32(GET_CODE(state), state.pc() + 1);
            state.pc() += 4;
            state.push(DSValue::create<Array_Object>(state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(APPEND_ARRAY) {
            DSValue v = state.pop();
            typeAs<Array_Object>(state.peek())->append(std::move(v));
            vmnext;
        }
        vmcase(NEW_MAP) {
            unsigned int v = read32(GET_CODE(state), state.pc() + 1);
            state.pc() += 4;
            state.push(DSValue::create<Map_Object>(state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(APPEND_MAP) {
            DSValue value = state.pop();
            DSValue key = state.pop();
            typeAs<Map_Object>(state.peek())->add(std::make_pair(std::move(key), std::move(value)));
            vmnext;
        }
        vmcase(NEW_TUPLE) {
            unsigned int v = read32(GET_CODE(state), state.pc() + 1);
            state.pc() += 4;
            state.push(DSValue::create<Tuple_Object>(state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(NEW) {
            unsigned int v = read32(GET_CODE(state), state.pc() + 1);
            state.pc() += 4;

            auto &type = state.symbolTable.get(v);
            if(!type.isRecordType()) {
                state.push(DSValue::create<DSObject>(type));
            } else {
                fatal("currently, DSObject allocation not supported\n");
            }
            vmnext;
        }
        vmcase(CALL_INIT) {
            unsigned short paramSize = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            TRY(callConstructor(state, paramSize));
            vmnext;
        }
        vmcase(CALL_METHOD) {
            unsigned short paramSize = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            unsigned short index = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            TRY(callMethod(state, index, paramSize));
            vmnext;
        }
        vmcase(CALL_FUNC) {
            unsigned short paramSize = read16(GET_CODE(state), state.pc() + 1);
            state.pc() += 2;
            TRY(applyFuncObject(state, paramSize));
            vmnext;
        }
        vmcase(CALL_NATIVE) {
            unsigned long v = read64(GET_CODE(state), state.pc() + 1);
            state.pc() += 8;
            auto func = (native_func_t) v;
            DSValue returnValue = func(state);
            TRY(!state.getThrownObject());
            if(returnValue) {
                state.push(std::move(returnValue));
            }
            vmnext;
        }
        vmcase(INIT_MODULE) {
            auto &code = typeAs<FuncObject>(state.peek())->getCode();
            windStackFrame(state, 0, 0, &code);
            vmnext;
        }
        vmcase(RETURN) {
            unwindStackFrame(state);
            if(state.controlStack.empty()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_V) {
            DSValue v = state.pop();
            unwindStackFrame(state);
            state.push(std::move(v));
            if(state.controlStack.empty()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_UDC) {
            auto v = state.pop();
            unwindStackFrame(state);
            state.pushExitStatus(typeAs<Int_Object>(v)->getValue());
            if(state.controlStack.empty()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_SIG) {
            auto v = state.getLocal(0);   // old exit status
            unwindStackFrame(state);
            unsetFlag(DSState::eventDesc, DSState::VM_EVENT_MASK);
            state.setGlobal(toIndex(BuiltinVarOffset::EXIT_STATUS), std::move(v));
            vmnext;
        }
        vmcase(BRANCH) {
            unsigned short offset = read16(GET_CODE(state), state.pc() + 1);
            if(typeAs<Boolean_Object>(state.pop())->getValue()) {
                state.pc() += 2;
            } else {
                state.pc() += offset - 1;
            }
            vmnext;
        }
        vmcase(GOTO) {
            unsigned int index = read32(GET_CODE(state), state.pc() + 1);
            state.pc() = index - 1;
            vmnext;
        }
        vmcase(THROW) {
            state.storeThrowObject();
            vmerror;
        }
        vmcase(EXIT_SHELL) {
            auto obj = state.pop();
            auto &type = *obj->getType();

            int ret = typeAs<Int_Object>(state.getGlobal(toIndex(BuiltinVarOffset::EXIT_STATUS)))->getValue();
            if(type == state.symbolTable.get(TYPE::Int32)) { // normally Int Object
                ret = typeAs<Int_Object>(obj)->getValue();
            } else if(type == state.symbolTable.get(TYPE::StringArray)) {    // for builtin exit command
                auto *arrayObj = typeAs<Array_Object>(obj);
                if(arrayObj->getValues().size() > 1) {
                    const char *num = str(arrayObj->getValues()[1]);
                    int status;
                    long value = convertToInt64(num, status);
                    if(status == 0) {
                        ret = value;
                    }
                }
            }
            exitShell(state, ret);
            vmerror;
        }
        vmcase(ENTER_FINALLY) {
            const unsigned int offset = read16(GET_CODE(state), state.pc() + 1);
            const unsigned int savedIndex = state.pc() + 2;
            state.push(DSValue::createNum(savedIndex));
            state.pc() += offset - 1;
            vmnext;
        }
        vmcase(EXIT_FINALLY) {
            switch(state.peek().kind()) {
            case DSValueKind::OBJECT:
            case DSValueKind::INVALID: {
                state.storeThrowObject();
                vmerror;
            }
            case DSValueKind::NUMBER: {
                unsigned int index = static_cast<unsigned int>(state.pop().value());
                state.pc() = index;
                vmnext;
            }
            }
            vmnext;
        }
        vmcase(COPY_INT) {
            DSType *type = state.symbolTable.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            int v = typeAs<Int_Object>(state.pop())->getValue();
            state.push(DSValue::create<Int_Object>(*type, v));
            vmnext;
        }
        vmcase(TO_BYTE) {
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            v &= 0xFF;  // fill higher bits (8th ~ 31) with 0
            state.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Byte), v));
            vmnext;
        }
        vmcase(TO_U16) {
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            state.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Uint16), v));
            vmnext;
        }
        vmcase(TO_I16) {
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            if((v & 0x8000) != 0u) {    // if 15th bit is 1, fill higher bits with 1
                v |= 0xFFFF0000;
            }
            state.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int16), v));
            vmnext;
        }
        vmcase(NEW_LONG) {
            DSType *type = state.symbolTable.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            unsigned long l = v;
            state.push(DSValue::create<Long_Object>(*type, l));
            vmnext;
        }
        vmcase(COPY_LONG) {
            DSType *type = state.symbolTable.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            long v = typeAs<Long_Object>(state.pop())->getValue();
            state.push(DSValue::create<Long_Object>(*type, v));
            vmnext;
        }
        vmcase(I_NEW_LONG) {
            DSType *type = state.symbolTable.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            int v = typeAs<Int_Object>(state.pop())->getValue();
            long l = v;
            state.push(DSValue::create<Long_Object>(*type, l));
            vmnext;
        }
        vmcase(NEW_INT) {
            DSType *type = state.symbolTable.getByNumTypeIndex(read8(GET_CODE(state), ++state.pc()));
            unsigned long l = typeAs<Long_Object>(state.pop())->getValue();
            auto v = static_cast<unsigned int>(l);
            state.push(DSValue::create<Int_Object>(*type, v));
            vmnext;
        }
        vmcase(U32_TO_D) {
            unsigned int v = typeAs<Int_Object>(state.pop())->getValue();
            auto d = static_cast<double>(v);
            state.push(DSValue::create<Float_Object>(state.symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(I32_TO_D) {
            int v = typeAs<Int_Object>(state.pop())->getValue();
            auto d = static_cast<double>(v);
            state.push(DSValue::create<Float_Object>(state.symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(U64_TO_D) {
            unsigned long v = typeAs<Long_Object>(state.pop())->getValue();
            auto d = static_cast<double>(v);
            state.push(DSValue::create<Float_Object>(state.symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(I64_TO_D) {
            long v = typeAs<Long_Object>(state.pop())->getValue();
            auto d = static_cast<double>(v);
            state.push(DSValue::create<Float_Object>(state.symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(D_TO_U32) {
            double d = typeAs<Float_Object>(state.pop())->getValue();
            auto v = static_cast<unsigned int>(d);
            state.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Uint32), v));
            vmnext;
        }
        vmcase(D_TO_I32) {
            double d = typeAs<Float_Object>(state.pop())->getValue();
            auto v = static_cast<int>(d);
            state.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), v));
            vmnext;
        }
        vmcase(D_TO_U64) {
            double d = typeAs<Float_Object>(state.pop())->getValue();
            auto v = static_cast<unsigned long>(d);
            state.push(DSValue::create<Long_Object>(state.symbolTable.get(TYPE::Uint64), v));
            vmnext;
        }
        vmcase(D_TO_I64) {
            double d = typeAs<Float_Object>(state.pop())->getValue();
            auto v = static_cast<long>(d);
            state.push(DSValue::create<Long_Object>(state.symbolTable.get(TYPE::Int64), v));
            vmnext;
        }
        vmcase(REF_EQ) {
            auto v1 = state.pop();
            auto v2 = state.pop();
            state.push(v1 == v2 ? state.trueObj : state.falseObj);
            vmnext;
        }
        vmcase(REF_NE) {
            auto v1 = state.pop();
            auto v2 = state.pop();
            state.push(v1 != v2 ? state.trueObj : state.falseObj);
            vmnext;
        }
        vmcase(FORK) {
            TRY(forkAndEval(state));
            vmnext;
        }
        vmcase(PIPELINE)
        vmcase(PIPELINE_LP) {
            bool lastPipe = op == OpCode::PIPELINE_LP;
            TRY(callPipeline(state, lastPipe));
            vmnext;
        }
        vmcase(EXPAND_TILDE) {
            std::string str = typeAs<String_Object>(state.pop())->getValue();
            expandTilde(str);
            state.push(DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), std::move(str)));
            vmnext;
        }
        vmcase(NEW_CMD) {
            auto v = state.pop();
            auto obj = DSValue::create<Array_Object>(state.symbolTable.get(TYPE::StringArray));
            auto *argv = typeAs<Array_Object>(obj);
            argv->append(std::move(v));
            state.push(std::move(obj));
            vmnext;
        }
        vmcase(ADD_CMD_ARG) {
            unsigned char v = read8(GET_CODE(state), ++state.pc());
            addCmdArg(state, v > 0);
            vmnext;
        }
        vmcase(CALL_CMD)
        vmcase(CALL_CMD_P) {
            bool needFork = op != OpCode::CALL_CMD_P;
            flag8_set_t attr = 0;
            if(needFork) {
                setFlag(attr, UDC_ATTR_NEED_FORK);
            }

            auto redir = state.pop();
            auto argv = state.pop();

            const char *cmdName = str(typeAs<Array_Object>(argv)->getValues()[0]);
            TRY(callCommand(state, CmdResolver()(state, cmdName), std::move(argv), std::move(redir), attr));
            vmnext;
        }
        vmcase(BUILTIN_CMD) {
            auto attr = state.getLocal(UDC_PARAM_ATTR).value();
            DSValue redir = state.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.getLocal(UDC_PARAM_ARGV);
            bool ret = callBuiltinCommand(state, std::move(argv), std::move(redir), attr);
            flushStdFD();
            TRY(ret);
            vmnext;
        }
        vmcase(BUILTIN_EVAL) {
            auto attr = state.getLocal(UDC_PARAM_ATTR).value();
            DSValue redir = state.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.getLocal(UDC_PARAM_ARGV);

            eraseFirst(*typeAs<Array_Object>(argv));
            auto *array = typeAs<Array_Object>(argv);
            if(!array->getValues().empty()) {
                const char *cmdName = str(typeAs<Array_Object>(argv)->getValues()[0]);
                TRY(callCommand(state, CmdResolver()(state, cmdName), std::move(argv), std::move(redir), attr));
            } else {
                state.pushExitStatus(0);
            }
            vmnext;
        }
        vmcase(BUILTIN_EXEC) {
            DSValue redir = state.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.getLocal(UDC_PARAM_ARGV);
            callBuiltinExec(state, std::move(argv), std::move(redir));
            vmnext;
        }
        vmcase(NEW_REDIR) {
            state.push(DSValue::create<RedirConfig>());
            vmnext;
        }
        vmcase(ADD_REDIR_OP) {
            unsigned char v = read8(GET_CODE(state), ++state.pc());
            auto value = state.pop();
            typeAs<RedirConfig>(state.peek())->addRedirOp(static_cast<RedirOP>(v), std::move(value));
            vmnext;
        }
        vmcase(DO_REDIR) {
            TRY(typeAs<RedirConfig>(state.peek())->redirect(state));
            vmnext;
        }
        vmcase(RAND) {
            std::random_device rand;
            std::default_random_engine engine(rand());
            std::uniform_int_distribution<unsigned int> dist;
            unsigned int v = dist(engine);
            state.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Uint32), v));
            vmnext;
        }
        vmcase(GET_SECOND) {
            auto now = std::chrono::system_clock::now();
            auto diff = now - state.baseTime;
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(diff);
            unsigned long v = typeAs<Long_Object>(state.getGlobal(toIndex(BuiltinVarOffset::SECONDS)))->getValue();
            v += sec.count();
            state.push(DSValue::create<Long_Object>(state.symbolTable.get(TYPE::Uint64), v));
            vmnext;
        }
        vmcase(SET_SECOND) {
            state.baseTime = std::chrono::system_clock::now();
            state.storeGlobal(toIndex(BuiltinVarOffset::SECONDS));
            vmnext;
        }
        vmcase(UNWRAP) {
            if(state.peek().kind() == DSValueKind::INVALID) {
                raiseError(state, state.symbolTable.get(TYPE::UnwrappingError), std::string("invalid value"));
                vmerror;
            }
            vmnext;
        }
        vmcase(CHECK_UNWRAP) {
            bool b = state.pop().kind() != DSValueKind::INVALID;
            state.push(b ? state.trueObj : state.falseObj);
            vmnext;
        }
        vmcase(TRY_UNWRAP) {
            unsigned short offset = read16(GET_CODE(state), state.pc() + 1);
            if(state.peek().kind() == DSValueKind::INVALID) {
                state.popNoReturn();
                state.pc() += 2;
            } else {
                state.pc() += offset - 1;
            }
            vmnext;
        }
        vmcase(NEW_INVALID) {
            state.push(DSValue::createInvalid());
            vmnext;
        }
        vmcase(RECLAIM_LOCAL) {
            unsigned char offset = read8(GET_CODE(state), ++state.pc());
            unsigned char size = read8(GET_CODE(state), ++state.pc());

            reclaimLocals(state, offset, size);
            vmnext;
        }
        }
    }
}

/**
 * if found exception handler, return true.
 * otherwise return false.
 */
static bool handleException(DSState &state, bool forceUnwind) {
    if(state.hook != nullptr) {
        state.hook->vmThrowHook(state);
    }

    for(; !state.controlStack.empty(); unwindStackFrame(state)) {
        if(!forceUnwind && !CODE(state)->is(CodeKind::NATIVE)) {
            auto *cc = static_cast<const CompiledCode *>(CODE(state));

            // search exception entry
            const unsigned int occurredPC = state.pc();
            const DSType *occurredType = state.getThrownObject()->getType();

            for(unsigned int i = 0; cc->getExceptionEntries()[i].type != nullptr; i++) {
                const ExceptionEntry &entry = cc->getExceptionEntries()[i];
                if(occurredPC >= entry.begin && occurredPC < entry.end
                   && entry.type->isSameOrBaseTypeOf(*occurredType)) {
                    if(*entry.type == state.symbolTable.get(TYPE::_Root)) {
                        return false;
                    }
                    state.pc() = entry.dest - 1;
                    clearOperandStack(state);
                    reclaimLocals(state, entry.localOffset, entry.localSize);
                    state.loadThrownObject();
                    return true;
                }
            }
        } else if(CODE(state) == &signalTrampoline) {   // within signal trampoline
            unsetFlag(DSState::eventDesc, DSState::VM_EVENT_MASK);
            if(!forceUnwind) {
                auto v = state.getLocal(0);
                state.setGlobal(toIndex(BuiltinVarOffset::EXIT_STATUS), std::move(v));
            }
        }
    }
    return false;
}

} // namespace ydsh

static bool runMainLoop(DSState &state) {
    while(!mainLoop(state)) {
        bool forceUnwind = state.symbolTable.get(TYPE::_InternalStatus)
                .isSameOrBaseTypeOf(*state.getThrownObject()->getType());
        if(!handleException(state, forceUnwind)) {
            return false;
        }
    }
    return true;
}

bool vmEval(DSState &state, CompiledCode &code) {
    state.resetState();
    reserveGlobalVar(state);

    windStackFrame(state, 0, 0, &code);

    return runMainLoop(state);
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
        values.push_back(DSValue::create<String_Object>(st.symbolTable.get(TYPE::String), std::string(*argv)));
    }
    auto obj = DSValue::create<Array_Object>(st.symbolTable.get(TYPE::StringArray), std::move(values));

    st.resetState();
    bool ret = callCommand(st, cmd, std::move(obj), DSValue());
    assert(ret);
    (void) ret;
    if(!st.controlStack.empty()) {
        bool r = runMainLoop(st);
        if(!r) {
            if(st.symbolTable.get(TYPE::_InternalStatus).isSameOrBaseTypeOf(*st.getThrownObject()->getType())) {
                st.loadThrownObject(); // force clear thrownObject
            } else {
                st.pushExitStatus(1);
            }
        }
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
    bool s = runMainLoop(state);
    DSValue ret;
    if(!handle->getReturnType()->isVoidType() && s) {
        ret = state.pop();
    }
    return ret;
}

DSValue callFunction(DSState &state, DSValue &&funcObj, std::vector<DSValue> &&args) {
    state.resetState();

    // push arguments
    auto *type = funcObj->getType();
    state.push(std::move(funcObj));
    for(auto &arg : args) {
        state.push(std::move(arg));
    }

    applyFuncObject(state, args.size());
    bool s = runMainLoop(state);
    DSValue ret;
    assert(type->isFuncType());
    if(!static_cast<FunctionType *>(type)->getReturnType()->isVoidType() && s) {
        ret = state.pop();
    }
    return ret;
}