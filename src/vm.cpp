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

#include <cstdlib>
#include <cerrno>
#include <random>

#include "opcode.h"
#include "vm.h"
#include "logger.h"
#include "constant.h"
#include "redir.h"
#include "misc/files.h"
#include "misc/num.h"


// #####################
// ##     DSState     ##
// #####################

flag32_set_t DSState::eventDesc = 0;

SigSet DSState::pendingSigSet;

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

DSState::DSState() :
        trueObj(DSValue::create<Boolean_Object>(this->symbolTable.get(TYPE::Boolean), true)),
        falseObj(DSValue::create<Boolean_Object>(this->symbolTable.get(TYPE::Boolean), false)),
        emptyStrObj(DSValue::create<String_Object>(this->symbolTable.get(TYPE::String), std::string())),
        emptyFDObj(DSValue::create<UnixFD_Object>(this->symbolTable.get(TYPE::UnixFD), -1)),
        logicalWorkingDir(initLogicalWorkingDir()), callStack(new DSValue[DEFAULT_STACK_SIZE]),
        baseTime(std::chrono::system_clock::now()) { }

void DSState::reserveLocalStackImpl(unsigned int needSize) {
    unsigned int newSize = this->callStackSize;
    do {
        newSize += (newSize >> 1u);
    } while(newSize < needSize);
    auto newTable = new DSValue[newSize];
    for(unsigned int i = 0; i < this->stackTopIndex() + 1; i++) {
        newTable[i] = std::move(this->callStack[i]);
    }
    delete[] this->callStack;
    this->callStack = newTable;
    this->callStackSize = newSize;
}

unsigned int DSState::getTermHookIndex() {
    if(this->termHookIndex == 0) {
        auto *handle = this->symbolTable.lookupHandle(VAR_TERM_HOOK);
        assert(handle != nullptr);
        this->termHookIndex = handle->getIndex();
    }
    return this->termHookIndex;
}

bool DSState::checkCast(DSType *targetType) {
    if(!this->peek()->introspect(*this, targetType)) {
        DSType *stackTopType = this->pop()->getType();
        std::string str("cannot cast ");
        str += this->symbolTable.getTypeName(*stackTopType);
        str += " to ";
        str += this->symbolTable.getTypeName(*targetType);
        raiseError(*this, TYPE::TypeCastError, std::move(str));
        return false;
    }
    return true;
}

bool DSState::checkAssertion() {
    auto msg(this->pop());
    assert(typeAs<String_Object>(msg)->getValue() != nullptr);

    if(!typeAs<Boolean_Object>(this->pop())->getValue()) {
        raiseError(*this, TYPE::_AssertFail, std::string(typeAs<String_Object>(msg)->getValue()));
        return false;
    }
    return true;
}

const char *DSState::loadEnv(bool hasDefault) {
    DSValue dValue;
    if(hasDefault) {
        dValue = this->pop();
    }
    DSValue nameObj(this->pop());
    const char *name = typeAs<String_Object>(nameObj)->getValue();

    const char *env = getenv(name);
    if(env == nullptr && hasDefault) {
        setenv(name, typeAs<String_Object>(dValue)->getValue(), 1);
        env = getenv(name);
    }

    if(env == nullptr) {
        std::string str = UNDEF_ENV_ERROR;
        str += name;
        raiseSystemError(*this, EINVAL, std::move(str));
        return nullptr;
    }
    return env;
}

bool DSState::windStackFrame(unsigned int stackTopOffset, unsigned int paramSize, const DSCode *code) {
    const unsigned int maxVarSize = code->is(CodeKind::NATIVE) ? paramSize :
                                    static_cast<const CompiledCode *>(code)->getLocalVarNum();
    const unsigned int localVarOffset = this->stackTopIndex() - paramSize + 1;
    const unsigned int operandSize = code->is(CodeKind::NATIVE) ? 4 :
                                     static_cast<const CompiledCode *>(code)->getStackDepth();

    if(this->controlStack.size() == DSState::MAX_CONTROL_STACK_SIZE) {
        raiseError(*this, TYPE::StackOverflowError, "local stack size reaches limit");
        return false;
    }

    // save current control frame
    this->stackTopIndex() -= stackTopOffset;
    this->controlStack.push_back(this->getFrame());
    this->stackTopIndex() += stackTopOffset;

    // reallocate stack
    this->reserveLocalStack(maxVarSize - paramSize + operandSize);

    // prepare control frame
    this->code() = code;
    this->stackTopIndex() = this->stackTopIndex() + maxVarSize - paramSize;
    this->stackBottomIndex() = this->stackTopIndex();
    this->localVarOffset() = localVarOffset;
    this->pc() = this->code()->getCodeOffset() - 1;
    return true;
}

void DSState::unwindStackFrame() {
    auto frame = this->controlStack.back();

    this->code() = frame.code;
    this->stackBottomIndex() = frame.stackBottomIndex;
    this->localVarOffset() = frame.localVarOffset;
    this->pc() = frame.pc;

    unsigned int oldStackTopIndex = frame.stackTopIndex;
    while(this->stackTopIndex() > oldStackTopIndex) {
        this->popNoReturn();
    }
    this->controlStack.pop_back();
}

bool DSState::prepareUserDefinedCommandCall(const DSCode *code, DSValue &&argvObj,
                                            DSValue &&restoreFD, const flag8_set_t attr) {
    if(hasFlag(attr, UDC_ATTR_SETVAR)) {
        // reset exit status
        this->updateExitStatus(0);
    }

    // set stack stack
    if(!this->windStackFrame(0, 0, code)) {
        return false;
    }

    // set parameter
    this->setLocal(UDC_PARAM_ATTR, DSValue::createNum(attr));
    this->setLocal(UDC_PARAM_REDIR, std::move(restoreFD));
    this->setLocal(UDC_PARAM_ARGV, std::move(argvObj));

    if(hasFlag(attr, UDC_ATTR_SETVAR)) {    // set variable
        auto argv = typeAs<Array_Object>(this->getLocal(UDC_PARAM_ARGV));
        eraseFirst(*argv);
        const unsigned int argSize = argv->getValues().size();
        this->setLocal(UDC_PARAM_ARGV + 1, DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Int32), argSize));   // #
        this->setLocal(UDC_PARAM_ARGV + 2, this->getGlobal(BuiltinVarOffset::POS_0)); // 0
        unsigned int limit = 9;
        if(argSize < limit) {
            limit = argSize;
        }

        unsigned int index = 0;
        for(; index < limit; index++) {
            this->setLocal(index + UDC_PARAM_ARGV + 3, argv->getValues()[index]);
        }

        for(; index < 9; index++) {
            this->setLocal(index + UDC_PARAM_ARGV + 3, this->emptyStrObj);  // set remain
        }
    }
    return true;
}

#define CODE(ctx) ((ctx).code())
#define GET_CODE(ctx) (CODE(ctx)->getCode())
#define CONST_POOL(ctx) (static_cast<const CompiledCode *>(CODE(ctx))->getConstPool())

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
    auto *ifsObj = typeAs<String_Object>(state.getGlobal(BuiltinVarOffset::IFS));
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

static DSValue newFD(const DSState &st, int &fd) {
    if(fd < 0) {
        return st.emptyFDObj;
    }
    int v = fd;
    fd = -1;
    auto value = DSValue::create<UnixFD_Object>(st.symbolTable.get(TYPE::UnixFD), v);
    typeAs<UnixFD_Object>(value)->closeOnExec(true);
    return value;
}

bool DSState::forkAndEval() {
    const auto forkKind = static_cast<ForkKind >(read8(GET_CODE(*this), this->pc() + 1));
    const unsigned short offset = read16(GET_CODE(*this), this->pc() + 2);

    // flush standard stream due to prevent mixing io buffer
    flushStdFD();

    // set in/out pipe
    auto pipeset = initPipeSet(forkKind);
    const bool rootShell = this->isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    auto proc = Proc::fork(*this, pgid, needForeground(forkKind) && rootShell);
    if(proc.pid() > 0) {   // parent process
        tryToClose(pipeset.in[READ_PIPE]);
        tryToClose(pipeset.out[WRITE_PIPE]);

        DSValue obj;

        switch(forkKind) {
        case ForkKind::STR:
        case ForkKind::ARRAY: {
            tryToClose(pipeset.in[WRITE_PIPE]);
            const bool isStr = forkKind == ForkKind::STR;
            obj = isStr ? readAsStr(*this, pipeset.out[READ_PIPE]) : readAsStrArray(*this, pipeset.out[READ_PIPE]);
            auto waitOp = this->isRootShell() && this->isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
            int ret = proc.wait(waitOp);   // wait exit
            tryToClose(pipeset.out[READ_PIPE]); // close read pipe after wait, due to prevent EPIPE
            this->updateExitStatus(ret);
            tryToBeForeground(*this);
            break;
        }
        case ForkKind::IN_PIPE:
        case ForkKind::OUT_PIPE: {
            auto entry = JobImpl::create(proc);
            this->jobTable.attach(entry, true);
            int &fd = forkKind == ForkKind::IN_PIPE ? pipeset.in[WRITE_PIPE] : pipeset.out[READ_PIPE];
            obj = newFD(*this, fd);
            break;
        }
        case ForkKind::COPROC:
        case ForkKind::JOB:
        case ForkKind::DISOWN: {
            bool disown = forkKind == ForkKind::DISOWN;
            auto entry = JobImpl::create(proc);
            this->jobTable.attach(entry, disown);
            obj = DSValue::create<Job_Object>(
                    this->symbolTable.get(TYPE::Job),
                    entry,
                    newFD(*this, pipeset.in[WRITE_PIPE]),
                    newFD(*this, pipeset.out[READ_PIPE])
            );
            break;
        }
        }

        // push object
        if(obj) {
            this->push(std::move(obj));
        }

        this->pc() += offset - 1;
    } else if(proc.pid() == 0) {   // child process
        tryToDup(pipeset.in[READ_PIPE], STDIN_FILENO);
        tryToClose(pipeset.in);
        tryToDup(pipeset.out[WRITE_PIPE], STDOUT_FILENO);
        tryToClose(pipeset.out);

        if(forkKind == ForkKind::DISOWN || (forkKind == ForkKind::JOB && !this->isJobControl())) {
            redirInToNull();
        }

        this->pc() += 3;
    } else {
        raiseSystemError(*this, EAGAIN, "fork failed");
        return false;
    }
    return true;
}

/* for pipeline evaluation */

class CmdResolver {
private:
    flag8_set_t mask;
    flag8_set_t searchOp;

public:
    static constexpr flag8_t MASK_UDC      = 1u << 0u;
    static constexpr flag8_t MASK_EXTERNAL = 1u << 1u;

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

Command CmdResolver::operator()(DSState &state, const char *cmdName) const {
    Command cmd{};

    // first, check user-defined command
    if(!hasFlag(this->mask, MASK_UDC)) {
        auto *udcObj = state.lookupUserDefinedCommand(cmdName);
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
        raiseError(state, TYPE::SystemError, std::move(str));
    } else {
        raiseSystemError(state, errnum, std::move(str));
    }
}

int DSState::forkAndExec(const char *cmdName, Command cmd, char **const argv, DSValue &&redirConfig) {
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

    bool rootShell = this->isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    auto proc = Proc::fork(*this, pgid, rootShell);
    if(proc.pid() == -1) {
        raiseCmdError(*this, cmdName, EAGAIN);
        return 1;
    } else if(proc.pid() == 0) {   // child
        close(selfpipe[READ_PIPE]);
        xexecve(cmd.filePath, argv, nullptr, redirConfig);

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
            this->pathCache.removePath(argv[0]);
        }

        // wait process or job termination
        int status;
        auto waitOp = rootShell && this->isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
        status = proc.wait(waitOp);
        if(proc.state() != Proc::TERMINATED) {
            this->jobTable.attach(JobImpl::create(proc));
        }
        int ret = tryToBeForeground(*this);
        LOG(DUMP_EXEC, "tryToBeForeground: %d, %s", ret, strerror(errno));
        this->jobTable.updateStatus();
        if(errnum != 0) {
            raiseCmdError(*this, cmdName, errnum);
            return 1;
        }
        return status;
    }
}

bool DSState::callCommand(Command cmd, DSValue &&argvObj, DSValue &&redirConfig, flag8_set_t attr) {
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
        return this->prepareUserDefinedCommandCall(cmd.udc, std::move(argvObj), std::move(redirConfig), attr);
    }
    case CmdKind::BUILTIN: {
        int status = cmd.builtinCmd(*this, *array);
        flushStdFD();
        if(this->hasError()) {
            return false;
        }
        this->pushExitStatus(status);
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
            int status = this->forkAndExec(cmdName, cmd, argv, std::move(redirConfig));
            this->pushExitStatus(status);
        } else {
            xexecve(cmd.filePath, argv, nullptr, redirConfig);
            raiseCmdError(*this, cmdName, errno);
        }
        return !this->hasError();
    }
    }
    return true;    // normally unreachable, but need to suppress gcc warning.
}

namespace ydsh {

int invalidOptionError(const Array_Object &obj, const GetOptState &s);

const NativeCode *getNativeCode(unsigned int index);

}

bool DSState::callBuiltinCommand(DSValue &&argvObj, DSValue &&redir, flag8_set_t attr) {
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
            this->pushExitStatus(s);
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
            return this->callCommand(resolve(*this, cmdName), std::move(argvObj), std::move(redir), attr);
        }

        // show command description
        unsigned int successCount = 0;
        for(; index < argc; index++) {
            const char *commandName = str(arrayObj.getValues()[index]);
            auto cmd = CmdResolver(0, FilePathCache::DIRECT_SEARCH)(*this, commandName);
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
                    } else if(this->pathCache.isCached(commandName)) {
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
        this->pushExitStatus(successCount > 0 ? 0 : 1);
        return true;
    }
    this->pushExitStatus(0);
    return true;
}

void DSState::callBuiltinExec(DSValue &&array, DSValue &&redir) {
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
            this->pushExitStatus(s);
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

        const char *filePath = this->pathCache.searchPath(argv2[0], FilePathCache::DIRECT_SEARCH);
        if(progName != nullptr) {
            argv2[0] = const_cast<char *>(progName);
        }

        char *envp[] = {nullptr};
        xexecve(filePath, argv2, clearEnv ? envp : nullptr, redir);
        PERROR(argvObj, "%s", str(argvObj.getValues()[index]));
        exit(1);
    }
    this->pushExitStatus(0);
}

bool DSState::callPipeline(bool lastPipe) {
    /**
     * ls | grep .
     * ==> pipeSize == 1, procSize == 2
     *
     * if lastPipe is true,
     *
     * ls | { grep . ;}
     * ==> pipeSize == 1, procSize == 1
     */
    const unsigned int procSize = read8(GET_CODE(*this), this->pc() + 1) - 1;
    const unsigned int pipeSize = procSize - (lastPipe ? 0 : 1);

    assert(pipeSize > 0);

    int pipefds[pipeSize][2];
    initAllPipe(pipeSize, pipefds);

    // fork
    Proc childs[procSize];
    const bool rootShell = this->isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    Proc proc;  //NOLINT

    unsigned int procIndex;
    for(procIndex = 0; procIndex < procSize && (proc = Proc::fork(*this, pgid, rootShell)).pid() > 0; procIndex++) {
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
            ::dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex > 0 && procIndex < pipeSize) {   // other process.
            ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            ::dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == pipeSize && !lastPipe) {    // last process
            ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
        }
        closeAllPipe(pipeSize, pipefds);

        // set pc to next instruction
        this->pc() += read16(GET_CODE(*this), this->pc() + 2 + procIndex * 2) - 1;
    } else if(procIndex == procSize) { // parent (last pipeline)
        /**
         * in last pipe, save current stdin before call dup2
         */
        auto jobEntry = JobImpl::create(procSize, childs, lastPipe);

        if(lastPipe) {
            ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
        }
        closeAllPipe(pipeSize, pipefds);

        if(lastPipe) {
            this->push(DSValue::create<PipelineState>(*this, std::move(jobEntry)));
        } else {
            // job termination
            auto waitOp = rootShell && this->isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
            int status = jobEntry->wait(waitOp);
            if(jobEntry->available()) {
                this->jobTable.attach(jobEntry);
            }
            tryToBeForeground(*this);
            this->jobTable.updateStatus();
            this->pushExitStatus(status);
        }

        // set pc to next instruction
        this->pc() += read16(GET_CODE(*this), this->pc() + 2 + procIndex * 2) - 1;
    } else {
        // force terminate forked process.
        for(unsigned int i = 0; i < procIndex; i++) {
            kill(childs[i].pid(), SIGKILL);
        }

        raiseSystemError(*this, EAGAIN, "fork failed");
        return false;
    }
    return true;
}

void DSState::addCmdArg(bool skipEmptyStr) {
    /**
     * stack layout
     *
     * ===========> stack grow
     * +------+-------+-------+
     * | argv | redir | value |
     * +------+-------+-------+
     */
    DSValue value = this->pop();
    DSType *valueType = value->getType();

    auto *argv = typeAs<Array_Object>(this->callStack[this->stackTopIndex() - 1]);
    if(valueType->is(TYPE::String)) {  // String
        if(skipEmptyStr && typeAs<String_Object>(value)->empty()) {
            return;
        }
        argv->append(std::move(value));
        return;
    }

    if(valueType->is(TYPE::UnixFD)) { // UnixFD
        if(!this->peek()) {
            this->pop();
            this->push(DSValue::create<RedirConfig>());
        }
        auto fdPath = typeAs<UnixFD_Object>(value)->toString();
        auto strObj = DSValue::create<String_Object>(this->symbolTable.get(TYPE::String), std::move(fdPath));
        typeAs<RedirConfig>(this->peek())->addRedirOp(RedirOP::NOP, std::move(value));
        argv->append(std::move(strObj));
        return;
    }

    assert(valueType->is(TYPE::StringArray));  // Array<String>
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

bool DSState::kickSignalHandler(int sigNum, DSValue &&func) {
    this->reserveLocalStack(3);
    this->push(this->getGlobal(BuiltinVarOffset::EXIT_STATUS));
    this->push(std::move(func));
    this->push(DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Signal), sigNum));

    return this->windStackFrame(3, 3, &signalTrampoline);
}

bool DSState::checkVMEvent() {
    if(hasFlag(DSState::eventDesc, DSState::VM_EVENT_SIGNAL) &&
            !hasFlag(DSState::eventDesc, DSState::VM_EVENT_MASK)) {
        SignalGuard guard;

        int sigNum = DSState::pendingSigSet.popPendingSig();
        if(DSState::pendingSigSet.empty()) {
            DSState::pendingSigSet.clear();
            unsetFlag(DSState::eventDesc, DSState::VM_EVENT_SIGNAL);
        }

        auto handler = this->sigVector.lookup(sigNum);
        if(handler != nullptr) {
            setFlag(DSState::eventDesc, DSState::VM_EVENT_MASK);
            if(!this->kickSignalHandler(sigNum, std::move(handler))) {
                return false;
            }
        }
    }

    if(this->hook != nullptr) {
        assert(hasFlag(DSState::eventDesc, DSState::VM_EVENT_HOOK));
        auto op = static_cast<OpCode>(GET_CODE(*this)[this->pc() + 1]);
        this->hook->vmFetchHook(*this, op);
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

bool DSState::mainLoop() {
    while(true) {
        if(DSState::eventDesc != 0u) {
            TRY(this->checkVMEvent());
        }

        // fetch next opcode
        const auto op = static_cast<OpCode>(GET_CODE(*this)[++this->pc()]);

        // dispatch instruction
        vmdispatch(op) {
        vmcase(HALT) {
            this->unwindStackFrame();
            return true;
        }
        vmcase(ASSERT) {
            TRY(this->checkAssertion());
            vmnext;
        }
        vmcase(PRINT) {
            unsigned int v = read32(GET_CODE(*this), this->pc() + 1);
            this->pc() += 4;

            auto &stackTopType = this->symbolTable.get(v);
            assert(!stackTopType.isVoidType());
            auto *strObj = typeAs<String_Object>(this->peek());
            std::string value = "(";
            value += this->symbolTable.getTypeName(stackTopType);
            value += ") ";
            value.append(strObj->getValue(), strObj->size());
            value += "\n";
            fwrite(value.c_str(), sizeof(char), value.size(), stdout);
            fflush(stdout);
            this->popNoReturn();
            vmnext;
        }
        vmcase(INSTANCE_OF) {
            unsigned int v = read32(GET_CODE(*this), this->pc() + 1);
            this->pc() += 4;

            auto &targetType = this->symbolTable.get(v);
            if(this->pop()->introspect(*this, &targetType)) {
                this->push(this->trueObj);
            } else {
                this->push(this->falseObj);
            }
            vmnext;
        }
        vmcase(CHECK_CAST) {
            unsigned int v = read32(GET_CODE(*this), this->pc() + 1);
            this->pc() += 4;
            TRY(this->checkCast(&this->symbolTable.get(v)));
            vmnext;
        }
        vmcase(PUSH_NULL) {
            this->push(nullptr);
            vmnext;
        }
        vmcase(PUSH_TRUE) {
            this->push(this->trueObj);
            vmnext;
        }
        vmcase(PUSH_FALSE) {
            this->push(this->falseObj);
            vmnext;
        }
        vmcase(PUSH_ESTRING) {
            this->push(this->emptyStrObj);
            vmnext;
        }
        vmcase(LOAD_CONST) {
            unsigned char index = read8(GET_CODE(*this), ++this->pc());
            this->push(CONST_POOL(*this)[index]);
            vmnext;
        }
        vmcase(LOAD_CONST_W) {
            unsigned short index = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            this->push(CONST_POOL(*this)[index]);
            vmnext;
        }
        vmcase(LOAD_CONST_T) {
            unsigned int index = read24(GET_CODE(*this), this->pc() + 1);
            this->pc() += 3;
            this->push(CONST_POOL(*this)[index]);
            vmnext;
        }
        vmcase(LOAD_GLOBAL) {
            unsigned short index = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            this->loadGlobal(index);
            vmnext;
        }
        vmcase(STORE_GLOBAL) {
            unsigned short index = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            this->storeGlobal(index);
            vmnext;
        }
        vmcase(LOAD_LOCAL) {
            unsigned char index = read8(GET_CODE(*this), ++this->pc());
            this->loadLocal(index);
            vmnext;
        }
        vmcase(STORE_LOCAL) {
            unsigned char index = read8(GET_CODE(*this), ++this->pc());
            this->storeLocal(index);
            vmnext;
        }
        vmcase(LOAD_FIELD) {
            unsigned short index = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            this->loadField(index);
            vmnext;
        }
        vmcase(STORE_FIELD) {
            unsigned short index = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            this->storeField(index);
            vmnext;
        }
        vmcase(IMPORT_ENV) {
            unsigned char b = read8(GET_CODE(*this), ++this->pc());
            TRY(this->loadEnv(b > 0));
            vmnext;
        }
        vmcase(LOAD_ENV) {
            const char *value = this->loadEnv(false);
            TRY(value);
            this->push(DSValue::create<String_Object>(this->symbolTable.get(TYPE::String), value));
            vmnext;
        }
        vmcase(STORE_ENV) {
            DSValue value = this->pop();
            DSValue name = this->pop();

            setenv(typeAs<String_Object>(name)->getValue(),
                   typeAs<String_Object>(value)->getValue(), 1);//FIXME: check return value and throw
            vmnext;
        }
        vmcase(POP) {
            this->popNoReturn();
            vmnext;
        }
        vmcase(DUP) {
            this->dup();
            vmnext;
        }
        vmcase(DUP2) {
            this->dup2();
            vmnext;
        }
        vmcase(SWAP) {
            this->swap();
            vmnext;
        }
        vmcase(NEW_STRING) {
            this->push(DSValue::create<String_Object>(this->symbolTable.get(TYPE::String)));
            vmnext;
        }
        vmcase(APPEND_STRING) {
            DSValue v = this->pop();
            typeAs<String_Object>(this->peek())->append(std::move(v));
            vmnext;
        }
        vmcase(NEW_ARRAY) {
            unsigned int v = read32(GET_CODE(*this), this->pc() + 1);
            this->pc() += 4;
            this->push(DSValue::create<Array_Object>(this->symbolTable.get(v)));
            vmnext;
        }
        vmcase(APPEND_ARRAY) {
            DSValue v = this->pop();
            typeAs<Array_Object>(this->peek())->append(std::move(v));
            vmnext;
        }
        vmcase(NEW_MAP) {
            unsigned int v = read32(GET_CODE(*this), this->pc() + 1);
            this->pc() += 4;
            this->push(DSValue::create<Map_Object>(this->symbolTable.get(v)));
            vmnext;
        }
        vmcase(APPEND_MAP) {
            DSValue value = this->pop();
            DSValue key = this->pop();
            typeAs<Map_Object>(this->peek())->set(std::move(key), std::move(value));
            vmnext;
        }
        vmcase(NEW_TUPLE) {
            unsigned int v = read32(GET_CODE(*this), this->pc() + 1);
            this->pc() += 4;
            this->push(DSValue::create<Tuple_Object>(this->symbolTable.get(v)));
            vmnext;
        }
        vmcase(NEW) {
            unsigned int v = read32(GET_CODE(*this), this->pc() + 1);
            this->pc() += 4;

            auto &type = this->symbolTable.get(v);
            if(!type.isRecordType()) {
                this->push(DSValue::create<DSObject>(type));
            } else {
                fatal("currently, DSObject allocation not supported\n");
            }
            vmnext;
        }
        vmcase(CALL_INIT) {
            unsigned short paramSize = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            TRY(this->prepareConstructorCall(paramSize));
            vmnext;
        }
        vmcase(CALL_METHOD) {
            unsigned short paramSize = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            unsigned short index = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            TRY(this->prepareMethodCall(index, paramSize));
            vmnext;
        }
        vmcase(CALL_FUNC) {
            unsigned short paramSize = read16(GET_CODE(*this), this->pc() + 1);
            this->pc() += 2;
            TRY(this->prepareFuncCall(paramSize));
            vmnext;
        }
        vmcase(CALL_NATIVE) {
            unsigned long v = read64(GET_CODE(*this), this->pc() + 1);
            this->pc() += 8;
            auto func = (native_func_t) v;
            DSValue returnValue = func(*this);
            TRY(!this->getThrownObject());
            if(returnValue) {
                this->push(std::move(returnValue));
            }
            vmnext;
        }
        vmcase(INIT_MODULE) {
            auto &code = typeAs<FuncObject>(this->peek())->getCode();
            this->windStackFrame(0, 0, &code);
            vmnext;
        }
        vmcase(RETURN) {
            this->unwindStackFrame();
            if(this->checkVMReturn()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_V) {
            DSValue v = this->pop();
            this->unwindStackFrame();
            this->push(std::move(v));
            if(this->checkVMReturn()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_UDC) {
            auto v = this->pop();
            this->unwindStackFrame();
            this->pushExitStatus(typeAs<Int_Object>(v)->getValue());
            if(this->checkVMReturn()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_SIG) {
            auto v = this->getLocal(0);   // old exit status
            this->unwindStackFrame();
            unsetFlag(DSState::eventDesc, DSState::VM_EVENT_MASK);
            this->setGlobal(toIndex(BuiltinVarOffset::EXIT_STATUS), std::move(v));
            vmnext;
        }
        vmcase(BRANCH) {
            unsigned short offset = read16(GET_CODE(*this), this->pc() + 1);
            if(typeAs<Boolean_Object>(this->pop())->getValue()) {
                this->pc() += 2;
            } else {
                this->pc() += offset - 1;
            }
            vmnext;
        }
        vmcase(GOTO) {
            unsigned int index = read32(GET_CODE(*this), this->pc() + 1);
            this->pc() = index - 1;
            vmnext;
        }
        vmcase(THROW) {
            auto obj = this->pop();
            this->throwObject(std::move(obj), 1);
            vmerror;
        }
        vmcase(ENTER_FINALLY) {
            const unsigned int offset = read16(GET_CODE(*this), this->pc() + 1);
            const unsigned int savedIndex = this->pc() + 2;
            this->push(DSValue::createNum(savedIndex));
            this->pc() += offset - 1;
            vmnext;
        }
        vmcase(EXIT_FINALLY) {
            switch(this->peek().kind()) {
            case DSValueKind::OBJECT:
            case DSValueKind::INVALID: {
                this->storeThrownObject();
                vmerror;
            }
            case DSValueKind::NUMBER: {
                unsigned int index = static_cast<unsigned int>(this->pop().value());
                this->pc() = index;
                vmnext;
            }
            }
            vmnext;
        }
        vmcase(LOOKUP_HASH) {
            auto key = this->pop();
            auto map = this->pop();
            auto &valueMap = typeAs<Map_Object>(map)->getValueMap();
            auto iter = valueMap.find(key);
            if(iter != valueMap.end()) {
                unsigned int index = typeAs<Int_Object>(iter->second)->getValue();
                this->pc() = index - 1;
            }
            vmnext;
        }
        vmcase(COPY_INT) {
            DSType *type = this->symbolTable.getByNumTypeIndex(read8(GET_CODE(*this), ++this->pc()));
            int v = typeAs<Int_Object>(this->pop())->getValue();
            this->push(DSValue::create<Int_Object>(*type, v));
            vmnext;
        }
        vmcase(TO_BYTE) {
            unsigned int v = typeAs<Int_Object>(this->pop())->getValue();
            v &= 0xFF;  // fill higher bits (8th ~ 31) with 0
            this->push(DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Byte), v));
            vmnext;
        }
        vmcase(TO_U16) {
            unsigned int v = typeAs<Int_Object>(this->pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            this->push(DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Uint16), v));
            vmnext;
        }
        vmcase(TO_I16) {
            unsigned int v = typeAs<Int_Object>(this->pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            if((v & 0x8000) != 0u) {    // if 15th bit is 1, fill higher bits with 1
                v |= 0xFFFF0000;
            }
            this->push(DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Int16), v));
            vmnext;
        }
        vmcase(NEW_LONG) {
            DSType *type = this->symbolTable.getByNumTypeIndex(read8(GET_CODE(*this), ++this->pc()));
            unsigned int v = typeAs<Int_Object>(this->pop())->getValue();
            unsigned long l = v;
            this->push(DSValue::create<Long_Object>(*type, l));
            vmnext;
        }
        vmcase(COPY_LONG) {
            DSType *type = this->symbolTable.getByNumTypeIndex(read8(GET_CODE(*this), ++this->pc()));
            long v = typeAs<Long_Object>(this->pop())->getValue();
            this->push(DSValue::create<Long_Object>(*type, v));
            vmnext;
        }
        vmcase(I_NEW_LONG) {
            DSType *type = this->symbolTable.getByNumTypeIndex(read8(GET_CODE(*this), ++this->pc()));
            int v = typeAs<Int_Object>(this->pop())->getValue();
            long l = v;
            this->push(DSValue::create<Long_Object>(*type, l));
            vmnext;
        }
        vmcase(NEW_INT) {
            DSType *type = this->symbolTable.getByNumTypeIndex(read8(GET_CODE(*this), ++this->pc()));
            unsigned long l = typeAs<Long_Object>(this->pop())->getValue();
            auto v = static_cast<unsigned int>(l);
            this->push(DSValue::create<Int_Object>(*type, v));
            vmnext;
        }
        vmcase(U32_TO_D) {
            unsigned int v = typeAs<Int_Object>(this->pop())->getValue();
            auto d = static_cast<double>(v);
            this->push(DSValue::create<Float_Object>(this->symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(I32_TO_D) {
            int v = typeAs<Int_Object>(this->pop())->getValue();
            auto d = static_cast<double>(v);
            this->push(DSValue::create<Float_Object>(this->symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(U64_TO_D) {
            unsigned long v = typeAs<Long_Object>(this->pop())->getValue();
            auto d = static_cast<double>(v);
            this->push(DSValue::create<Float_Object>(this->symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(I64_TO_D) {
            long v = typeAs<Long_Object>(this->pop())->getValue();
            auto d = static_cast<double>(v);
            this->push(DSValue::create<Float_Object>(this->symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(D_TO_U32) {
            double d = typeAs<Float_Object>(this->pop())->getValue();
            auto v = static_cast<unsigned int>(d);
            this->push(DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Uint32), v));
            vmnext;
        }
        vmcase(D_TO_I32) {
            double d = typeAs<Float_Object>(this->pop())->getValue();
            auto v = static_cast<int>(d);
            this->push(DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Int32), v));
            vmnext;
        }
        vmcase(D_TO_U64) {
            double d = typeAs<Float_Object>(this->pop())->getValue();
            auto v = static_cast<unsigned long>(d);
            this->push(DSValue::create<Long_Object>(this->symbolTable.get(TYPE::Uint64), v));
            vmnext;
        }
        vmcase(D_TO_I64) {
            double d = typeAs<Float_Object>(this->pop())->getValue();
            auto v = static_cast<long>(d);
            this->push(DSValue::create<Long_Object>(this->symbolTable.get(TYPE::Int64), v));
            vmnext;
        }
        vmcase(REF_EQ) {
            auto v1 = this->pop();
            auto v2 = this->pop();
            this->push(v1 == v2 ? this->trueObj : this->falseObj);
            vmnext;
        }
        vmcase(REF_NE) {
            auto v1 = this->pop();
            auto v2 = this->pop();
            this->push(v1 != v2 ? this->trueObj : this->falseObj);
            vmnext;
        }
        vmcase(FORK) {
            TRY(this->forkAndEval());
            vmnext;
        }
        vmcase(PIPELINE)
        vmcase(PIPELINE_LP) {
            bool lastPipe = op == OpCode::PIPELINE_LP;
            TRY(this->callPipeline(lastPipe));
            vmnext;
        }
        vmcase(EXPAND_TILDE) {
            std::string str = typeAs<String_Object>(this->pop())->getValue();
            expandTilde(str);
            this->push(DSValue::create<String_Object>(this->symbolTable.get(TYPE::String), std::move(str)));
            vmnext;
        }
        vmcase(NEW_CMD) {
            auto v = this->pop();
            auto obj = DSValue::create<Array_Object>(this->symbolTable.get(TYPE::StringArray));
            auto *argv = typeAs<Array_Object>(obj);
            argv->append(std::move(v));
            this->push(std::move(obj));
            vmnext;
        }
        vmcase(ADD_CMD_ARG) {
            unsigned char v = read8(GET_CODE(*this), ++this->pc());
            this->addCmdArg(v > 0);
            vmnext;
        }
        vmcase(CALL_CMD)
        vmcase(CALL_CMD_P) {
            bool needFork = op != OpCode::CALL_CMD_P;
            flag8_set_t attr = 0;
            if(needFork) {
                setFlag(attr, UDC_ATTR_NEED_FORK);
            }

            auto redir = this->pop();
            auto argv = this->pop();

            const char *cmdName = str(typeAs<Array_Object>(argv)->getValues()[0]);
            TRY(this->callCommand(CmdResolver()(*this, cmdName), std::move(argv), std::move(redir), attr));
            vmnext;
        }
        vmcase(BUILTIN_CMD) {
            auto attr = this->getLocal(UDC_PARAM_ATTR).value();
            DSValue redir = this->getLocal(UDC_PARAM_REDIR);
            DSValue argv = this->getLocal(UDC_PARAM_ARGV);
            bool ret = this->callBuiltinCommand(std::move(argv), std::move(redir), attr);
            flushStdFD();
            TRY(ret);
            vmnext;
        }
        vmcase(BUILTIN_EVAL) {
            auto attr = this->getLocal(UDC_PARAM_ATTR).value();
            DSValue redir = this->getLocal(UDC_PARAM_REDIR);
            DSValue argv = this->getLocal(UDC_PARAM_ARGV);

            eraseFirst(*typeAs<Array_Object>(argv));
            auto *array = typeAs<Array_Object>(argv);
            if(!array->getValues().empty()) {
                const char *cmdName = str(typeAs<Array_Object>(argv)->getValues()[0]);
                TRY(this->callCommand(CmdResolver()(*this, cmdName), std::move(argv), std::move(redir), attr));
            } else {
                this->pushExitStatus(0);
            }
            vmnext;
        }
        vmcase(BUILTIN_EXEC) {
            DSValue redir = this->getLocal(UDC_PARAM_REDIR);
            DSValue argv = this->getLocal(UDC_PARAM_ARGV);
            this->callBuiltinExec(std::move(argv), std::move(redir));
            vmnext;
        }
        vmcase(NEW_REDIR) {
            this->push(DSValue::create<RedirConfig>());
            vmnext;
        }
        vmcase(ADD_REDIR_OP) {
            unsigned char v = read8(GET_CODE(*this), ++this->pc());
            auto value = this->pop();
            typeAs<RedirConfig>(this->peek())->addRedirOp(static_cast<RedirOP>(v), std::move(value));
            vmnext;
        }
        vmcase(DO_REDIR) {
            TRY(typeAs<RedirConfig>(this->peek())->redirect(*this));
            vmnext;
        }
        vmcase(RAND) {
            std::random_device rand;
            std::default_random_engine engine(rand());
            std::uniform_int_distribution<unsigned int> dist;
            unsigned int v = dist(engine);
            this->push(DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Uint32), v));
            vmnext;
        }
        vmcase(GET_SECOND) {
            auto now = std::chrono::system_clock::now();
            auto diff = now - this->baseTime;
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(diff);
            unsigned long v = typeAs<Long_Object>(this->getGlobal(BuiltinVarOffset::SECONDS))->getValue();
            v += sec.count();
            this->push(DSValue::create<Long_Object>(this->symbolTable.get(TYPE::Uint64), v));
            vmnext;
        }
        vmcase(SET_SECOND) {
            this->baseTime = std::chrono::system_clock::now();
            this->storeGlobal(toIndex(BuiltinVarOffset::SECONDS));
            vmnext;
        }
        vmcase(UNWRAP) {
            if(this->peek().kind() == DSValueKind::INVALID) {
                raiseError(*this, TYPE::UnwrappingError, std::string("invalid value"));
                vmerror;
            }
            vmnext;
        }
        vmcase(CHECK_UNWRAP) {
            bool b = this->pop().kind() != DSValueKind::INVALID;
            this->push(b ? this->trueObj : this->falseObj);
            vmnext;
        }
        vmcase(TRY_UNWRAP) {
            unsigned short offset = read16(GET_CODE(*this), this->pc() + 1);
            if(this->peek().kind() == DSValueKind::INVALID) {
                this->popNoReturn();
                this->pc() += 2;
            } else {
                this->pc() += offset - 1;
            }
            vmnext;
        }
        vmcase(NEW_INVALID) {
            this->push(DSValue::createInvalid());
            vmnext;
        }
        vmcase(RECLAIM_LOCAL) {
            unsigned char offset = read8(GET_CODE(*this), ++this->pc());
            unsigned char size = read8(GET_CODE(*this), ++this->pc());

            this->reclaimLocals(offset, size);
            vmnext;
        }
        }
    }
}

bool DSState::handleException(bool forceUnwind) {
    if(this->hook != nullptr) {
        this->hook->vmThrowHook(*this);
    }

    for(; !this->checkVMReturn(); this->unwindStackFrame()) {
        if(!forceUnwind && !CODE(*this)->is(CodeKind::NATIVE)) {
            auto *cc = static_cast<const CompiledCode *>(CODE(*this));

            // search exception entry
            const unsigned int occurredPC = this->pc();
            const DSType *occurredType = this->getThrownObject()->getType();

            for(unsigned int i = 0; cc->getExceptionEntries()[i].type != nullptr; i++) {
                const ExceptionEntry &entry = cc->getExceptionEntries()[i];
                if(occurredPC >= entry.begin && occurredPC < entry.end
                   && entry.type->isSameOrBaseTypeOf(*occurredType)) {
                    if(entry.type->is(TYPE::_Root)) {
                        return false;
                    }
                    this->pc() = entry.dest - 1;
                    this->clearOperandStack();
                    this->reclaimLocals(entry.localOffset, entry.localSize);
                    this->loadThrownObject();
                    return true;
                }
            }
        } else if(CODE(*this) == &signalTrampoline) {   // within signal trampoline
            unsetFlag(DSState::eventDesc, DSState::VM_EVENT_MASK);
        }
    }
    return false;
}

bool DSState::runMainLoop() {
    while(!this->mainLoop()) {
        bool forceUnwind = this->symbolTable.get(TYPE::_InternalStatus)
                .isSameOrBaseTypeOf(*this->getThrownObject()->getType());
        if(!this->handleException(forceUnwind)) {
            return false;
        }
    }
    return true;
}

bool DSState::vmEval(const CompiledCode &code) {
    this->clearThrownObject();
    this->reserveGlobalVar();

    this->windStackFrame(0, 0, &code);

    return this->runMainLoop();
}

bool DSState::execCommand(char *const *argv) {
    auto cmd = CmdResolver()(*this, argv[0]);

    // init parameter
    std::vector<DSValue> values;
    for(; *argv != nullptr; argv++) {
        values.push_back(DSValue::create<String_Object>(this->symbolTable.get(TYPE::String), std::string(*argv)));
    }
    auto obj = DSValue::create<Array_Object>(this->symbolTable.get(TYPE::StringArray), std::move(values));

    this->clearThrownObject();
    bool ret = this->callCommand(cmd, std::move(obj), DSValue());
    if(ret) {
        if(!this->controlStack.empty()) {
            ret = this->runMainLoop();
        }
    }
    this->clearOperandStack();
    return ret;
}

unsigned int DSState::prepareArguments(DSValue &&recv,
                                       std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
    this->clearThrownObject();

    // push arguments
    unsigned int size = args.first;
    this->reserveLocalStack(size + 1);
    this->push(std::move(recv));
    for(unsigned int i = 0; i < size; i++) {
        this->push(std::move(args.second[i]));
    }
    return size;
}

class RecursionGuard {
private:
    static constexpr unsigned int LIMIT = 256;
    DSState &state;

public:
    explicit RecursionGuard(DSState &state) : state(state) {
        this->state.recDepth()++;
    }

    ~RecursionGuard() {
        this->state.recDepth()--;
    }

    bool checkLimit() {
        if(this->state.recDepth() == LIMIT) {
            raiseError(this->state, TYPE::StackOverflowError,
                       "interpreter recursion depth reaches limit");
            return false;
        }
        return true;
    }
};

#define GUARD_RECURSION(state) \
    RecursionGuard _guard(state); \
    do { if(!_guard.checkLimit()) { return nullptr; } } while(false)


DSValue DSState::callMethod(const MethodHandle *handle, DSValue &&recv,
                            std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
    assert(handle != nullptr);
    assert(handle->getParamTypes().size() == args.first);

    GUARD_RECURSION(*this);

    unsigned int size = this->prepareArguments(std::move(recv), std::move(args));

    DSValue ret;
    if(this->prepareMethodCall(handle->getMethodIndex(), size)) {
        bool s = this->runMainLoop();
        if(!handle->getReturnType()->isVoidType() && s) {
            ret = this->pop();
        }
    }
    return ret;
}

DSValue DSState::callFunction(DSValue &&funcObj, std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
    GUARD_RECURSION(*this);

    auto *type = funcObj->getType();
    unsigned int size = this->prepareArguments(std::move(funcObj), std::move(args));

    DSValue ret;
    if(this->prepareFuncCall(size)) {
        bool s = this->runMainLoop();
        assert(type->isFuncType());
        if(!static_cast<FunctionType *>(type)->getReturnType()->isVoidType() && s) {
            ret = this->pop();
        }
    }
    return ret;
}