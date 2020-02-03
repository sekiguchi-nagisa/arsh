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
#include "misc/num_util.hpp"

// #####################
// ##     DSState     ##
// #####################

VMEvent DSState::eventDesc{};

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
        logicalWorkingDir(initLogicalWorkingDir()),
        baseTime(std::chrono::system_clock::now()) { }


void DSState::updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus) {
    auto *obj = typeAs<Array_Object>(this->getGlobal(BuiltinVarOffset::PIPESTATUS));
    obj->refValues().clear();
    obj->refValues().reserve(size + (mergeExitStatus ? 1 : 0));

    auto &type = *static_cast<ReifiedType *>(obj->getType())->getElementTypes()[0];
    for(unsigned int i = 0; i < size; i++) {
        obj->refValues().push_back(DSValue::create<Int_Object>(type, procs[i].exitStatus()));
    }
    if(mergeExitStatus) {
        obj->refValues().push_back(this->getGlobal(BuiltinVarOffset::EXIT_STATUS));
    }
}

namespace ydsh {

bool VM::checkCast(DSState &state, DSType *targetType) {
    if(!state.stack.peek()->introspect(state, targetType)) {
        DSType *stackTopType = state.stack.pop()->getType();
        std::string str("cannot cast `");
        str += state.symbolTable.getTypeName(*stackTopType);
        str += "' to `";
        str += state.symbolTable.getTypeName(*targetType);
        str += "'";
        raiseError(state, TYPE::TypeCastError, std::move(str));
        return false;
    }
    return true;
}

bool VM::checkAssertion(DSState &state) {
    auto msg(state.stack.pop());
    assert(typeAs<String_Object>(msg)->getValue() != nullptr);

    if(!typeAs<Boolean_Object>(state.stack.pop())->getValue()) {
        raiseError(state, TYPE::_AssertFail, std::string(typeAs<String_Object>(msg)->getValue()));
        return false;
    }
    return true;
}

const char *VM::loadEnv(DSState &state, bool hasDefault) {
    DSValue dValue;
    if(hasDefault) {
        dValue = state.stack.pop();
    }
    DSValue nameObj(state.stack.pop());
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

bool VM::prepareUserDefinedCommandCall(DSState &state, const DSCode *code, DSValue &&argvObj,
                                            DSValue &&restoreFD, const flag8_set_t attr) {
    if(hasFlag(attr, UDC_ATTR_SETVAR)) {
        // reset exit status
        state.updateExitStatus(0);
    }

    // set parameter
    state.stack.reserve(3);
    state.stack.push(DSValue::createNum(attr));
    state.stack.push(std::move(restoreFD));
    state.stack.push(std::move(argvObj));

    if(!windStackFrame(state, 3, 3, code)) {
        return false;
    }

    if(hasFlag(attr, UDC_ATTR_SETVAR)) {    // set variable
        auto argv = typeAs<Array_Object>(state.stack.getLocal(UDC_PARAM_ARGV));
        auto cmdName = argv->takeFirst();
        const unsigned int argSize = argv->getValues().size();
        state.stack.setLocal(UDC_PARAM_ARGV + 1,
                             DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), argSize));   // #
        state.stack.setLocal(UDC_PARAM_ARGV + 2, std::move(cmdName)); // 0
        unsigned int limit = 9;
        if(argSize < limit) {
            limit = argSize;
        }

        unsigned int index = 0;
        for(; index < limit; index++) {
            state.stack.setLocal(index + UDC_PARAM_ARGV + 3, argv->getValues()[index]);
        }

        for(; index < 9; index++) {
            state.stack.setLocal(index + UDC_PARAM_ARGV + 3, state.emptyStrObj);  // set remain
        }
    }
    return true;
}

#define CODE(ctx) ((ctx).stack.code())
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
            int ch = buf[i];
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
            str += static_cast<char>(ch);
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

bool VM::forkAndEval(DSState &state) {
    const auto forkKind = static_cast<ForkKind >(read8(GET_CODE(state), state.stack.pc() + 1));
    const unsigned short offset = read16(GET_CODE(state), state.stack.pc() + 2);

    // flush standard stream due to prevent mixing io buffer
    flushStdFD();

    // set in/out pipe
    auto pipeset = initPipeSet(forkKind);
    const bool rootShell = state.isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    auto proc = Proc::fork(state, pgid, needForeground(forkKind) && rootShell);
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
            auto waitOp = state.isRootShell() && state.isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
            int ret = proc.wait(waitOp);   // wait exit
            tryToClose(pipeset.out[READ_PIPE]); // close read pipe after wait, due to prevent EPIPE
            if(proc.state() != Proc::TERMINATED) {
                state.jobTable.attach(JobTable::create(
                        state.symbolTable.get(TYPE::Job), proc,
                        DSValue(state.emptyFDObj),
                        DSValue(state.emptyFDObj)));
            }
            state.updateExitStatus(ret);
            tryToBeForeground(state);
            break;
        }
        case ForkKind::IN_PIPE:
        case ForkKind::OUT_PIPE: {
            int &fd = forkKind == ForkKind::IN_PIPE ? pipeset.in[WRITE_PIPE] : pipeset.out[READ_PIPE];
            obj = newFD(state, fd);
            auto entry = JobTable::create(
                    state.symbolTable.get(TYPE::Job), proc,
                    DSValue(forkKind == ForkKind::IN_PIPE ? obj : state.emptyFDObj),
                    DSValue(forkKind == ForkKind::OUT_PIPE ? obj : state.emptyFDObj));
            state.jobTable.attach(entry, true);
            break;
        }
        case ForkKind::COPROC:
        case ForkKind::JOB:
        case ForkKind::DISOWN: {
            bool disown = forkKind == ForkKind::DISOWN;
            auto entry = JobTable::create(
                    state.symbolTable.get(TYPE::Job), proc,
                    newFD(state, pipeset.in[WRITE_PIPE]),
                    newFD(state, pipeset.out[READ_PIPE]));
            state.jobTable.attach(entry, disown);
            obj = DSValue(entry.get());
            break;
        }
        }

        // push object
        if(obj) {
            state.stack.push(std::move(obj));
        }

        state.stack.pc() += offset - 1;
    } else if(proc.pid() == 0) {   // child process
        tryToDup(pipeset.in[READ_PIPE], STDIN_FILENO);
        tryToClose(pipeset.in);
        tryToDup(pipeset.out[WRITE_PIPE], STDOUT_FILENO);
        tryToClose(pipeset.out);

        if(forkKind == ForkKind::DISOWN || (forkKind == ForkKind::JOB && !state.isJobControl())) {
            redirInToNull();
        }

        state.stack.pc() += 3;
    } else {
        raiseSystemError(state, EAGAIN, "fork failed");
        return false;
    }
    return true;
}

/* for pipeline evaluation */
static NativeCode initCode(OpCode op) {
    auto *code = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * 3));
    code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
    code[1] = static_cast<unsigned char>(op);
    code[2] = static_cast<unsigned char>(OpCode::RETURN_V);
    return NativeCode(code);
}

static const DSCode *lookupUdc(const DSState &state, const char *name) {
    auto handle = state.symbolTable.lookupUdc(name);
    auto *udcObj = handle != nullptr ?
            &typeAs<FuncObject>(state.getGlobal(handle->getIndex()))->getCode() : nullptr;
    return udcObj;
}

Command CmdResolver::operator()(DSState &state, const char *cmdName) const {
    Command cmd{};

    // first, check user-defined command
    if(!hasFlag(this->mask, MASK_UDC)) {
        auto *udcObj = lookupUdc(state, cmdName);
        if(udcObj != nullptr) {
            cmd.kind = Command::USER_DEFINED;
            cmd.udc = udcObj;
            return cmd;
        }
    }

    // second, check builtin command
    {
        builtin_command_t bcmd = lookupBuiltinCommand(cmdName);
        if(bcmd != nullptr) {
            cmd.kind = Command::BUILTIN;
            cmd.builtinCmd = bcmd;
            return cmd;
        }

        static std::pair<const char *, NativeCode> sb[] = {
                {"command", initCode(OpCode::BUILTIN_CMD)},
                {"eval",    initCode(OpCode::BUILTIN_EVAL)},
                {"exec",    initCode(OpCode::BUILTIN_EXEC)},
        };
        for(auto &e : sb) {
            if(strcmp(cmdName, e.first) == 0) {
                cmd.kind = Command::BUILTIN_S;
                cmd.udc = &e.second;
                return cmd;
            }
        }
    }

    // resolve external command path
    cmd.kind = Command::EXTERNAL;
    cmd.filePath = nullptr;
    if(!hasFlag(this->mask, MASK_EXTERNAL)) {
        cmd.filePath = state.pathCache.searchPath(cmdName, this->searchOp);

        // if command not found or directory, lookup _cmd_fallback_handler
        if(hasFlag(this->mask, MASK_FALLBACK)) {
            return cmd;
        }
        if(cmd.filePath == nullptr || S_ISDIR(getStMode(cmd.filePath))) {
            auto handle = state.symbolTable.lookupHandle(VAR_CMD_FALLBACK);
            unsigned int index = handle->getIndex();
            if(state.getGlobal(index).isValidObject()) {
                cmd.kind = Command::USER_DEFINED;
                cmd.udc = lookupUdc(state, CMD_FALLBACK_HANDLER);
                assert(cmd.udc);
            }
        }
    }
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

int VM::forkAndExec(DSState &state, const char *filePath, char *const *argv, DSValue &&redirConfig) {
    // setup self pipe
    int selfpipe[2];
    if(pipe(selfpipe) < 0) {
        fatal_perror("pipe creation error");
    }
    if(fcntl(selfpipe[WRITE_PIPE], F_SETFD, fcntl(selfpipe[WRITE_PIPE], F_GETFD) | FD_CLOEXEC) != 0) {
        fatal_perror("fcntl error");
    }

    bool rootShell = state.isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    auto proc = Proc::fork(state, pgid, rootShell);
    if(proc.pid() == -1) {
        raiseCmdError(state, argv[0], EAGAIN);
        return 1;
    } else if(proc.pid() == 0) {   // child
        close(selfpipe[READ_PIPE]);
        xexecve(filePath, argv, nullptr, redirConfig);

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
            state.jobTable.attach(JobTable::create(
                    state.symbolTable.get(TYPE::Job), proc,
                    DSValue(state.emptyFDObj),
                    DSValue(state.emptyFDObj)));
        }
        int ret = tryToBeForeground(state);
        LOG(DUMP_EXEC, "tryToBeForeground: %d, %s", ret, strerror(errno));
        state.jobTable.updateStatus();
        if(errnum != 0) {
            raiseCmdError(state, argv[0], errnum);
            return 1;
        }
        return status;
    }
}

bool VM::callCommand(DSState &state, CmdResolver resolver, DSValue &&argvObj, DSValue &&redirConfig, flag8_set_t attr) {
    auto *array = typeAs<Array_Object>(argvObj);
    auto cmd = resolver(state, str(array->getValues()[0]));

    switch(cmd.kind) {
    case Command::USER_DEFINED:
    case Command::BUILTIN_S: {
        if(cmd.kind == Command::USER_DEFINED) {
            setFlag(attr, UDC_ATTR_SETVAR);
        }
        return prepareUserDefinedCommandCall(state, cmd.udc, std::move(argvObj), std::move(redirConfig), attr);
    }
    case Command::BUILTIN: {
        int status = cmd.builtinCmd(state, *array);
        flushStdFD();
        if(state.hasError()) {
            return false;
        }
        pushExitStatus(state, status);
        return true;
    }
    case Command::EXTERNAL: {
        // create argv
        const unsigned int size = array->getValues().size();
        char *argv[size + 1];
        for(unsigned int i = 0; i < size; i++) {
            argv[i] = const_cast<char *>(str(array->getValues()[i]));
        }
        argv[size] = nullptr;

        if(hasFlag(attr, UDC_ATTR_NEED_FORK)) {
            int status = forkAndExec(state, cmd.filePath, argv, std::move(redirConfig));
            pushExitStatus(state, status);
        } else {
            xexecve(cmd.filePath, argv, nullptr, redirConfig);
            raiseCmdError(state, argv[0], errno);
        }
        return !state.hasError();
    }
    }
    return true;    // normally unreachable, but need to suppress gcc warning.
}


int invalidOptionError(const Array_Object &obj, const GetOptState &s);

const NativeCode *getNativeCode(unsigned int index);

bool VM::callBuiltinCommand(DSState &state, DSValue &&argvObj, DSValue &&redir, flag8_set_t attr) {
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
            return true;
        }
    }

    unsigned int index = optState.index;
    const unsigned int argc = arrayObj.getValues().size();
    if(index < argc) {
        if(showDesc == 0) { // execute command
            auto &values = arrayObj.refValues();
            values.erase(values.begin(), values.begin() + index);

            auto resolve = CmdResolver(CmdResolver::MASK_UDC,
                                       useDefaultPath ? FilePathCache::USE_DEFAULT_PATH : FilePathCache::NON);
            return callCommand(state, resolve, std::move(argvObj), std::move(redir), attr);
        }

        // show command description
        unsigned int successCount = 0;
        for(; index < argc; index++) {
            const char *commandName = str(arrayObj.getValues()[index]);
            auto cmd = CmdResolver(CmdResolver::MASK_FALLBACK, FilePathCache::DIRECT_SEARCH)(state, commandName);
            switch(cmd.kind) {
            case Command::USER_DEFINED: {
                successCount++;
                fputs(commandName, stdout);
                if(showDesc == 2) {
                    fputs(" is an user-defined command", stdout);
                }
                fputc('\n', stdout);
                continue;
            }
            case Command::BUILTIN_S:
            case Command::BUILTIN: {
                successCount++;
                fputs(commandName, stdout);
                if(showDesc == 2) {
                    fputs(" is a shell builtin command", stdout);
                }
                fputc('\n', stdout);
                continue;
            }
            case Command::EXTERNAL: {
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
        pushExitStatus(state, successCount > 0 ? 0 : 1);
        return true;
    }
    pushExitStatus(state, 0);
    return true;
}

void VM::callBuiltinExec(DSState &state, DSValue &&array, DSValue &&redir) {
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
            pushExitStatus(state, s);
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

        const char *filePath = state.pathCache.searchPath(argv2[0], FilePathCache::DIRECT_SEARCH);
        if(progName != nullptr) {
            argv2[0] = const_cast<char *>(progName);
        }

        char *envp[] = {nullptr};
        xexecve(filePath, argv2, clearEnv ? envp : nullptr, redir);
        PERROR(argvObj, "%s", str(argvObj.getValues()[index]));
        exit(1);
    }
    pushExitStatus(state, 0);
}

bool VM::callPipeline(DSState &state, bool lastPipe) {
    /**
     * ls | grep .
     * ==> pipeSize == 1, procSize == 2
     *
     * if lastPipe is true,
     *
     * ls | { grep . ;}
     * ==> pipeSize == 1, procSize == 1
     */
    const unsigned int procSize = read8(GET_CODE(state), state.stack.pc() + 1) - 1;
    const unsigned int pipeSize = procSize - (lastPipe ? 0 : 1);

    assert(pipeSize > 0);

    int pipefds[pipeSize][2];
    initAllPipe(pipeSize, pipefds);

    // fork
    Proc childs[procSize];
    const bool rootShell = state.isRootShell();
    pid_t pgid = rootShell ? 0 : getpgid(0);
    Proc proc;  //NOLINT

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
        state.stack.pc() += read16(GET_CODE(state), state.stack.pc() + 2 + procIndex * 2) - 1;
    } else if(procIndex == procSize) { // parent (last pipeline)
        /**
         * in last pipe, save current stdin before call dup2
         */
        auto jobEntry = JobTable::create(
                state.symbolTable.get(TYPE::Job),
                procSize, childs, lastPipe,
                DSValue(state.emptyFDObj), DSValue(state.emptyFDObj));

        if(lastPipe) {
            ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
        }
        closeAllPipe(pipeSize, pipefds);

        if(lastPipe) {
            state.stack.push(DSValue::create<PipelineState>(state, std::move(jobEntry)));
        } else {
            // job termination
            auto waitOp = rootShell && state.isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
            int status = jobEntry->wait(waitOp);
            state.updatePipeStatus(jobEntry->getProcSize(), jobEntry->getProcs(), false);
            if(jobEntry->available()) {
                state.jobTable.attach(jobEntry);
            }
            tryToBeForeground(state);
            state.jobTable.updateStatus();
            pushExitStatus(state, status);
        }

        // set pc to next instruction
        state.stack.pc() += read16(GET_CODE(state), state.stack.pc() + 2 + procIndex * 2) - 1;
    } else {
        // force terminate forked process.
        for(unsigned int i = 0; i < procIndex; i++) {
            childs[i].send(SIGKILL);
        }

        raiseSystemError(state, EAGAIN, "fork failed");
        return false;
    }
    return true;
}

void VM::addCmdArg(DSState &state, bool skipEmptyStr) {
    /**
     * stack layout
     *
     * ===========> stack grow
     * +------+-------+-------+
     * | argv | redir | value |
     * +------+-------+-------+
     */
    DSValue value = state.stack.pop();
    DSType *valueType = value->getType();

    auto *argv = typeAs<Array_Object>(state.stack.peekByOffset(1));
    if(valueType->is(TYPE::String)) {  // String
        if(skipEmptyStr && typeAs<String_Object>(value)->empty()) {
            return;
        }
        argv->append(std::move(value));
        return;
    }

    if(valueType->is(TYPE::UnixFD)) { // UnixFD
        if(!state.stack.peek()) {
            state.stack.pop();
            state.stack.push(DSValue::create<RedirConfig>());
        }
        auto fdPath = typeAs<UnixFD_Object>(value)->toString();
        auto strObj = DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), std::move(fdPath));
        typeAs<RedirConfig>(state.stack.peek())->addRedirOp(RedirOP::NOP, std::move(value));
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
    auto *code = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * 8));
    code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
    code[1] = static_cast<unsigned char>(OpCode::LOAD_LOCAL);
    code[2] = static_cast<unsigned char>(1);
    code[3] = static_cast<unsigned char>(OpCode::LOAD_LOCAL);
    code[4] = static_cast<unsigned char>(2);
    code[5] = static_cast<unsigned char>(OpCode::CALL_FUNC);
    code[6] = 1;
    code[7] = static_cast<unsigned char>(OpCode::RETURN_SIG);
    return NativeCode(code);
}

static auto signalTrampoline = initSignalTrampoline();

bool VM::kickSignalHandler(DSState &state, int sigNum, DSValue &&func) {
    state.stack.reserve(3);
    state.stack.push(state.getGlobal(BuiltinVarOffset::EXIT_STATUS));
    state.stack.push(std::move(func));
    state.stack.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Signal), sigNum));

    return windStackFrame(state, 3, 3, &signalTrampoline);
}

bool VM::checkVMEvent(DSState &state) {
    if(hasFlag(DSState::eventDesc, VMEvent::SIGNAL) &&
       !hasFlag(DSState::eventDesc, VMEvent::MASK)) {
        SignalGuard guard;

        int sigNum = DSState::pendingSigSet.popPendingSig();
        if(DSState::pendingSigSet.empty()) {
            DSState::pendingSigSet.clear();
            unsetFlag(DSState::eventDesc, VMEvent::SIGNAL);
        }

        auto handler = state.sigVector.lookup(sigNum);
        if(handler != nullptr) {
            setFlag(DSState::eventDesc, VMEvent::MASK);
            if(!kickSignalHandler(state, sigNum, std::move(handler))) {
                return false;
            }
        }
    }

    if(state.hook != nullptr) {
        assert(hasFlag(DSState::eventDesc, VMEvent::HOOK));
        auto op = static_cast<OpCode>(GET_CODE(state)[state.stack.pc() + 1]);
        state.hook->vmFetchHook(state, op);
    }
    return true;
}

static auto nativeCallDummy = initCode(OpCode::HALT);

#define vmdispatch(V) switch(V)

#if 0
#define vmcase(code) case OpCode::code: {fprintf(stderr, "pc: %u, code: %s\n", state.pc(), #code); }
#else
#define vmcase(code) case OpCode::code:
#endif

#define vmnext continue
#define vmerror goto EXCEPT

#define TRY(E) do { if(!(E)) { vmerror; } } while(false)

bool VM::mainLoop(DSState &state) {
    OpCode op = OpCode::HALT;
    while(true) {
        if(!empty(DSState::eventDesc)) {
            TRY(checkVMEvent(state));
        }

        // fetch next opcode
        op = static_cast<OpCode>(GET_CODE(state)[++state.stack.pc()]);

        // dispatch instruction
        vmdispatch(op) {
        vmcase(HALT) {
            return true;
        }
        vmcase(ASSERT) {
            TRY(checkAssertion(state));
            vmnext;
        }
        vmcase(PRINT) {
            unsigned int v = read32(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 4;

            auto &stackTopType = state.symbolTable.get(v);
            assert(!stackTopType.isVoidType());
            auto *strObj = typeAs<String_Object>(state.stack.peek());
            std::string value = ": ";
            value += state.symbolTable.getTypeName(stackTopType);
            value += " = ";
            value.append(strObj->getValue(), strObj->size());
            value += "\n";
            fwrite(value.c_str(), sizeof(char), value.size(), stdout);
            fflush(stdout);
            state.stack.popNoReturn();
            vmnext;
        }
        vmcase(INSTANCE_OF) {
            unsigned int v = read32(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 4;

            auto &targetType = state.symbolTable.get(v);
            if(state.stack.pop()->introspect(state, &targetType)) {
                state.stack.push(state.trueObj);
            } else {
                state.stack.push(state.falseObj);
            }
            vmnext;
        }
        vmcase(CHECK_CAST) {
            unsigned int v = read32(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 4;
            TRY(checkCast(state, &state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(PUSH_NULL) {
            state.stack.push(nullptr);
            vmnext;
        }
        vmcase(PUSH_TRUE) {
            state.stack.push(state.trueObj);
            vmnext;
        }
        vmcase(PUSH_FALSE) {
            state.stack.push(state.falseObj);
            vmnext;
        }
        vmcase(PUSH_ESTRING) {
            state.stack.push(state.emptyStrObj);
            vmnext;
        }
        vmcase(LOAD_CONST) {
            unsigned char index = read8(GET_CODE(state), ++state.stack.pc());
            state.stack.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_CONST_W) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 2;
            state.stack.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_CONST_T) {
            unsigned int index = read24(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 3;
            state.stack.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_GLOBAL) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 2;
            auto v = state.getGlobal(index);
            state.stack.push(std::move(v));
            vmnext;
        }
        vmcase(STORE_GLOBAL) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 2;
            state.setGlobal(index, state.stack.pop());
            vmnext;
        }
        vmcase(LOAD_LOCAL) {
            unsigned char index = read8(GET_CODE(state), ++state.stack.pc());
            state.stack.loadLocal(index);
            vmnext;
        }
        vmcase(STORE_LOCAL) {
            unsigned char index = read8(GET_CODE(state), ++state.stack.pc());
            state.stack.storeLocal(index);
            vmnext;
        }
        vmcase(LOAD_FIELD) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 2;
            state.stack.loadField(index);
            vmnext;
        }
        vmcase(STORE_FIELD) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 2;
            state.stack.storeField(index);
            vmnext;
        }
        vmcase(IMPORT_ENV) {
            unsigned char b = read8(GET_CODE(state), ++state.stack.pc());
            TRY(loadEnv(state, b > 0));
            vmnext;
        }
        vmcase(LOAD_ENV) {
            const char *value = loadEnv(state, false);
            TRY(value);
            state.stack.push(DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), value));
            vmnext;
        }
        vmcase(STORE_ENV) {
            DSValue value = state.stack.pop();
            DSValue name = state.stack.pop();

            setenv(typeAs<String_Object>(name)->getValue(),
                   typeAs<String_Object>(value)->getValue(), 1);//FIXME: check return value and throw
            vmnext;
        }
        vmcase(POP) {
            state.stack.popNoReturn();
            vmnext;
        }
        vmcase(DUP) {
            state.stack.dup();
            vmnext;
        }
        vmcase(DUP2) {
            state.stack.dup2();
            vmnext;
        }
        vmcase(SWAP) {
            state.stack.swap();
            vmnext;
        }
        vmcase(NEW_STRING) {
            state.stack.push(DSValue::create<String_Object>(state.symbolTable.get(TYPE::String)));
            vmnext;
        }
        vmcase(APPEND_STRING) {
            DSValue v = state.stack.pop();
            typeAs<String_Object>(state.stack.peek())->append(std::move(v));
            vmnext;
        }
        vmcase(NEW_ARRAY) {
            unsigned int v = read32(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 4;
            state.stack.push(DSValue::create<Array_Object>(state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(APPEND_ARRAY) {
            DSValue v = state.stack.pop();
            typeAs<Array_Object>(state.stack.peek())->append(std::move(v));
            vmnext;
        }
        vmcase(NEW_MAP) {
            unsigned int v = read32(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 4;
            state.stack.push(DSValue::create<Map_Object>(state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(APPEND_MAP) {
            DSValue value = state.stack.pop();
            DSValue key = state.stack.pop();
            typeAs<Map_Object>(state.stack.peek())->set(std::move(key), std::move(value));
            vmnext;
        }
        vmcase(NEW_TUPLE) {
            unsigned int v = read32(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 4;
            state.stack.push(DSValue::create<Tuple_Object>(state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(NEW) {
            unsigned int v = read32(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 4;

            auto &type = state.symbolTable.get(v);
            if(!type.isRecordType()) {
                state.stack.push(DSValue::create<DSObject>(type));
            } else {
                fatal("currently, DSObject allocation not supported\n");
            }
            vmnext;
        }
        vmcase(CALL_METHOD) {
            unsigned short paramSize = read8(GET_CODE(state), ++state.stack.pc());
            unsigned short index = read16(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() += 2;
            TRY(prepareMethodCall(state, index, paramSize));
            vmnext;
        }
        vmcase(CALL_FUNC) {
            unsigned int paramSize = read8(GET_CODE(state), ++state.stack.pc());
            TRY(prepareFuncCall(state, paramSize));
            vmnext;
        }
        vmcase(CALL_NATIVE) {
            unsigned int index = read8(GET_CODE(state), ++state.stack.pc());
            DSValue returnValue = nativeFuncInfoTable()[index].func_ptr(state);
            TRY(!state.hasError());
            if(returnValue) {
                state.stack.push(std::move(returnValue));
            }
            vmnext;
        }
        vmcase(CALL_NATIVE2) {
            unsigned int paramSize = read8(GET_CODE(state), ++state.stack.pc());
            unsigned int index = read8(GET_CODE(state), ++state.stack.pc());
            TRY(windStackFrame(state, paramSize, paramSize, &nativeCallDummy));
            auto ret = nativeFuncInfoTable()[index].func_ptr(state);
            TRY(!state.hasError());
            state.stack.unwind();
            if(ret) {
                state.stack.push(std::move(ret));
            }
            vmnext;
        }
        vmcase(INIT_MODULE) {
            auto &code = typeAs<FuncObject>(state.stack.peek())->getCode();
            windStackFrame(state, 0, 0, &code);
            vmnext;
        }
        vmcase(RETURN) {
            state.stack.unwind();
            if(state.stack.checkVMReturn()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_V) {
            DSValue v = state.stack.pop();
            state.stack.unwind();
            state.stack.push(std::move(v));
            if(state.stack.checkVMReturn()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_UDC) {
            auto v = state.stack.pop();
            state.stack.unwind();
            pushExitStatus(state, typeAs<Int_Object>(v)->getValue());
            if(state.stack.checkVMReturn()) {
                return true;
            }
            vmnext;
        }
        vmcase(RETURN_SIG) {
            auto v = state.stack.getLocal(0);   // old exit status
            state.stack.unwind();
            unsetFlag(DSState::eventDesc, VMEvent::MASK);
            state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(v));
            vmnext;
        }
        vmcase(BRANCH) {
            unsigned short offset = read16(GET_CODE(state), state.stack.pc() + 1);
            if(typeAs<Boolean_Object>(state.stack.pop())->getValue()) {
                state.stack.pc() += 2;
            } else {
                state.stack.pc() += offset - 1;
            }
            vmnext;
        }
        vmcase(GOTO) {
            unsigned int index = read32(GET_CODE(state), state.stack.pc() + 1);
            state.stack.pc() = index - 1;
            vmnext;
        }
        vmcase(THROW) {
            auto obj = state.stack.pop();
            state.throwObject(std::move(obj), 1);
            vmerror;
        }
        vmcase(ENTER_FINALLY) {
            const unsigned int offset = read16(GET_CODE(state), state.stack.pc() + 1);
            const unsigned int savedIndex = state.stack.pc() + 2;
            state.stack.push(DSValue::createNum(savedIndex));
            state.stack.pc() += offset - 1;
            vmnext;
        }
        vmcase(EXIT_FINALLY) {
            switch(state.stack.peek().kind()) {
            case DSValueKind::OBJECT:
            case DSValueKind::INVALID: {
                state.stack.storeThrownObject();
                vmerror;
            }
            case DSValueKind::NUMBER: {
                unsigned int index = static_cast<unsigned int>(state.stack.pop().value());
                state.stack.pc() = index;
                vmnext;
            }
            }
            vmnext;
        }
        vmcase(LOOKUP_HASH) {
            auto key = state.stack.pop();
            auto map = state.stack.pop();
            if(!key.isInvalid()) {
                auto &valueMap = typeAs<Map_Object>(map)->getValueMap();
                auto iter = valueMap.find(key);
                if(iter != valueMap.end()) {
                    unsigned int index = typeAs<Int_Object>(iter->second)->getValue();
                    state.stack.pc() = index - 1;
                }
            }
            vmnext;
        }
        vmcase(I32_TO_I64) {
            int v = typeAs<Int_Object>(state.stack.pop())->getValue();
            long l = v;
            state.stack.push(DSValue::create<Long_Object>(state.symbolTable.get(TYPE::Int64), l));
            vmnext;
        }
        vmcase(I64_TO_I32) {
            unsigned long l = typeAs<Long_Object>(state.stack.pop())->getValue();
            auto v = static_cast<unsigned int>(l);
            state.stack.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), v));
            vmnext;
        }
        vmcase(I32_TO_D) {
            int v = typeAs<Int_Object>(state.stack.pop())->getValue();
            auto d = static_cast<double>(v);
            state.stack.push(DSValue::create<Float_Object>(state.symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(I64_TO_D) {
            long v = typeAs<Long_Object>(state.stack.pop())->getValue();
            auto d = static_cast<double>(v);
            state.stack.push(DSValue::create<Float_Object>(state.symbolTable.get(TYPE::Float), d));
            vmnext;
        }
        vmcase(D_TO_I32) {
            double d = typeAs<Float_Object>(state.stack.pop())->getValue();
            auto v = static_cast<int>(d);
            state.stack.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), v));
            vmnext;
        }
        vmcase(D_TO_I64) {
            double d = typeAs<Float_Object>(state.stack.pop())->getValue();
            auto v = static_cast<long>(d);
            state.stack.push(DSValue::create<Long_Object>(state.symbolTable.get(TYPE::Int64), v));
            vmnext;
        }
        vmcase(REF_EQ) {
            auto v1 = state.stack.pop();
            auto v2 = state.stack.pop();
            state.stack.push(v1 == v2 ? state.trueObj : state.falseObj);
            vmnext;
        }
        vmcase(REF_NE) {
            auto v1 = state.stack.pop();
            auto v2 = state.stack.pop();
            state.stack.push(v1 != v2 ? state.trueObj : state.falseObj);
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
            std::string str = typeAs<String_Object>(state.stack.pop())->getValue();
            expandTilde(str);
            state.stack.push(DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), std::move(str)));
            vmnext;
        }
        vmcase(NEW_CMD) {
            auto v = state.stack.pop();
            auto obj = DSValue::create<Array_Object>(state.symbolTable.get(TYPE::StringArray));
            auto *argv = typeAs<Array_Object>(obj);
            argv->append(std::move(v));
            state.stack.push(std::move(obj));
            vmnext;
        }
        vmcase(ADD_CMD_ARG) {
            unsigned char v = read8(GET_CODE(state), ++state.stack.pc());
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

            auto redir = state.stack.pop();
            auto argv = state.stack.pop();

            TRY(callCommand(state, CmdResolver(), std::move(argv), std::move(redir), attr));
            vmnext;
        }
        vmcase(BUILTIN_CMD) {
            auto attr = state.stack.getLocal(UDC_PARAM_ATTR).value();
            DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);
            bool ret = callBuiltinCommand(state, std::move(argv), std::move(redir), attr);
            flushStdFD();
            TRY(ret);
            vmnext;
        }
        vmcase(BUILTIN_EVAL) {
            auto attr = state.stack.getLocal(UDC_PARAM_ATTR).value();
            DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);

            typeAs<Array_Object>(argv)->takeFirst();
            auto *array = typeAs<Array_Object>(argv);
            if(!array->getValues().empty()) {
                TRY(callCommand(state, CmdResolver(), std::move(argv), std::move(redir), attr));
            } else {
                pushExitStatus(state, 0);
            }
            vmnext;
        }
        vmcase(BUILTIN_EXEC) {
            DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);
            callBuiltinExec(state, std::move(argv), std::move(redir));
            vmnext;
        }
        vmcase(NEW_REDIR) {
            state.stack.push(DSValue::create<RedirConfig>());
            vmnext;
        }
        vmcase(ADD_REDIR_OP) {
            unsigned char v = read8(GET_CODE(state), ++state.stack.pc());
            auto value = state.stack.pop();
            typeAs<RedirConfig>(state.stack.peek())->addRedirOp(static_cast<RedirOP>(v), std::move(value));
            vmnext;
        }
        vmcase(DO_REDIR) {
            TRY(typeAs<RedirConfig>(state.stack.peek())->redirect(state));
            vmnext;
        }
        vmcase(RAND) {
            std::random_device rand;
            std::default_random_engine engine(rand());
            std::uniform_int_distribution<int> dist;
            int v = dist(engine);
            state.stack.push(DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), v));
            vmnext;
        }
        vmcase(GET_SECOND) {
            auto now = std::chrono::system_clock::now();
            auto diff = now - state.baseTime;
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(diff);
            long v = typeAs<Long_Object>(state.getGlobal(BuiltinVarOffset::SECONDS))->getValue();
            v += sec.count();
            state.stack.push(DSValue::create<Long_Object>(state.symbolTable.get(TYPE::Int64), v));
            vmnext;
        }
        vmcase(SET_SECOND) {
            state.baseTime = std::chrono::system_clock::now();
            auto v = state.stack.pop();
            state.setGlobal(BuiltinVarOffset::SECONDS, std::move(v));
            vmnext;
        }
        vmcase(UNWRAP) {
            if(state.stack.peek().kind() == DSValueKind::INVALID) {
                raiseError(state, TYPE::UnwrappingError, std::string("invalid value"));
                vmerror;
            }
            vmnext;
        }
        vmcase(CHECK_UNWRAP) {
            bool b = state.stack.pop().kind() != DSValueKind::INVALID;
            state.stack.push(b ? state.trueObj : state.falseObj);
            vmnext;
        }
        vmcase(TRY_UNWRAP) {
            unsigned short offset = read16(GET_CODE(state), state.stack.pc() + 1);
            if(state.stack.peek().kind() == DSValueKind::INVALID) {
                state.stack.popNoReturn();
                state.stack.pc() += 2;
            } else {
                state.stack.pc() += offset - 1;
            }
            vmnext;
        }
        vmcase(NEW_INVALID) {
            state.stack.push(DSValue::createInvalid());
            vmnext;
        }
        vmcase(RECLAIM_LOCAL) {
            unsigned char offset = read8(GET_CODE(state), ++state.stack.pc());
            unsigned char size = read8(GET_CODE(state), ++state.stack.pc());

            state.stack.reclaimLocals(offset, size);
            vmnext;
        }
        }

        EXCEPT:
        assert(state.hasError());
        bool forceUnwind = state.symbolTable.get(TYPE::_InternalStatus)
                .isSameOrBaseTypeOf(*state.stack.getThrownObject()->getType());
        if(!handleException(state, forceUnwind)) {
            return false;
        }
    }
}

bool VM::handleException(DSState &state, bool forceUnwind) {
    if(state.hook != nullptr) {
        state.hook->vmThrowHook(state);
    }

    for(; !state.stack.checkVMReturn(); state.stack.unwind()) {
        if(!forceUnwind && !CODE(state)->is(CodeKind::NATIVE)) {
            auto *cc = static_cast<const CompiledCode *>(CODE(state));

            // search exception entry
            const unsigned int occurredPC = state.stack.pc();
            const DSType *occurredType = state.stack.getThrownObject()->getType();

            for(unsigned int i = 0; cc->getExceptionEntries()[i].type != nullptr; i++) {
                const ExceptionEntry &entry = cc->getExceptionEntries()[i];
                if(occurredPC >= entry.begin && occurredPC < entry.end
                   && entry.type->isSameOrBaseTypeOf(*occurredType)) {
                    if(entry.type->is(TYPE::_Root)) {
                        /**
                         * when exception entry indicate exception guard of sub-shell,
                         * immediately break interpreter
                         * (due to prevent signal handler interrupt and to load thrown object to stack)
                         */
                        return false;
                    }
                    state.stack.pc() = entry.dest - 1;
                    state.stack.clearOperands();
                    state.stack.reclaimLocals(entry.localOffset, entry.localSize);
                    state.stack.loadThrownObject();
                    return true;
                }
            }
        } else if(CODE(state) == &signalTrampoline) {   // within signal trampoline
            unsetFlag(DSState::eventDesc, VMEvent::MASK);
        }
    }
    return false;
}

#ifdef CODE_COVERAGE
extern "C" void __gcov_flush(); // for coverage reporting
#endif

DSValue VM::startEval(DSState &state, EvalOP op, DSError *dsError) {
    DSValue value;
    const unsigned int oldLevel = state.subshellLevel;

    // run main loop
    const auto ret = mainLoop(state);
    /**
     * if return form subshell, subshellLevel is greater than old.
     */
    const bool subshell = oldLevel != state.subshellLevel;
    DSValue thrown;
    if(ret) {
        if(hasFlag(op, EvalOP::HAS_RETURN)) {
            value = state.stack.pop();
        }
    } else {
        if(!subshell && hasFlag(op, EvalOP::PROPAGATE)) {
            return value;
        }
        thrown = state.stack.takeThrownObject();
    }

    // handle uncaught exception and termination handler
    auto kind = handleUncaughtException(state, thrown, dsError);
    if(subshell || !hasFlag(op, EvalOP::SKIP_TERM) || !ret) {
        callTermHook(state, kind, std::move(thrown));
    }

    if(subshell) {
#ifdef CODE_COVERAGE
        /*
         * after call _exit(), not write coverage information due to skip atexit handler.
         * in order to write coverage information, manually call __gcove_flush()
         */
        __gcov_flush();
#endif
        _exit(state.getExitStatus());
    }

    if(hasFlag(op, EvalOP::COMMIT)) {
        if(!ret) {
            state.symbolTable.abort();
        }
        state.symbolTable.commit();
    }
    return value;
}

int VM::callToplevel(DSState &state, const CompiledCode &code, DSError *dsError) {
    state.globals.resize(state.symbolTable.getMaxGVarIndex());
    state.stack.reset();

    state.stack.wind(0, 0, &code);

    EvalOP op = EvalOP::COMMIT;
    if(hasFlag(state.option, DS_OPTION_INTERACTIVE)) {
        setFlag(op, EvalOP::SKIP_TERM);
    }
    startEval(state, op, dsError);
    return state.getExitStatus();
}

unsigned int VM::prepareArguments(VMState &state, DSValue &&recv,
                                       std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
    state.clearThrownObject();

    // push arguments
    unsigned int size = args.first;
    state.reserve(size + 1);
    state.push(std::move(recv));
    for(unsigned int i = 0; i < size; i++) {
        state.push(std::move(args.second[i]));
    }
    return size;
}

#define RAISE_STACK_OVERFLOW(state) \
    do { \
        raiseError(state, TYPE::StackOverflowError, "interpreter recursion depth reaches limit"); \
        return nullptr; \
    } while(false)

#define GUARD_RECURSION(state) \
    RecursionGuard _guard(state.stack); \
    do { if(!_guard.checkLimit()) { RAISE_STACK_OVERFLOW(state); } } while(false)


static NativeCode initCmdTrampoline() noexcept {
    auto *code = static_cast<unsigned char *>(malloc(sizeof(unsigned char) * 6));
    code[0] = static_cast<unsigned char>(CodeKind::NATIVE);
    code[1] = static_cast<unsigned char>(OpCode::LOAD_LOCAL);
    code[2] = static_cast<unsigned char>(0);
    code[3] = static_cast<unsigned char>(OpCode::PUSH_NULL);
    code[4] = static_cast<unsigned char>(OpCode::CALL_CMD);
    code[5] = static_cast<unsigned char>(OpCode::RETURN_V);
    return NativeCode(code);
}

static auto cmdTrampoline = initCmdTrampoline();

DSValue VM::execCommand(DSState &state, std::vector<DSValue> &&argv, bool propagate) {
    GUARD_RECURSION(state);

    DSValue ret;
    auto obj = DSValue::create<Array_Object>(state.symbolTable.get(TYPE::StringArray), std::move(argv));
    prepareArguments(state.stack, std::move(obj), {0, {}});
    if(windStackFrame(state, 1, 1, &cmdTrampoline)) {
        ret = startEval(state, EvalOP::SKIP_TERM | EvalOP::PROPAGATE | EvalOP::HAS_RETURN, nullptr);
    }

    if(!propagate) {
        DSValue thrown = state.stack.takeThrownObject();
        handleUncaughtException(state, thrown, nullptr);
    }
    return ret;
}

DSValue VM::callMethod(DSState &state, const MethodHandle *handle, DSValue &&recv,
                            std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
    assert(handle != nullptr);
    assert(handle->getParamSize() == args.first);

    GUARD_RECURSION(state);

    unsigned int size = prepareArguments(state.stack, std::move(recv), std::move(args));

    DSValue ret;
    NativeCode code;
    if(handle->isNative()) {
        code = NativeCode(handle->getMethodIndex(), !handle->getReturnType().isVoidType());
    }

    if(handle->isNative() ? windStackFrame(state, size + 1, size + 1, &code)
                        : prepareMethodCall(state, handle->getMethodIndex(), size)) {
        EvalOP op = EvalOP::PROPAGATE | EvalOP::SKIP_TERM;
        if(!handle->getReturnType().isVoidType()) {
            setFlag(op, EvalOP::HAS_RETURN);
        }
        ret = startEval(state, op, nullptr);
    }
    return ret;
}

DSValue VM::callFunction(DSState &state, DSValue &&funcObj, std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
    GUARD_RECURSION(state);

    auto *type = funcObj->getType();
    unsigned int size = prepareArguments(state.stack, std::move(funcObj), std::move(args));

    DSValue ret;
    if(prepareFuncCall(state, size)) {
        assert(type->isFuncType());
        EvalOP op = EvalOP::PROPAGATE | EvalOP::SKIP_TERM;
        if(!static_cast<FunctionType *>(type)->getReturnType()->isVoidType()) {
            setFlag(op, EvalOP::HAS_RETURN);
        }
        ret = startEval(state, op, nullptr);
    }
    return ret;
}

DSErrorKind VM::handleUncaughtException(DSState &state, const DSValue &except, DSError *dsError) {
    if(!except) {
        return DS_ERROR_KIND_SUCCESS;
    }

    auto &errorType = *except->getType();
    DSErrorKind kind = DS_ERROR_KIND_RUNTIME_ERROR;
    if(errorType.is(TYPE::_ShellExit)) {
        kind = DS_ERROR_KIND_EXIT;
    } else if(errorType.is(TYPE::_AssertFail)) {
        kind = DS_ERROR_KIND_ASSERTION_ERROR;
    }

    // get error line number
    unsigned int errorLineNum = 0;
    std::string sourceName;
    if(state.symbolTable.get(TYPE::Error).isSameOrBaseTypeOf(errorType) || kind != DS_ERROR_KIND_RUNTIME_ERROR) {
        auto *obj = typeAs<Error_Object>(except);
        errorLineNum = getOccurredLineNum(obj->getStackTrace());
        const char *ptr = getOccurredSourceName(obj->getStackTrace());
        sourceName = ptr;
    }

    // print error message
    int oldStatus = state.getExitStatus();
    if(kind == DS_ERROR_KIND_RUNTIME_ERROR) {
        fputs("[runtime error]\n", stderr);
        const bool bt = state.symbolTable.get(TYPE::Error).isSameOrBaseTypeOf(errorType);
        auto *handle = state.symbolTable.lookupMethod(errorType, bt ? "backtrace" : OP_STR);

        DSValue ret = VM::callMethod(state, handle, DSValue(except), makeArgs());
        if(state.hasError()) {
            state.stack.clearThrownObject();
            fputs("cannot obtain string representation\n", stderr);
        } else if(!bt) {
            fwrite(typeAs<String_Object>(ret)->getValue(),
                   sizeof(char), typeAs<String_Object>(ret)->size(), stderr);
            fputc('\n', stderr);
        }
    } else if(kind == DS_ERROR_KIND_ASSERTION_ERROR || hasFlag(state.option, DS_OPTION_TRACE_EXIT)) {
        typeAs<Error_Object>(except)->printStackTrace(state);
    }
    fflush(stderr);
    state.updateExitStatus(oldStatus);

    if(dsError != nullptr) {
        *dsError = {
                .kind = kind,
                .fileName = sourceName.empty() ? nullptr : strdup(sourceName.c_str()),
                .lineNum = errorLineNum,
                .name = strdup(kind == DS_ERROR_KIND_RUNTIME_ERROR ? state.symbolTable.getTypeName(errorType) : "")
        };
    }
    return kind;
}

void VM::callTermHook(DSState &state, DSErrorKind kind, DSValue &&except) {
    auto funcObj = state.getGlobal(state.symbolTable.getTermHookIndex());
    if(funcObj.kind() == DSValueKind::INVALID) {
        return;
    }

    int termKind = TERM_ON_EXIT;
    if(kind == DS_ERROR_KIND_RUNTIME_ERROR) {
        termKind = TERM_ON_ERR;
    } else if(kind == DS_ERROR_KIND_ASSERTION_ERROR) {
        termKind = TERM_ON_ASSERT;
    }

    auto oldExitStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
    auto args = makeArgs(
            DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), termKind),
            termKind == TERM_ON_ERR ? std::move(except) : oldExitStatus
    );

    setFlag(DSState::eventDesc, VMEvent::MASK);
    VM::callFunction(state, std::move(funcObj), std::move(args));    // ignore exception
    state.stack.clearThrownObject();

    // restore old value
    state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(oldExitStatus));
    unsetFlag(DSState::eventDesc, VMEvent::MASK);
}

} // namespace