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

#include "opcode.h"
#include "context.h"
#include "logger.h"

extern char **environ;

namespace ydsh {

#define vmswitch(V) switch(static_cast<OpCode>(V))

#if 0
#define vmcase(code) case OpCode::code: {fprintf(stderr, "pc: %u, code: %s\n", ctx.pc(), #code); }
#else
#define vmcase(code) case OpCode::code:
#endif

#define CALLABLE(ctx) (ctx.callableStack().back())
#define GET_CODE(ctx) (CALLABLE(ctx)->getCode())
#define CONST_POOL(ctx) (CALLABLE(ctx)->getConstPool())

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

static void forkAndCapture(bool isStr, RuntimeContext &ctx) {
    const unsigned short offset = read16(GET_CODE(ctx), ctx.pc() + 1);

    // capture stdout
    pid_t pipefds[2];

    if(pipe(pipefds) < 0) {
        perror("pipe creation failed\n");
        exit(1);    //FIXME: throw exception
    }

    pid_t pid = xfork();
    if(pid > 0) {   // parent process
        close(pipefds[WRITE_PIPE]);

        DSValue obj;

        if(isStr) {  // capture stdout as String
            static const int bufSize = 256;
            char buf[bufSize + 1];
            std::string str;
            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, bufSize);
                if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if(readSize <= 0) {
                    break;
                }
                buf[readSize] = '\0';
                str += buf;
            }

            // remove last newlines
            std::string::size_type pos = str.find_last_not_of('\n');
            if(pos == std::string::npos) {
                str.clear();
            } else {
                str.erase(pos + 1);
            }

            obj = DSValue::create<String_Object>(ctx.getPool().getStringType(), std::move(str));
        } else {    // capture stdout as String Array
            const char *ifs = ctx.getIFS();
            unsigned int skipCount = 1;

            static const int bufSize = 256;
            char buf[bufSize];
            std::string str;
            obj = DSValue::create<Array_Object>(ctx.getPool().getStringArrayType());
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
                        array->append(DSValue::create<String_Object>(
                                ctx.getPool().getStringType(), std::move(str)));
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
                array->append(DSValue::create<String_Object>(
                        ctx.getPool().getStringType(), std::move(str)));
            }
        }
        close(pipefds[READ_PIPE]);

        // wait exit
        int status;
        ctx.xwaitpid(pid, status, 0);
        if(WIFEXITED(status)) {
            ctx.updateExitStatus(WEXITSTATUS(status));
        }
        if(WIFSIGNALED(status)) {
            ctx.updateExitStatus(WTERMSIG(status));
        }

        // push object
        ctx.push(std::move(obj));

        ctx.pc() += offset - 1;
    } else if(pid == 0) {   // child process
        dup2(pipefds[WRITE_PIPE], STDOUT_FILENO);
        close(pipefds[READ_PIPE]);
        close(pipefds[WRITE_PIPE]);

        ctx.pc() += 2;
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
    unsigned int __argOffset;

    /**
     * if not have redirect option, offset is 0.
     */
    unsigned int __redirOffset;

    ProcKind __procKind;

    union {
        void *__dummy;
        FuncObject *__udcObj;
        builtin_command_t __builtinCmd;
        const char *__filePath;   // may be null if not found file
    };


    /**
     * following fields are valid, if parent process.
     */

    ExitKind __kind;
    pid_t __pid;
    int __exitStatus;

public:
    ProcState() = default;

    ProcState(unsigned int argOffset, unsigned int redirOffset, ProcKind procKind, void *ptr) :
            __argOffset(argOffset), __redirOffset(redirOffset),
            __procKind(procKind), __dummy(ptr),
            __kind(NORMAL), __pid(0), __exitStatus(0) { }

    ~ProcState() = default;

    /**
     * only called, if parent process.
     */
    void set(ExitKind kind, int exitStatus) {
        this->__kind = kind;
        this->__exitStatus = exitStatus;
    }

    /**
     * only called, if parent process.
     */
    void setPid(pid_t pid) {
        this->__pid = pid;
    }

    unsigned int argOffset() const {
        return this->__argOffset;
    }

    unsigned int redirOffset() const {
        return this->__redirOffset;
    }

    ProcKind procKind() const {
        return this->__procKind;
    }

    FuncObject *udcObj() const {
        return this->__udcObj;
    }

    builtin_command_t builtinCmd() const {
        return this->__builtinCmd;
    }

    const char *filePath() const {
        return this->__filePath;
    }

    ExitKind kind() const {
        return this->__kind;
    }

    pid_t pid() const {
        return this->__pid;
    }

    int exitStatus() const {
        return this->__exitStatus;
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
    }

    void redirect(RuntimeContext &ctx, unsigned int procIndex, int errorPipe);

    DSValue *getARGV(unsigned int procIndex);
    const char *getCommandName(unsigned int procIndex);

    void checkChildError(RuntimeContext &ctx, const std::pair<unsigned int, ChildError> &errorPair);
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
void PipelineState::redirect(RuntimeContext &ctx, unsigned int procIndex, int errorPipe) {
#define CHECK_ERROR(result) do { occurredError = (result); if(occurredError != 0) { goto ERR; } } while(0)

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
            this->checkChildError(ctx, std::make_pair(0, e));
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

static PipelineState &activePipeline(RuntimeContext &ctx) {
    return *typeAs<PipelineState>(ctx.peek());
}

static void callCommand(RuntimeContext &ctx, unsigned short procIndex) {
    auto &pipeline = activePipeline(ctx);
    const unsigned int procSize = pipeline.procStates.size();

    const auto &procState = pipeline.procStates[procIndex];
    DSValue *ptr = pipeline.getARGV(procIndex);
    const auto procKind = procState.procKind();
    if(procKind == ProcState::ProcKind::USER_DEFINED) { // invoke user-defined command
        auto *udcObj = procState.udcObj();
        closeAllPipe(procSize, pipeline.selfpipes);
        ctx.callUserDefinedCommand(udcObj, ptr);
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

                pipeline.redirect(ctx, 0, -1);

                const int pid = getpid();
                ctx.updateExitStatus(cmd_ptr(&ctx, argc, argv));

                if(pid == getpid()) {   // in parent process (if call command or eval, may be child)
                    // flush and restore
                    flushStdFD();
                    if(restoreFD) {
                        restoreStdFD(origFDs);
                    }

                    ctx.pc()++; // skip next instruction (EXIT_CHILD)
                }
            } else {
                closeAllPipe(procSize, pipeline.selfpipes);
                ctx.updateExitStatus(cmd_ptr(&ctx, argc, argv));
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

static void callPipeline(RuntimeContext &ctx) {
    auto &pipeline = activePipeline(ctx);
    const unsigned int procSize = pipeline.procStates.size();

    // check builtin command
    if(procSize == 1 && pipeline.procStates[0].procKind() == ProcState::ProcKind::BUILTIN) {
        // set pc to next instruction
        ctx.pc() += read16(GET_CODE(ctx), ctx.pc() + 2) - 1;
        return;
    }

    int pipefds[procSize][2];
    initPipe(pipeline, procSize, pipefds);


    // fork
    pid_t pid;
    std::pair<unsigned int, ChildError> errorPair;
    unsigned int procIndex;
    unsigned int actualProcSize = 0;
    for(procIndex = 0; procIndex < procSize && (pid = xfork()) > 0; procIndex++) {
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
                ctx.getPathCache().removePath(cmdName);
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
            ctx.xwaitpid(pipeline.procStates[i].pid(), status, 0);
            if(WIFEXITED(status)) {
                pipeline.procStates[i].set(ProcState::NORMAL, WEXITSTATUS(status));
            }
            if(WIFSIGNALED(status)) {
                pipeline.procStates[i].set(ProcState::INTR, WTERMSIG(status));
            }
        }

        ctx.updateExitStatus(pipeline.procStates[actualProcSize - 1].exitStatus());
        pipeline.checkChildError(ctx, errorPair);

        // set pc to next instruction
        unsigned int byteSize = read8(GET_CODE(ctx), ctx.pc() + 1);
        ctx.pc() += read16(GET_CODE(ctx), ctx.pc() + 2 + (byteSize - 1) * 2) - 1;
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

        pipeline.redirect(ctx, procIndex, pipeline.selfpipes[procIndex][WRITE_PIPE]);

        closeAllPipe(procSize, pipefds);

        // set pc to next instruction
        ctx.pc() += read16(GET_CODE(ctx), ctx.pc() + 2 + procIndex * 2) - 1;
    } else {
        perror("child process error");
        exit(1);
    }
}

void RuntimeContext::execBuiltinCommand(char *const argv[]) {
    int cmdIndex = getBuiltinCommandIndex(argv[0]);
    if(cmdIndex < 0) {
        fprintf(stderr, "ydsh: %s: not builtin command\n", argv[0]);
        this->updateExitStatus(1);
        return;
    }

    int argc;
    for(argc = 0; argv[argc] != nullptr; argc++);

    this->updateExitStatus(::ydsh::execBuiltinCommand(this, cmdIndex, argc, argv));
    flushStdFD();
}

const char *PipelineState::getCommandName(unsigned int procIndex) {
    return typeAs<String_Object>(this->getARGV(procIndex)[0])->getValue();
}

void PipelineState::checkChildError(RuntimeContext &ctx, const std::pair<unsigned int, ChildError> &errorPair) {
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
        ctx.updateExitStatus(1);
        ctx.throwSystemError(errorPair.second.errorNum, std::move(msg));
    }
}


/**
 * stack top value must be String_Object and it represents command name.
 */
static void openProc(RuntimeContext &ctx) {
    DSValue value = ctx.pop();

    // resolve proc kind (external command, builtin command or user-defined command)
    const char *commandName = typeAs<String_Object>(value)->getValue();
    ProcState::ProcKind procKind = ProcState::EXTERNAL;
    void *ptr = nullptr;

    // first, check user-defined command
    {
        auto *udcObj = ctx.lookupUserDefinedCommand(commandName);
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
        ptr = (void *)ctx.getPathCache().searchPath(commandName);
    }

    auto &pipeline = activePipeline(ctx);
    unsigned int argOffset = pipeline.argArray.size();
    unsigned int redirOffset = pipeline.redirOptions.size();
    pipeline.procStates.push_back(ProcState(argOffset, redirOffset, procKind, ptr));

    pipeline.argArray.push_back(std::move(value));
}

static void closeProc(RuntimeContext &ctx) {
    auto &pipeline = activePipeline(ctx);
    pipeline.argArray.push_back(DSValue());
    pipeline.redirOptions.push_back(std::make_pair(RedirectOP::DUMMY, DSValue()));
}

/**
 * stack top value must be String_Object or Array_Object.
 */
static void addArg(RuntimeContext &ctx, bool skipEmptyString) {
    DSValue value = ctx.pop();
    DSType *valueType = value->getType();
    if(*valueType == ctx.getPool().getStringType()) {
        if(skipEmptyString && typeAs<String_Object>(value)->empty()) {
            return;
        }
        activePipeline(ctx).argArray.push_back(std::move(value));
        return;
    }

    if(*valueType == ctx.getPool().getStringArrayType()) {
        Array_Object *arrayObj = typeAs<Array_Object>(value);
        for(auto &element : arrayObj->getValues()) {
            if(typeAs<String_Object>(element)->empty()) {
                continue;
            }
            activePipeline(ctx).argArray.push_back(element);
        }
    } else {
        fatal("illegal command parameter type: %s\n", ctx.getPool().getTypeName(*valueType).c_str());
    }
}

/**
 * stack top value must be String_Object.
 */
static void addRedirOption(RuntimeContext &ctx, RedirectOP op) {
    DSValue value = ctx.pop();
    DSType *valueType = value->getType();
    if(*valueType == ctx.getPool().getStringType()) {
        activePipeline(ctx).redirOptions.push_back(std::make_pair(op, value));
    } else {
        fatal("illegal command parameter type: %s\n", ctx.getPool().getTypeName(*valueType).c_str());
    }
}

static void mainLoop(RuntimeContext &ctx) {
    while(true) {
        vmswitch(GET_CODE(ctx)[++ctx.pc()]) {
        vmcase(NOP) {
            break;
        }
        vmcase(STOP_EVAL) {
            return;
        }
        vmcase(ASSERT) {
            ctx.checkAssertion();
            break;
        }
        vmcase(PRINT) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.printStackTop(reinterpret_cast<DSType *>(v));
            break;
        }
        vmcase(INSTANCE_OF) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.instanceOf(reinterpret_cast<DSType *>(v));
            break;
        }
        vmcase(CHECK_CAST) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.checkCast(reinterpret_cast<DSType *>(v));
            break;
        }
        vmcase(PUSH_TRUE) {
            ctx.push(ctx.getTrueObj());
            break;
        }
        vmcase(PUSH_FALSE) {
            ctx.push(ctx.getFalseObj());
            break;
        }
        vmcase(PUSH_ESTRING) {
            ctx.push(ctx.getEmptyStrObj());
            break;
        }
        vmcase(LOAD_CONST) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.push(CONST_POOL(ctx)[index]);
            break;
        }
        vmcase(LOAD_FUNC) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.loadGlobal(index);

            auto *func = typeAs<FuncObject>(ctx.peek());
            if(func->getType() == nullptr) {
                auto *handle = ctx.getSymbolTable().lookupHandle(func->getCallable().getName());
                assert(handle != nullptr);
                func->setType(handle->getFieldType(ctx.getPool()));
            }
            break;
        }
        vmcase(LOAD_GLOBAL) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.loadGlobal(index);
            break;
        }
        vmcase(STORE_GLOBAL) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.storeGlobal(index);
            break;
        }
        vmcase(LOAD_LOCAL) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.loadLocal(index);
            break;
        }
        vmcase(STORE_LOCAL) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.storeLocal(index);
            break;
        }
        vmcase(LOAD_FIELD) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.loadField(index);
            break;
        }
        vmcase(STORE_FIELD) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.storeField(index);
            break;
        }
        vmcase(IMPORT_ENV) {
            unsigned char b = read8(GET_CODE(ctx), ++ctx.pc());
            ctx.importEnv(b > 0);
            break;
        }
        vmcase(LOAD_ENV) {
            ctx.loadEnv();
            break;
        }
        vmcase(STORE_ENV) {
            ctx.storeEnv();
            break;
        }
        vmcase(POP) {
            ctx.popNoReturn();
            break;
        }
        vmcase(DUP) {
            ctx.dup();
            break;
        }
        vmcase(DUP2) {
            ctx.dup2();
            break;
        }
        vmcase(SWAP) {
            ctx.swap();
            break;
        }
        vmcase(NEW_STRING) {
            ctx.push(DSValue::create<String_Object>(ctx.getPool().getStringType()));
            break;
        }
        vmcase(APPEND_STRING) {
            DSValue v(ctx.pop());
            typeAs<String_Object>(ctx.peek())->append(std::move(v));
            break;
        }
        vmcase(NEW_ARRAY) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.push(DSValue::create<Array_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(APPEND_ARRAY) {
            DSValue v(ctx.pop());
            typeAs<Array_Object>(ctx.peek())->append(std::move(v));
            break;
        }
        vmcase(NEW_MAP) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.push(DSValue::create<Map_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(APPEND_MAP) {
            DSValue value(ctx.pop());
            DSValue key(ctx.pop());
            typeAs<Map_Object>(ctx.peek())->add(std::make_pair(std::move(key), std::move(value)));
            break;
        }
        vmcase(NEW_TUPLE) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.push(DSValue::create<Tuple_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(NEW) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.newDSObject(reinterpret_cast<DSType *>(v));
            break;
        }
        vmcase(CALL_INIT) {
            unsigned short paramSize = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.callConstructor(paramSize);
            break;
        }
        vmcase(CALL_METHOD) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            unsigned short paramSize = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.callMethod(index, paramSize);
            break;
        }
        vmcase(CALL_FUNC) {
            unsigned short paramSize = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.applyFuncObject(paramSize);
            break;
        }
        vmcase(RETURN) {
            ctx.restoreStackState();
            ctx.callableStack().pop_back();
            break;
        }
        vmcase(RETURN_V) {
            DSValue v(ctx.pop());
            ctx.restoreStackState();
            ctx.callableStack().pop_back();
            ctx.push(std::move(v));
            break;
        }
        vmcase(RETURN_UDC) {
            auto v = ctx.pop();
            ctx.restoreStackState();
            ctx.callableStack().pop_back();
            ctx.updateExitStatus(typeAs<Int_Object>(v)->getValue());
            break;
        }
        vmcase(BRANCH) {
            unsigned short offset = read16(GET_CODE(ctx), ctx.pc() + 1);
            if(typeAs<Boolean_Object>(ctx.pop())->getValue()) {
                ctx.pc() += 2;
            } else {
                ctx.pc() += offset - 1;
            }
            break;
        }
        vmcase(GOTO) {
            unsigned int index = read32(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() = index - 1;
            break;
        }
        vmcase(THROW) {
            ctx.throwException(ctx.pop());
        }
        vmcase(ENTER_FINALLY) {
            const unsigned int index = read32(GET_CODE(ctx), ctx.pc() + 1);
            const unsigned int savedIndex = ctx.pc() + 4;
            ctx.push(DSValue::createNum(savedIndex));
            ctx.pc() = index - 1;
            break;
        }
        vmcase(EXIT_FINALLY) {
            switch(ctx.peek().kind()) {
            case DSValueKind::OBJECT: {
                ctx.throwException(ctx.pop());
                break;
            }
            case DSValueKind::NUMBER: {
                unsigned int index = static_cast<unsigned int>(ctx.pop().value());
                ctx.pc() = index;
                break;
            }
            }
            break;
        }
        vmcase(COPY_INT) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            int v = typeAs<Int_Object>(ctx.pop())->getValue();
            ctx.push(DSValue::create<Int_Object>(*type, v));
            break;
        }
        vmcase(TO_BYTE) {
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            v &= 0xFF;  // fill higher bits (8th ~ 31) with 0
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getByteType(), v));
            break;
        }
        vmcase(TO_U16) {
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getUint16Type(), v));
            break;
        }
        vmcase(TO_I16) {
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            if(v & 0x8000) {    // if 15th bit is 1, fill higher bits with 1
                v |= 0xFFFF0000;
            }
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getInt16Type(), v));
            break;
        }
        vmcase(NEW_LONG) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            unsigned long l = v;
            ctx.push(DSValue::create<Long_Object>(*type, l));
            break;
        }
        vmcase(COPY_LONG) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            long v = typeAs<Long_Object>(ctx.pop())->getValue();
            ctx.push(DSValue::create<Long_Object>(*type, v));
            break;
        }
        vmcase(I_NEW_LONG) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            int v = typeAs<Int_Object>(ctx.pop())->getValue();
            long l = v;
            ctx.push(DSValue::create<Long_Object>(*type, l));
            break;
        }
        vmcase(NEW_INT) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            unsigned long l = typeAs<Long_Object>(ctx.pop())->getValue();
            unsigned int v = static_cast<unsigned int>(l);
            ctx.push(DSValue::create<Int_Object>(*type, v));
            break;
        }
        vmcase(U32_TO_D) {
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            double d = static_cast<double>(v);
            ctx.push(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), d));
            break;
        }
        vmcase(I32_TO_D) {
            int v = typeAs<Int_Object>(ctx.pop())->getValue();
            double d = static_cast<double>(v);
            ctx.push(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), d));
            break;
        }
        vmcase(U64_TO_D) {
            unsigned long v = typeAs<Long_Object>(ctx.pop())->getValue();
            double d = static_cast<double>(v);
            ctx.push(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), d));
            break;
        }
        vmcase(I64_TO_D) {
            long v = typeAs<Long_Object>(ctx.pop())->getValue();
            double d = static_cast<double>(v);
            ctx.push(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), d));
            break;
        }
        vmcase(D_TO_U32) {
            double d = typeAs<Float_Object>(ctx.pop())->getValue();
            unsigned int v = static_cast<unsigned int>(d);
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), v));
            break;
        }
        vmcase(D_TO_I32) {
            double d = typeAs<Float_Object>(ctx.pop())->getValue();
            int v = static_cast<int>(d);
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), v));
            break;
        }
        vmcase(D_TO_U64) {
            double d = typeAs<Float_Object>(ctx.pop())->getValue();
            unsigned long v = static_cast<unsigned long>(d);
            ctx.push(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), v));
            break;
        }
        vmcase(D_TO_I64) {
            double d = typeAs<Float_Object>(ctx.pop())->getValue();
            long v = static_cast<long>(d);
            ctx.push(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), v));
            break;
        }
        vmcase(SUCCESS_CHILD) {
            exit(ctx.getExitStatus());
        }
        vmcase(FAILURE_CHILD) {
            ctx.handleUncaughtException(ctx.pop());
            exit(1);
        }
        vmcase(CAPTURE_STR) {
            forkAndCapture(true, ctx);
            break;
        }
        vmcase(CAPTURE_ARRAY) {
            forkAndCapture(false, ctx);
            break;
        }
        vmcase(NEW_PIPELINE) {
            if(!ctx.getPipeline()) {
                ctx.getPipeline() = DSValue::create<PipelineState>();
            }

            if(ctx.getPipeline().get()->getRefcount() == 1) {   // reuse cached object
                typeAs<PipelineState>(ctx.getPipeline())->clear();
                ctx.push(ctx.getPipeline());
            } else {
                ctx.push(DSValue::create<PipelineState>());
            }
            break;
        }
        vmcase(CALL_PIPELINE) {
            callPipeline(ctx);
            break;
        }
        vmcase(OPEN_PROC) {
            openProc(ctx);
            break;
        }
        vmcase(CLOSE_PROC) {
            closeProc(ctx);
            break;
        }
        vmcase(ADD_CMD_ARG) {
            unsigned char v = read8(GET_CODE(ctx), ++ctx.pc());
            addArg(ctx, v > 0);
            break;
        }
        vmcase(ADD_REDIR_OP) {
            unsigned char v = read8(GET_CODE(ctx), ++ctx.pc());
            addRedirOption(ctx, static_cast<RedirectOP>(v));
            break;
        }
        vmcase(EXPAND_TILDE) {
            std::string str(expandTilde(typeAs<String_Object>(ctx.pop())->getValue()));
            ctx.push(DSValue::create<String_Object>(ctx.getPool().getStringType(), std::move(str)));
            break;
        }
        vmcase(CALL_CMD) {
            unsigned char v = read8(GET_CODE(ctx), ++ctx.pc());
            callCommand(ctx, v);
            break;
        }
        vmcase(POP_PIPELINE) {
            ctx.popNoReturn();
            if(ctx.getExitStatus() == 0) {
                ctx.push(ctx.getTrueObj());
            } else {
                ctx.push(ctx.getFalseObj());
            }
            break;
        }
        }
    }
}

/**
 * if found exception handler, return true.
 * otherwise return false.
 */
static bool handleException(RuntimeContext &ctx) {
    while(!ctx.callableStack().empty()) {
        if(ctx.pc() == 0) { // from native method
            ctx.restoreStackState();
            continue;
        }

        // search exception entry
        const unsigned int occurredPC = ctx.pc();
        const DSType *occurredType = ctx.getThrownObject()->getType();

        for(unsigned int i = 0; CALLABLE(ctx)->getExceptionEntries()[i].type != nullptr; i++) {
            const ExceptionEntry &entry = CALLABLE(ctx)->getExceptionEntries()[i];
            if(occurredPC >= entry.begin && occurredPC < entry.end
               && entry.type->isSameOrBaseTypeOf(*occurredType)) {
                ctx.pc() = entry.dest - 1;
                ctx.loadThrownObject();
                return true;
            }
        }

        // unwind stack
        if(ctx.callableStack().size() > 1) {
            ctx.restoreStackState();
        }
        ctx.callableStack().pop_back();
    }
    return false;
}

bool vmEval(RuntimeContext &ctx, Callable &callable) {
    ctx.resetState();

    ctx.callableStack().push_back(&callable);
    ctx.skipHeader();

    while(true) {
        try {
            mainLoop(ctx);
            ctx.callableStack().clear();
            return true;
        } catch(const DSExcepton &) {
            if(handleException(ctx)) {
                continue;
            }
            return false;
        }
    }
}

} // namespace ydsh
