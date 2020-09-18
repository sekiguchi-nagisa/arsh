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

#include <pwd.h>

#include <cstdlib>
#include <cerrno>
#include <random>

#include "opcode.h"
#include "vm.h"
#include "logger.h"
#include "redir.h"
#include "misc/files.h"
#include "misc/glob.hpp"
#include "misc/num_util.hpp"

// #####################
// ##     DSState     ##
// #####################

VMEvent DSState::eventDesc{};

SigSet DSState::pendingSigSet;

/**
 * if environmental variable SHLVL dose not exist, set 0.
 */
static unsigned int getShellLevel() {
    char *shlvl = getenv(ENV_SHLVL);
    unsigned int level = 0;
    if(shlvl != nullptr) {
        auto pair = convertToNum<int64_t>(shlvl);
        if(!pair.second) {
            level = 0;
        } else {
            level = pair.first;
        }
    }
    return level;
}

static unsigned int originalShellLevel() {
    static unsigned int level = getShellLevel();
    return level;
}

static void setPWDs() {
    auto v = getCWD();
    const char *cwd = v ? v.get() : ".";

    const char *pwd = getenv(ENV_PWD);
    if(strcmp(cwd, ".") == 0 || !pwd || *pwd != '/' || !isSameFile(pwd, cwd)) {
        setenv(ENV_PWD, cwd, 1);
        pwd = cwd;
    }

    const char *oldpwd = getenv(ENV_OLDPWD);
    if(!oldpwd || *oldpwd != '/' || !S_ISDIR(getStMode(oldpwd))) {
        setenv(ENV_OLDPWD, pwd, 1);
    }
}

static void initEnv() {
    // set locale
    setlocale(LC_ALL, "");
    setlocale(LC_MESSAGES, "C");

    // set environmental variables

    // update shell level
    setenv(ENV_SHLVL, std::to_string(originalShellLevel() + 1).c_str(), 1);

    // set HOME
    struct passwd *pw = getpwuid(getuid());
    if(pw == nullptr) {
        fatal_perror("getpwuid failed\n");
    }
    setenv(ENV_HOME, pw->pw_dir, 1);

    // set LOGNAME
    setenv(ENV_LOGNAME, pw->pw_name, 1);

    // set USER
    setenv(ENV_USER, pw->pw_name, 1);

    // set PWD/OLDPWD
    setPWDs();
}

DSState::DSState() :
        emptyFDObj(DSValue::create<UnixFdObject>(-1)),
        baseTime(std::chrono::system_clock::now()) {
    // init envs
    initEnv();
    const char *pwd = getenv(ENV_PWD);
    assert(pwd);
    if(*pwd == '/') {
        this->logicalWorkingDir = expandDots(nullptr, pwd);
    }
}


void DSState::updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus) const {
    auto &obj = typeAs<ArrayObject>(this->getGlobal(BuiltinVarOffset::PIPESTATUS));
    obj.refValues().clear();
    obj.refValues().reserve(size + (mergeExitStatus ? 1 : 0));

    for(unsigned int i = 0; i < size; i++) {
        obj.refValues().push_back(DSValue::createInt(procs[i].exitStatus()));
    }
    if(mergeExitStatus) {
        obj.refValues().push_back(this->getGlobal(BuiltinVarOffset::EXIT_STATUS));
    }
}

namespace ydsh {

bool VM::checkCast(DSState &state, const DSType &targetType) {
    if(!instanceOf(state.symbolTable.getTypePool(), state.stack.peek(), targetType)) {
        auto &stackTopType = state.symbolTable.get(state.stack.pop().getTypeID());
        std::string str("cannot cast `");
        str += state.symbolTable.getTypeName(stackTopType);
        str += "' to `";
        str += state.symbolTable.getTypeName(targetType);
        str += "'";
        raiseError(state, TYPE::TypeCastError, std::move(str));
        return false;
    }
    return true;
}

bool VM::checkAssertion(DSState &state) {
    auto msg = state.stack.pop();
    auto ref = msg.asStrRef();
    if(!state.stack.pop().asBool()) {
        raiseError(state, TYPE::_AssertFail, ref.toString());
        return false;
    }
    return true;
}

const char *VM::loadEnv(DSState &state, bool hasDefault) {
    DSValue dValue;
    if(hasDefault) {
        dValue = state.stack.pop();
    }
    auto nameObj = state.stack.pop();
    auto nameRef = nameObj.asStrRef();
    assert(!nameRef.hasNullChar());
    const char *name = nameRef.data();
    const char *env = getenv(name);
    if(env == nullptr && hasDefault) {
        auto ref = dValue.asStrRef();
        if(ref.hasNullChar()) {
            std::string str = SET_ENV_ERROR;
            str += name;
            raiseSystemError(state, EINVAL, std::move(str));
            return nullptr;
        }
        setenv(name, ref.data(), 1);
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

bool VM::storeEnv(DSState &state) {
    auto value = state.stack.pop();
    auto name = state.stack.pop();
    auto nameRef = name.asStrRef();
    auto valueRef = value.asStrRef();
    assert(!nameRef.hasNullChar());
    if(setenv(valueRef.hasNullChar() ? "" : nameRef.data(), valueRef.data(), 1) == 0) {
        return true;
    }
    int errNum = errno;
    std::string str = SET_ENV_ERROR;
    str += nameRef.data();
    raiseSystemError(state, errNum, std::move(str));
    return false;
}

void VM::pushNewObject(DSState &state, const DSType &type) {
    DSValue value;
    if(state.symbolTable.getTypePool().isArrayType(type)) {
        value = DSValue::create<ArrayObject>(type);
    } else if(state.symbolTable.getTypePool().isMapType(type)) {
        value = DSValue::create<MapObject>(type);
    } else if(state.symbolTable.getTypePool().isTupleType(type)) {
        value = DSValue::create<BaseObject>(type);
    } else if(!type.isRecordType()) {
        value = DSValue::createDummy(type);
    } else {
        fatal("currently, DSObject allocation not supported\n");
    }
    state.stack.push(std::move(value));
}

bool VM::prepareUserDefinedCommandCall(DSState &state, const DSCode *code, DSValue &&argvObj,
                                            DSValue &&restoreFD, const CmdCallAttr attr) {
    if(hasFlag(attr, CmdCallAttr::SET_VAR)) {
        // reset exit status
        state.setExitStatus(0);
    }

    // set parameter
    state.stack.reserve(3);
    state.stack.push(DSValue::createNum(static_cast<unsigned int>(attr)));
    state.stack.push(std::move(restoreFD));
    state.stack.push(std::move(argvObj));

    if(!windStackFrame(state, 3, 3, code)) {
        return false;
    }

    if(hasFlag(attr, CmdCallAttr::SET_VAR)) {    // set variable
        auto &argv = typeAs<ArrayObject>(state.stack.getLocal(UDC_PARAM_ARGV));
        auto cmdName = argv.takeFirst();
        const unsigned int argSize = argv.getValues().size();
        state.stack.setLocal(UDC_PARAM_ARGV + 1, DSValue::createInt(argSize));   // #
        state.stack.setLocal(UDC_PARAM_ARGV + 2, std::move(cmdName)); // 0
        unsigned int limit = 9;
        if(argSize < limit) {
            limit = argSize;
        }

        unsigned int index = 0;
        for(; index < limit; index++) {
            state.stack.setLocal(index + UDC_PARAM_ARGV + 3, argv.getValues()[index]);
        }

        for(; index < 9; index++) {
            state.stack.setLocal(index + UDC_PARAM_ARGV + 3, DSValue::createStr());  // set remain
        }
    }
    return true;
}

#define CODE(ctx) ((ctx).stack.code())
#define GET_CODE(ctx) (CODE(ctx)->getCode())
#define CONST_POOL(ctx) (static_cast<const CompiledCode *>(CODE(ctx))->getConstPool())

/* for substitution */

static DSValue readAsStr(int fd) {
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

    return DSValue::createStr(std::move(str));
}

static DSValue readAsStrArray(const DSState &state, int fd) {
    auto ifsRef = state.getGlobal(BuiltinVarOffset::IFS).asStrRef();
    const char *ifs = ifsRef.data();
    const unsigned ifsSize = ifsRef.size();
    unsigned int skipCount = 1;

    char buf[256];
    std::string str;
    auto obj = DSValue::create<ArrayObject>(state.symbolTable.get(TYPE::StringArray));
    auto &array = typeAs<ArrayObject>(obj);

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
                array.append(DSValue::createStr(std::move(str)));
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
        array.append(DSValue::createStr(std::move(str)));
    }

    return obj;
}

static DSValue newFD(const DSState &st, int &fd) {
    if(fd < 0) {
        return st.emptyFDObj;
    }
    int v = fd;
    fd = -1;
    auto value = DSValue::create<UnixFdObject>(v);
    typeAs<UnixFdObject>(value).closeOnExec(true);
    return value;
}

bool VM::attachAsyncJob(DSState &state, unsigned int procSize, const Proc *procs,
                           ForkKind forkKind, PipeSet &pipeSet, DSValue &ret) {
    switch(forkKind) {
    case ForkKind::NONE: {
        auto entry = JobTable::create(
                procSize, procs, false,
                DSValue(state.emptyFDObj), DSValue(state.emptyFDObj));
        // job termination
        auto waitOp = state.isRootShell() && state.isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
        int status = entry->wait(waitOp);
        int errNum = errno;
        state.updatePipeStatus(entry->getProcSize(), entry->getProcs(), false);
        if(entry->available()) {
            state.jobTable.attach(entry);
        }
        state.tryToBeForeground();
        state.jobTable.updateStatus();
        state.setExitStatus(status);
        if(errNum != 0) {
            raiseSystemError(state, errNum, "wait failed");
            return false;
        }
        ret = exitStatusToBool(status);
        break;
    }
    case ForkKind::IN_PIPE:
    case ForkKind::OUT_PIPE: {
        int &fd = forkKind == ForkKind::IN_PIPE ? pipeSet.in[WRITE_PIPE] : pipeSet.out[READ_PIPE];
        ret = newFD(state, fd);
        auto entry = JobTable::create(
                procSize, procs, false,
                DSValue(forkKind == ForkKind::IN_PIPE ? ret : state.emptyFDObj),
                DSValue(forkKind == ForkKind::OUT_PIPE ? ret : state.emptyFDObj));
        state.jobTable.attach(entry, true);
        break;
    }
    case ForkKind::COPROC:
    case ForkKind::JOB:
    case ForkKind::DISOWN: {
        bool disown = forkKind == ForkKind::DISOWN;
        auto entry = JobTable::create(
                procSize, procs, false,
                newFD(state, pipeSet.in[WRITE_PIPE]),
                newFD(state, pipeSet.out[READ_PIPE]));
        state.jobTable.attach(entry, disown);
        ret = DSValue(entry.get());
        break;
    }
    default:
        break;
    }
    return true;
}

bool VM::forkAndEval(DSState &state) {
    const auto forkKind = static_cast<ForkKind >(read8(GET_CODE(state), state.stack.pc()));
    const unsigned short offset = read16(GET_CODE(state), state.stack.pc() + 1);

    // flush standard stream due to prevent mixing io buffer
    flushStdFD();

    // set in/out pipe
    PipeSet pipeset(forkKind);
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
            obj = isStr ? readAsStr(pipeset.out[READ_PIPE]) : readAsStrArray(state, pipeset.out[READ_PIPE]);
            auto waitOp = state.isRootShell() && state.isJobControl() ? Proc::BLOCK_UNTRACED : Proc::BLOCKING;
            int ret = proc.wait(waitOp);   // wait exit
            int errNum = errno;
            tryToClose(pipeset.out[READ_PIPE]); // close read pipe after wait, due to prevent EPIPE
            if(proc.state() != Proc::TERMINATED) {
                state.jobTable.attach(JobTable::create(
                        proc,
                        DSValue(state.emptyFDObj),
                        DSValue(state.emptyFDObj)));
            }
            state.setExitStatus(ret);
            state.tryToBeForeground();
            if(ret < 0) {
                raiseSystemError(state, errNum, "wait failed");
                return false;
            }
            break;
        }
        default:
            Proc procs[1] = {proc};
            if(!attachAsyncJob(state, 1, procs, forkKind, pipeset, obj)) {
                return false;
            }
        }

        // push object
        if(obj) {
            state.stack.push(std::move(obj));
        }

        state.stack.pc() += offset - 1;
    } else if(proc.pid() == 0) {   // child process
        pipeset.setupChildStdin(forkKind, state.isJobControl());
        pipeset.setupChildStdout();
        pipeset.closeAll();

        state.stack.pc() += 3;
    } else {
        raiseSystemError(state, EAGAIN, "fork failed");
        return false;
    }
    return true;
}

/* for pipeline evaluation */
static NativeCode initCode(OpCode op) {
    NativeCode::ArrayType code;
    code[0] = static_cast<char>(op);
    code[1] = static_cast<char>(OpCode::RETURN_V);
    return NativeCode(code);
}

static bool lookupUdc(const DSState &state, const char *name, Command &cmd, bool forceGlobal = false) {
    const CompiledCode *code = nullptr;
    state.getCallStack().walkFrames([&](const ControlFrame &frame) {
        auto *c = frame.code;
        if(c->is(CodeKind::NATIVE)) {
            return true;    // continue
        }
        code = static_cast<const CompiledCode*>(c);
        return false;
    });

    const ModType *modType = nullptr;
    if(code && !forceGlobal) {
        auto key = code->getSourceName();
        auto *e = state.symbolTable.getModLoader().find(key);
        if(e && e->isModule()) {
            modType = static_cast<const ModType*>(&state.symbolTable.get(e->getTypeId()));
        }
    }
    auto handle = state.symbolTable.lookupUdc(modType, name);
    auto *udcObj = handle != nullptr ?
            &typeAs<FuncObject>(state.getGlobal(handle->getIndex())) : nullptr;

    if(udcObj) {
        auto &type = state.symbolTable.get(udcObj->getTypeID());
        if(type.isModType()) {
            cmd.kind = Command::MODULE;
            cmd.modType = static_cast<const ModType*>(&type);
        } else {
            assert(type.isVoidType());
            cmd.kind = Command::USER_DEFINED;
            cmd.udc = &udcObj->getCode();
        }
        return true;
    }
    return false;
}

Command CmdResolver::operator()(DSState &state, const char *cmdName) const {
    Command cmd{};

    // first, check user-defined command
    if(!hasFlag(this->mask, MASK_UDC)) {
        if(lookupUdc(state, cmdName, cmd)) {
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
            if(state.getGlobal(index).isObject()) {
                bool r = lookupUdc(state, CMD_FALLBACK_HANDLER, cmd, true); //FIXME:
                (void) r;
                assert(r);
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
    if(!setCloseOnExec(selfpipe[WRITE_PIPE], true)) {
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
        int errNum2 = errno;
        if(proc.state() != Proc::TERMINATED) {
            state.jobTable.attach(JobTable::create(
                    proc,
                    DSValue(state.emptyFDObj),
                    DSValue(state.emptyFDObj)));
        }
        int ret = state.tryToBeForeground();
        LOG(DUMP_EXEC, "tryToBeForeground: %d, %s", ret, strerror(errno));
        state.jobTable.updateStatus();
        if(errnum != 0) {
            errNum2 = errnum;
        }
        if(errNum2 != 0) {
            raiseCmdError(state, argv[0], errNum2);
            return 1;
        }
        return status;
    }
}

bool VM::prepareSubCommand(DSState &state, const ModType &modType,
                           DSValue &&argvObj, DSValue &&restoreFD) {
    auto &array = typeAs<ArrayObject>(argvObj);
    if(array.size() == 1) {
        ERROR(array, "require subcommand");
        pushExitStatus(state, 2);
        return true;
    }

    const char *cmd = array.getValues()[1].asCStr();
    if(*cmd == '_') {
        ERROR(array, "cannot resolve private subcommand: %s", cmd);
        pushExitStatus(state, 1);
        return true;
    }

    std::string key = CMD_SYMBOL_PREFIX;
    key += cmd;
    auto *handle = modType.find(key);
    if(!handle) {
        ERROR(array, "undefined subcommand: %s", cmd);
        pushExitStatus(state, 2);
        return true;
    }
    auto *udc = &typeAs<FuncObject>(state.getGlobal(handle->getIndex())).getCode();
    array.takeFirst();
    return prepareUserDefinedCommandCall(
            state, udc, std::move(argvObj), std::move(restoreFD), CmdCallAttr::SET_VAR);
}

bool VM::callCommand(DSState &state, CmdResolver resolver, DSValue &&argvObj, DSValue &&redirConfig, CmdCallAttr attr) {
    auto &array = typeAs<ArrayObject>(argvObj);
    auto cmd = resolver(state, array.getValues()[0].asCStr());

    switch(cmd.kind) {
    case Command::USER_DEFINED:
    case Command::BUILTIN_S: {
        if(cmd.kind == Command::USER_DEFINED) {
            setFlag(attr, CmdCallAttr::SET_VAR);
        }
        return prepareUserDefinedCommandCall(state, cmd.udc, std::move(argvObj), std::move(redirConfig), attr);
    }
    case Command::BUILTIN: {
        int status = cmd.builtinCmd(state, array);
        flushStdFD();
        if(state.hasError()) {
            return false;
        }
        pushExitStatus(state, status);
        return true;
    }
    case Command::MODULE:
        assert(cmd.modType);
        return prepareSubCommand(state, *cmd.modType, std::move(argvObj), std::move(redirConfig));
    case Command::EXTERNAL: {
        // create argv
        const unsigned int size = array.getValues().size();
        char *argv[size + 1];
        for(unsigned int i = 0; i < size; i++) {
            argv[i] = const_cast<char *>(array.getValues()[i].asCStr());
        }
        argv[size] = nullptr;

        if(hasFlag(attr, CmdCallAttr::NEED_FORK)) {
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


int invalidOptionError(const ArrayObject &obj, const GetOptState &s);

bool VM::builtinCommand(DSState &state, DSValue &&argvObj, DSValue &&redir, CmdCallAttr attr) {
    auto &arrayObj = typeAs<ArrayObject>(argvObj);

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
            const char *commandName = arrayObj.getValues()[index].asCStr();
            auto cmd = CmdResolver(CmdResolver::MASK_FALLBACK, FilePathCache::DIRECT_SEARCH)(state, commandName);
            switch(cmd.kind) {
            case Command::USER_DEFINED:
            case Command::MODULE: {
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
                if(path != nullptr && isExecutable(path)) {
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
                ERROR(arrayObj, "%s: not found", commandName);
            }
        }
        pushExitStatus(state, successCount > 0 ? 0 : 1);
        return true;
    }
    pushExitStatus(state, 0);
    return true;
}

void VM::builtinExec(DSState &state, DSValue &&array, DSValue &&redir) {
    auto &argvObj = typeAs<ArrayObject>(array);
    bool clearEnv = false;
    const char *progName = nullptr;
    GetOptState optState;

    if(redir) {
        typeAs<RedirObject>(redir).ignoreBackup();
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
            argv2[i - index] = const_cast<char *>(argvObj.getValues()[i].asCStr());
        }
        argv2[argc - index] = nullptr;

        const char *filePath = state.pathCache.searchPath(argv2[0], FilePathCache::DIRECT_SEARCH);
        if(progName != nullptr) {
            argv2[0] = const_cast<char *>(progName);
        }

        char *envp[] = {nullptr};
        xexecve(filePath, argv2, clearEnv ? envp : nullptr, redir);
        PERROR(argvObj, "%s", argvObj.getValues()[index].asCStr());
        exit(1);
    }
    pushExitStatus(state, 0);
}

bool VM::callPipeline(DSState &state, bool lastPipe, ForkKind forkKind) {
    /**
     * ls | grep .
     * ==> pipeSize == 1, procSize == 2
     *
     * if lastPipe is true,
     *
     * ls | { grep . ;}
     * ==> pipeSize == 1, procSize == 1
     */
    const unsigned int procSize = read8(GET_CODE(state), state.stack.pc()) - 1;
    const unsigned int pipeSize = procSize - (lastPipe ? 0 : 1);

    assert(pipeSize > 0);

    PipeSet pipeset(forkKind);
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
            pipeset.setupChildStdin(forkKind, state.isJobControl());
        }
        if(procIndex > 0 && procIndex < pipeSize) {   // other process.
            ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            ::dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == pipeSize && !lastPipe) {    // last process
            ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            pipeset.setupChildStdout();
        }
        pipeset.closeAll();
        closeAllPipe(pipeSize, pipefds);

        // set pc to next instruction
        state.stack.pc() += read16(GET_CODE(state), state.stack.pc() + 1 + procIndex * 2) - 1;
    } else if(procIndex == procSize) { // parent (last pipeline)
        if(lastPipe) {
            /**
             * in last pipe, save current stdin before call dup2
             */
            auto jobEntry = JobTable::create(
                    procSize, childs, true,
                    DSValue(state.emptyFDObj), DSValue(state.emptyFDObj));
            ::dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            closeAllPipe(pipeSize, pipefds);
            state.stack.push(DSValue::create<PipelineObject>(state, std::move(jobEntry)));
        } else {
            tryToClose(pipeset.in[READ_PIPE]);
            tryToClose(pipeset.out[WRITE_PIPE]);
            closeAllPipe(pipeSize, pipefds);
            DSValue obj;
            if(!attachAsyncJob(state, procSize, childs, forkKind, pipeset, obj)) {
                return false;
            }
            if(obj) {
                state.stack.push(std::move(obj));
            }
        }

        // set pc to next instruction
        state.stack.pc() += read16(GET_CODE(state), state.stack.pc() + 1 + procIndex * 2) - 1;
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
    auto &valueType = state.symbolTable.get(value.getTypeID());

    auto &argv = typeAs<ArrayObject>(state.stack.peekByOffset(1));
    if(valueType.is(TYPE::String)) {  // String
        if(skipEmptyStr && value.asStrRef().empty()) {
            return;
        }
        argv.append(std::move(value));
        return;
    }

    if(valueType.is(TYPE::UnixFD)) { // UnixFD
        if(!state.stack.peek()) {
            state.stack.pop();
            state.stack.push(DSValue::create<RedirObject>());
        }
        auto strObj = DSValue::createStr(value.toString());
        typeAs<RedirObject>(state.stack.peek()).addRedirOp(RedirOP::NOP, std::move(value));
        argv.append(std::move(strObj));
        return;
    }

    assert(valueType.is(TYPE::StringArray));  // Array<String>
    auto &arrayObj = typeAs<ArrayObject>(value);
    for(auto &element : arrayObj.getValues()) {
        if(element.asStrRef().empty()) {
            continue;
        }
        argv.append(element);
    }
}

class GlobIter {
private:
    const DSValue *cur;
    const char *ptr{nullptr};

public:
    explicit GlobIter(const DSValue &value) : cur(&value) {
        if(this->cur->hasStrRef()) {
            this->ptr = this->cur->asStrRef().begin();
        }
    }

    char operator*() const {
        return this->ptr == nullptr ? '\0' : *this->ptr;
    }

    bool operator==(const GlobIter &other) const {
        return this->cur == other.cur && this->ptr == other.ptr;
    }

    bool operator!=(const GlobIter &other) const {
        return !(*this == other);
    }

    GlobIter &operator++() {
        if(this->ptr) {
            this->ptr++;
            if(*this->ptr == '\0') {  // if StringRef iterator reaches null, increment DSValue iterator
                this->ptr = nullptr;
            }
        }
        if(!this->ptr) {
            this->cur++;
            if(this->cur->hasStrRef()) {
                this->ptr = this->cur->asStrRef().begin();
            }
        }
        return *this;
    }

    const DSValue *getIter() const {
        return this->cur;
    }
};

struct DSValueGlobMeta {
    static bool isAny(GlobIter iter) {
        auto &v = *iter.getIter();
        return v.kind() == DSValueKind::GLOB_META && v.asGlobMeta() == GlobMeta::ANY;
    }

    static bool isZeroOrMore(GlobIter iter) {
        auto &v = *iter.getIter();
        return v.kind() == DSValueKind::GLOB_META && v.asGlobMeta() == GlobMeta::ZERO_OR_MORE;
    }

    static void preExpand(std::string &path) {
        expandTilde(path, true);
    }
};

static void raiseGlobbingError(DSState &state, const VMState &st, unsigned int size, const char *message) {
    std::string value = message;
    value += " `";
    for(unsigned int i = 0; i < size; i++) {
        auto &v = st.peekByOffset(size - i);
        if(v.hasStrRef()) {
            for(auto &e : v.asStrRef()) {
                if(e != '\0') {
                    value += e;
                } else {
                    value += "\\x00";
                }
            }
        } else {
            value += v.toString();
        }
    }
    value += "'";
    raiseError(state, TYPE::GlobbingError, std::move(value));
}

bool VM::addGlobbingPath(DSState &state, const unsigned int size, bool tilde) {
    /**
     * stack layout
     *
     * ===========> stack grow
     * +------+-------+--------+     +--------+----------+
     * | argv | redir | param1 | ~~~ | paramN | paramN+1 |
     * +------+-------+--------+     +--------+----------+
     *
     * after evaluation
     * +------+-------+
     * | argv | redir |
     * +------+-------+
     */
    GlobIter begin(state.stack.peekByOffset(size));
    GlobIter end(state.stack.peek());

    // check if glob path fragments have null character
    for(unsigned int i = 0; i < size; i++) {
        auto &v = state.stack.peekByOffset(size - i);
        if(v.hasStrRef()) {
            auto ref = v.asStrRef();
            if(ref.hasNullChar()) {
                raiseGlobbingError(state, state.stack, size, "glob pattern has null characters");
                return false;
            }
        }
    }

    auto &argv = state.stack.peekByOffset(size + 2);
    const unsigned int oldSize = typeAs<ArrayObject>(argv).size();
    auto appender = [&](std::string &&path) {
        typeAs<ArrayObject>(argv).append(DSValue::createStr(std::move(path)));
        return true;    //FIXME: check array size limit
    };
    GlobMatchOption option{};
    if(tilde) {
        setFlag(option, GlobMatchOption::TILDE);
    }
    if(hasFlag(state.runtimeOption, RuntimeOption::DOTGLOB)) {
        setFlag(option, GlobMatchOption::DOTGLOB);
    }
    if(hasFlag(state.runtimeOption, RuntimeOption::FASTGLOB)) {
        setFlag(option, GlobMatchOption::FASTGLOB);
    }
    auto matcher = createGlobMatcher<DSValueGlobMeta>(nullptr, begin, end, option);
    auto ret = matcher(appender);
    if(ret == GlobMatchResult::MATCH || hasFlag(state.runtimeOption, RuntimeOption::NULLGLOB)) {
        typeAs<ArrayObject>(argv).sortAsStrArray(oldSize);
        for(unsigned int i = 0; i <= size; i++) {
            state.stack.popNoReturn();
        }
        return true;
    } else {
        const char *msg = ret == GlobMatchResult::NOMATCH ?
                "No matches for glob pattern" : "number of glob results reaches limit";
        raiseGlobbingError(state, state.stack, size, msg);
        return false;
    }
}

static NativeCode initSignalTrampoline() noexcept {
    NativeCode::ArrayType code;
    code[0] = static_cast<char>(OpCode::LOAD_LOCAL);
    code[1] = 1;
    code[2] = static_cast<char>(OpCode::LOAD_LOCAL);
    code[3] = 2;
    code[4] = static_cast<char>(OpCode::CALL_FUNC);
    code[5] = 1;
    code[6] = static_cast<char>(OpCode::EXIT_SIG);
    code[7] = static_cast<char>(OpCode::RETURN);
    return NativeCode(code);
}

static auto signalTrampoline = initSignalTrampoline();

bool VM::kickSignalHandler(DSState &state, int sigNum, DSValue &&func) {
    state.stack.reserve(3);
    state.stack.push(state.getGlobal(BuiltinVarOffset::EXIT_STATUS));
    state.stack.push(std::move(func));
    state.stack.push(DSValue::createSig(sigNum));

    return windStackFrame(state, 3, 3, &signalTrampoline);
}

bool VM::checkVMEvent(DSState &state) {
    if(hasFlag(DSState::eventDesc, VMEvent::SIGNAL) &&
       !hasFlag(DSState::eventDesc, VMEvent::MASK)) {
        SignalGuard guard;

        int sigNum = DSState::pendingSigSet.popPendingSig();
        if(DSState::pendingSigSet.empty()) {
            DSState::clearPendingSignal();
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
        auto op = static_cast<OpCode>(GET_CODE(state)[state.stack.pc()]);
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
#define vmerror goto EXCEPT

#define TRY(E) do { if(!(E)) { vmerror; } } while(false)

bool VM::mainLoop(DSState &state) {
    OpCode op;
    while(true) {
        if(!empty(DSState::eventDesc)) {
            TRY(checkVMEvent(state));
        }

        // fetch next opcode
        op = static_cast<OpCode>(GET_CODE(state)[state.stack.pc()++]);

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
            unsigned int v = read24(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 3;

            auto &stackTopType = state.symbolTable.get(v);
            assert(!stackTopType.isVoidType());
            auto ref = state.stack.peek().asStrRef();
            std::string value = ": ";
            value += state.symbolTable.getTypeName(stackTopType);
            value += " = ";
            value.append(ref.data(), ref.size());
            value += "\n";
            fwrite(value.c_str(), sizeof(char), value.size(), stdout);
            fflush(stdout);
            state.stack.popNoReturn();
            vmnext;
        }
        vmcase(INSTANCE_OF) {
            unsigned int v = read24(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 3;

            auto &targetType = state.symbolTable.get(v);
            auto value = state.stack.pop();
            bool ret = instanceOf(state.symbolTable.getTypePool(), value, targetType);
            state.stack.push(DSValue::createBool(ret));
            vmnext;
        }
        vmcase(CHECK_CAST) {
            unsigned int v = read24(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 3;
            TRY(checkCast(state, state.symbolTable.get(v)));
            vmnext;
        }
        vmcase(PUSH_NULL) {
            state.stack.push(nullptr);
            vmnext;
        }
        vmcase(PUSH_TRUE) {
            state.stack.push(DSValue::createBool(true));
            vmnext;
        }
        vmcase(PUSH_FALSE) {
            state.stack.push(DSValue::createBool(false));
            vmnext;
        }
        vmcase(PUSH_SIG) {
            unsigned int value = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            state.stack.push(DSValue::createSig(value));
            vmnext;
        }
        vmcase(PUSH_INT) {
            unsigned int value = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            state.stack.push(DSValue::createInt(value));
            vmnext;
        }
        vmcase(PUSH_STR0) {
            state.stack.push(DSValue::createStr());
            vmnext;
        }
        vmcase(PUSH_STR1)
        vmcase(PUSH_STR2)
        vmcase(PUSH_STR3) {
            char data[3];
            unsigned int size = op == OpCode::PUSH_STR1 ? 1 : op == OpCode::PUSH_STR2 ? 2 : 3;
            for(unsigned int i = 0; i < size; i++) {
                data[i] = read8(GET_CODE(state), state.stack.pc());
                state.stack.pc()++;
            }
            state.stack.push(DSValue::createStr(StringRef(data, size)));
            vmnext;
        }
        vmcase(PUSH_META) {
            unsigned int value = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            state.stack.push(DSValue::createGlobMeta(static_cast<GlobMeta>(value)));
            vmnext;
        }
        vmcase(LOAD_CONST) {
            unsigned char index = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            state.stack.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_CONST_W) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 2;
            state.stack.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_CONST_T) {
            unsigned int index = read24(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 3;
            state.stack.push(CONST_POOL(state)[index]);
            vmnext;
        }
        vmcase(LOAD_GLOBAL) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 2;
            auto v = state.getGlobal(index);
            state.stack.push(std::move(v));
            vmnext;
        }
        vmcase(STORE_GLOBAL) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 2;
            state.setGlobal(index, state.stack.pop());
            vmnext;
        }
        vmcase(LOAD_LOCAL) {
            unsigned char index = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            state.stack.loadLocal(index);
            vmnext;
        }
        vmcase(STORE_LOCAL) {
            unsigned char index = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            state.stack.storeLocal(index);
            vmnext;
        }
        vmcase(LOAD_FIELD) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 2;
            state.stack.loadField(index);
            vmnext;
        }
        vmcase(STORE_FIELD) {
            unsigned short index = read16(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 2;
            state.stack.storeField(index);
            vmnext;
        }
        vmcase(IMPORT_ENV) {
            unsigned char b = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            TRY(loadEnv(state, b > 0));
            vmnext;
        }
        vmcase(LOAD_ENV) {
            const char *value = loadEnv(state, false);
            TRY(value);
            state.stack.push(DSValue::createStr(value));
            vmnext;
        }
        vmcase(STORE_ENV) {
            TRY(storeEnv(state));
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
        vmcase(CONCAT)
        vmcase(APPEND) {
            const bool selfConcat = op == OpCode::APPEND;
            auto right = state.stack.pop();
            auto left = state.stack.pop();
            if(!concatAsStr(left, right, selfConcat)) {
                raiseError(state, TYPE::OutOfRangeError, std::string("reach String size limit"));
                vmerror;
            }
            state.stack.push(std::move(left));
            vmnext;
        }
        vmcase(APPEND_ARRAY) {
            DSValue v = state.stack.pop();
            typeAs<ArrayObject>(state.stack.peek()).append(std::move(v));
            vmnext;
        }
        vmcase(APPEND_MAP) {
            DSValue value = state.stack.pop();
            DSValue key = state.stack.pop();
            typeAs<MapObject>(state.stack.peek()).set(std::move(key), std::move(value));
            vmnext;
        }
        vmcase(NEW) {
            unsigned int v = read24(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 3;
            auto &type = state.symbolTable.get(v);
            pushNewObject(state, type);
            vmnext;
        }
        vmcase(CALL_METHOD) {
            unsigned short paramSize = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            unsigned short index = read16(GET_CODE(state), state.stack.pc());
            state.stack.pc() += 2;
            TRY(prepareMethodCall(state, index, paramSize));
            vmnext;
        }
        vmcase(CALL_FUNC) {
            unsigned int paramSize = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            TRY(prepareFuncCall(state, paramSize));
            vmnext;
        }
        vmcase(CALL_NATIVE) {
            unsigned int index = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            DSValue returnValue = nativeFuncInfoTable()[index].func_ptr(state);
            TRY(!state.hasError());
            if(returnValue) {
                state.stack.push(std::move(returnValue));
            }
            vmnext;
        }
        vmcase(CALL_NATIVE2) {
            unsigned int paramSize = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            unsigned int index = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            auto old = state.stack.nativeWind(paramSize);
            auto ret = nativeFuncInfoTable()[index].func_ptr(state);
            state.stack.nativeUnwind(old);
            TRY(!state.hasError());
            if(ret) {
                state.stack.push(std::move(ret));
            }
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
            pushExitStatus(state, v.asInt());
            if(state.stack.checkVMReturn()) {
                return true;
            }
            vmnext;
        }
        vmcase(EXIT_SIG) {
            auto v = state.stack.getLocal(0);   // old exit status
            unsetFlag(DSState::eventDesc, VMEvent::MASK);
            state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(v));
            vmnext;
        }
        vmcase(BRANCH) {
            unsigned short offset = read16(GET_CODE(state), state.stack.pc());
            if(state.stack.pop().asBool()) {
                state.stack.pc() += 2;
            } else {
                state.stack.pc() += offset - 1;
            }
            vmnext;
        }
        vmcase(GOTO) {
            unsigned int index = read32(GET_CODE(state), state.stack.pc());
            state.stack.pc() = index;
            vmnext;
        }
        vmcase(THROW) {
            auto obj = state.stack.pop();
            state.throwObject(std::move(obj), 1);
            vmerror;
        }
        vmcase(ENTER_FINALLY) {
            const unsigned int offset = read16(GET_CODE(state), state.stack.pc());
            const unsigned int savedIndex = state.stack.pc() + 2;
            state.stack.push(DSValue::createNum(savedIndex));
            state.stack.pc() += offset - 1;
            vmnext;
        }
        vmcase(EXIT_FINALLY) {
            if(state.stack.restoreThrownObject()) {
                vmerror;
            } else {
                assert(state.stack.peek().kind() == DSValueKind::NUMBER);
                unsigned int index = state.stack.pop().asNum();
                state.stack.pc() = index;
                vmnext;
            }
        }
        vmcase(LOOKUP_HASH) {
            auto key = state.stack.pop();
            auto map = state.stack.pop();
            if(!key.isInvalid()) {
                auto &valueMap = typeAs<MapObject>(map).getValueMap();
                auto iter = valueMap.find(key);
                if(iter != valueMap.end()) {
                    assert(iter->second.kind() == DSValueKind::NUMBER);
                    unsigned int index = iter->second.asNum();
                    state.stack.pc() = index;
                }
            }
            vmnext;
        }
        vmcase(REF_EQ) {
            auto v1 = state.stack.pop();
            auto v2 = state.stack.pop();
            state.stack.push(DSValue::createBool(v1 == v2));
            vmnext;
        }
        vmcase(REF_NE) {
            auto v1 = state.stack.pop();
            auto v2 = state.stack.pop();
            state.stack.push(DSValue::createBool(v1 != v2));
            vmnext;
        }
        vmcase(FORK) {
            TRY(forkAndEval(state));
            vmnext;
        }
        vmcase(PIPELINE)
        vmcase(PIPELINE_LP)
        vmcase(PIPELINE_ASYNC) {
            bool lastPipe = op == OpCode::PIPELINE_LP;
            auto kind = ForkKind::NONE;
            if(op == OpCode::PIPELINE_ASYNC) {
                unsigned char v = read8(GET_CODE(state), state.stack.pc());
                state.stack.pc()++;
                kind = static_cast<ForkKind>(v);
            }
            TRY(callPipeline(state, lastPipe, kind));
            vmnext;
        }
        vmcase(EXPAND_TILDE) {
            std::string str = state.stack.pop().asStrRef().toString();
            expandTilde(str, true);
            state.stack.push(DSValue::createStr(std::move(str)));
            vmnext;
        }
        vmcase(NEW_CMD) {
            auto v = state.stack.pop();
            auto obj = DSValue::create<ArrayObject>(state.symbolTable.get(TYPE::StringArray));
            auto &argv = typeAs<ArrayObject>(obj);
            argv.append(std::move(v));
            state.stack.push(std::move(obj));
            vmnext;
        }
        vmcase(ADD_CMD_ARG) {
            unsigned char v = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            addCmdArg(state, v > 0);
            vmnext;
        }
        vmcase(ADD_GLOBBING) {
            unsigned int size = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            unsigned int v = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            TRY(addGlobbingPath(state, size, v > 0));
            vmnext;
        }
        vmcase(CALL_CMD)
        vmcase(CALL_CMD_NOFORK) {
            bool needFork = op != OpCode::CALL_CMD_NOFORK;
            CmdCallAttr attr{};
            if(needFork) {
                setFlag(attr, CmdCallAttr::NEED_FORK);
            }

            auto redir = state.stack.pop();
            auto argv = state.stack.pop();

            TRY(callCommand(state, CmdResolver(), std::move(argv), std::move(redir), attr));
            vmnext;
        }
        vmcase(BUILTIN_CMD) {
            auto v = state.stack.getLocal(UDC_PARAM_ATTR).asNum();
            auto attr = static_cast<CmdCallAttr>(v);
            DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);
            bool ret = builtinCommand(state, std::move(argv), std::move(redir), attr);
            flushStdFD();
            TRY(ret);
            vmnext;
        }
        vmcase(BUILTIN_EVAL) {
            auto v = state.stack.getLocal(UDC_PARAM_ATTR).asNum();
            auto attr = static_cast<CmdCallAttr>(v);
            DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);

            typeAs<ArrayObject>(argv).takeFirst();
            auto &array = typeAs<ArrayObject>(argv);
            if(!array.getValues().empty()) {
                TRY(callCommand(state, CmdResolver(), std::move(argv), std::move(redir), attr));
            } else {
                pushExitStatus(state, 0);
            }
            vmnext;
        }
        vmcase(BUILTIN_EXEC) {
            DSValue redir = state.stack.getLocal(UDC_PARAM_REDIR);
            DSValue argv = state.stack.getLocal(UDC_PARAM_ARGV);
            builtinExec(state, std::move(argv), std::move(redir));
            vmnext;
        }
        vmcase(NEW_REDIR) {
            state.stack.push(DSValue::create<RedirObject>());
            vmnext;
        }
        vmcase(ADD_REDIR_OP) {
            unsigned char v = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            auto value = state.stack.pop();
            typeAs<RedirObject>(state.stack.peek()).addRedirOp(static_cast<RedirOP>(v), std::move(value));
            vmnext;
        }
        vmcase(DO_REDIR) {
            TRY(typeAs<RedirObject>(state.stack.peek()).redirect(state));
            vmnext;
        }
        vmcase(RAND) {
            std::random_device rand;
            std::default_random_engine engine(rand());
            std::uniform_int_distribution<int64_t> dist;
            int64_t v = dist(engine);
            state.stack.push(DSValue::createInt(v));
            vmnext;
        }
        vmcase(GET_SECOND) {
            auto now = std::chrono::system_clock::now();
            auto diff = now - state.baseTime;
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(diff);
            int64_t v = state.getGlobal(BuiltinVarOffset::SECONDS).asInt();
            v += sec.count();
            state.stack.push(DSValue::createInt(v));
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
            state.stack.push(DSValue::createBool(b));
            vmnext;
        }
        vmcase(TRY_UNWRAP) {
            unsigned short offset = read16(GET_CODE(state), state.stack.pc());
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
            unsigned char offset = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;
            unsigned char size = read8(GET_CODE(state), state.stack.pc());
            state.stack.pc()++;

            state.stack.reclaimLocals(offset, size);
            vmnext;
        }
        }

        EXCEPT:
        assert(state.hasError());
        if(!handleException(state)) {
            return false;
        }
    }
}

bool VM::handleException(DSState &state) {
    if(state.hook != nullptr) {
        state.hook->vmThrowHook(state);
    }

    for(; !state.stack.checkVMReturn(); state.stack.unwind()) {
        if(!CODE(state)->is(CodeKind::NATIVE)) {
            auto *cc = static_cast<const CompiledCode *>(CODE(state));

            // search exception entry
            const unsigned int occurredPC = state.stack.pc() - 1;
            const DSType &occurredType = state.symbolTable.get(state.stack.getThrownObject().getTypeID());

            for(unsigned int i = 0; cc->getExceptionEntries()[i].type != nullptr; i++) {
                const ExceptionEntry &entry = cc->getExceptionEntries()[i];
                if(occurredPC >= entry.begin && occurredPC < entry.end
                   && entry.type->isSameOrBaseTypeOf(occurredType)) {
                    if(entry.type->is(TYPE::_ProcGuard)) {
                        /**
                         * when exception entry indicate exception guard of sub-shell,
                         * immediately break interpreter
                         * (due to prevent signal handler interrupt and to load thrown object to stack)
                         */
                        return false;
                    }
                    state.stack.pc() = entry.dest;
                    state.stack.clearOperands();
                    state.stack.reclaimLocals(entry.localOffset, entry.localSize);
                    if(entry.type->is(TYPE::_Root)) {   // finally block
                        state.stack.saveThrownObject();
                    } else {    // catch block
                        state.stack.loadThrownObject();
                    }
                    return true;
                }
            }
        } else if(CODE(state) == &signalTrampoline) {   // within signal trampoline
            unsetFlag(DSState::eventDesc, VMEvent::MASK);
        }
    }
    return false;
}

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
        terminate(state.getMaskedExitStatus());
    }

    if(hasFlag(op, EvalOP::COMMIT)) {
        if(ret) {
            state.symbolTable.commit();
        } else {
            state.symbolTable.abort();
        }
    }
    return value;
}

int VM::callToplevel(DSState &state, const CompiledCode &code, DSError *dsError) {
    state.globals.resize(state.symbolTable.getMaxGVarIndex());
    state.stack.reset();

    state.stack.wind(0, 0, &code);

    EvalOP op = EvalOP::COMMIT;
    if(hasFlag(state.compileOption, CompileOption::INTERACTIVE)) {
        setFlag(op, EvalOP::SKIP_TERM);
    }
    startEval(state, op, dsError);
    return state.getMaskedExitStatus();
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
    NativeCode::ArrayType code;
    code[0] = static_cast<char>(OpCode::LOAD_LOCAL);
    code[1] = 0;
    code[2] = static_cast<char>(OpCode::PUSH_NULL);
    code[3] = static_cast<char>(OpCode::CALL_CMD);
    code[4] = static_cast<char>(OpCode::RETURN_V);
    return NativeCode(code);
}

DSValue VM::execCommand(DSState &state, std::vector<DSValue> &&argv, bool propagate) {
    GUARD_RECURSION(state);

    static auto cmdTrampoline = initCmdTrampoline();

    DSValue ret;
    auto obj = DSValue::create<ArrayObject>(state.symbolTable.get(TYPE::StringArray), std::move(argv));
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

    auto &type = state.symbolTable.get(funcObj.getTypeID());
    unsigned int size = prepareArguments(state.stack, std::move(funcObj), std::move(args));

    DSValue ret;
    if(prepareFuncCall(state, size)) {
        assert(type.isFuncType());
        EvalOP op = EvalOP::PROPAGATE | EvalOP::SKIP_TERM;
        if(!static_cast<FunctionType&>(type).getReturnType()->isVoidType()) {
            setFlag(op, EvalOP::HAS_RETURN);
        }
        ret = startEval(state, op, nullptr);
    }
    return ret;
}

static int parseExitStatus(const ErrorObject &obj) {
    auto ref = obj.getMessage().asStrRef();
    auto r = ref.lastIndexOf(" ");
    ref.removePrefix(r + 1);
    auto pair = convertToNum<int32_t>(ref.begin(), ref.end());
    assert(pair.second);
    return pair.first;
}

DSErrorKind VM::handleUncaughtException(DSState &state, const DSValue &except, DSError *dsError) {
    if(!except) {
        return DS_ERROR_KIND_SUCCESS;
    }

    auto &errorType = state.symbolTable.get(except.getTypeID());
    DSErrorKind kind = DS_ERROR_KIND_RUNTIME_ERROR;
    state.setExitStatus(1);
    if(errorType.is(TYPE::_ShellExit)) {
        kind = DS_ERROR_KIND_EXIT;
        state.setExitStatus(parseExitStatus(typeAs<ErrorObject>(except)));
    } else if(errorType.is(TYPE::_AssertFail)) {
        kind = DS_ERROR_KIND_ASSERTION_ERROR;
    }

    // get error line number
    unsigned int errorLineNum = 0;
    std::string sourceName;
    if(state.symbolTable.get(TYPE::Error).isSameOrBaseTypeOf(errorType) || kind != DS_ERROR_KIND_RUNTIME_ERROR) {
        auto &obj = typeAs<ErrorObject>(except);
        errorLineNum = getOccurredLineNum(obj.getStackTrace());
        const char *ptr = getOccurredSourceName(obj.getStackTrace());
        sourceName = ptr;
    }

    // print error message
    auto oldStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
    if(kind == DS_ERROR_KIND_RUNTIME_ERROR) {
        fputs("[runtime error]\n", stderr);
        const bool bt = state.symbolTable.get(TYPE::Error).isSameOrBaseTypeOf(errorType);
        auto *handle = state.symbolTable.lookupMethod(errorType, bt ? "backtrace" : OP_STR);

        DSValue ret = VM::callMethod(state, handle, DSValue(except), makeArgs());
        if(state.hasError()) {
            state.stack.clearThrownObject();
            fputs("cannot obtain string representation\n", stderr);
        } else if(!bt) {
            auto ref = ret.asStrRef();
            fwrite(ref.data(), sizeof(char), ref.size(), stderr);
            fputc('\n', stderr);
        }
    } else if(kind == DS_ERROR_KIND_ASSERTION_ERROR || hasFlag(state.runtimeOption, RuntimeOption::TRACE_EXIT)) {
        typeAs<ErrorObject>(except).printStackTrace(state);
    }
    fflush(stderr);
    state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(oldStatus));


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
            DSValue::createInt(termKind),
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