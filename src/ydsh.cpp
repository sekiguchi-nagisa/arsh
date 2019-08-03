/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include <cstring>
#include <algorithm>

#include <unistd.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <libgen.h>

#include <ydsh/ydsh.h>
#include <embed.h>

#include "vm.h"
#include "constant.h"
#include "logger.h"
#include "frontend.h"
#include "codegen.h"
#include "misc/num.h"

using namespace ydsh;

/**
 * if environmental variable SHLVL dose not exist, set 0.
 */
static unsigned int getShellLevel() {
    char *shlvl = getenv(ENV_SHLVL);
    unsigned int level = 0;
    if(shlvl != nullptr) {
        int status;
        long value = convertToInt64(shlvl, status);
        if(status != 0) {
            level = 0;
        } else {
            level = value;
        }
    }
    return level;
}

static unsigned int originalShellLevel() {
    static unsigned int level = getShellLevel();
    return level;
}

static void invokeTerminationHook(DSState &state, DSErrorKind kind, DSValue &&except) {
    DSValue funcObj = state.getGlobal(state.getTermHookIndex());
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

    setFlag(DSState::eventDesc, DSState::VM_EVENT_MASK);
    state.callFunction(std::move(funcObj), std::move(args));

    // restore old value
    state.setGlobal(toIndex(BuiltinVarOffset::EXIT_STATUS), std::move(oldExitStatus));
    unsetFlag(DSState::eventDesc, DSState::VM_EVENT_MASK);
}

/**
 * if called from child process, exit(1).
 * @param state
 * @param dsError
 * if not null, write error info
 * @return
 */
static DSErrorKind handleRuntimeError(DSState &state, DSError *dsError) {
    auto thrownObj = state.getThrownObject();
    auto &errorType = *thrownObj->getType();
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
        auto *obj = typeAs<Error_Object>(thrownObj);
        errorLineNum = getOccurredLineNum(obj->getStackTrace());
        const char *ptr = getOccurredSourceName(obj->getStackTrace());
        sourceName = ptr;
    }

    // print error message
    int oldStatus = state.getExitStatus();
    if(kind == DS_ERROR_KIND_RUNTIME_ERROR) {
        fputs("[runtime error]\n", stderr);
        const bool bt = state.symbolTable.get(TYPE::Error).isSameOrBaseTypeOf(errorType);
        auto *handle = errorType.lookupMethodHandle(state.symbolTable, bt ? "backtrace" : OP_STR);

        DSValue ret = state.callMethod(handle, DSValue(thrownObj), makeArgs());
        if(state.getThrownObject()) {
            fputs("cannot obtain string representation\n", stderr);
        } else if(!bt) {
            fwrite(typeAs<String_Object>(ret)->getValue(),
                    sizeof(char), typeAs<String_Object>(ret)->size(), stderr);
            fputc('\n', stderr);
        }
    } else if(kind == DS_ERROR_KIND_ASSERTION_ERROR || hasFlag(state.option, DS_OPTION_TRACE_EXIT)) {
        typeAs<Error_Object>(thrownObj)->printStackTrace(state);
    }
    fflush(stderr);
    state.updateExitStatus(oldStatus);

    // invoke termination hook.
    invokeTerminationHook(state, kind, std::move(thrownObj));


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

static int evalCodeImpl(DSState &state, const CompiledCode &code, DSError *dsError) {
    bool s = state.vmEval(code);
    bool root = state.isRootShell();
    if(!s) {
        auto kind = handleRuntimeError(state, dsError);
        if(kind == DS_ERROR_KIND_RUNTIME_ERROR && root) {
            state.symbolTable.abort(false);
        }
    } else if(!hasFlag(state.option, DS_OPTION_INTERACTIVE) || !root) {
        invokeTerminationHook(state, DS_ERROR_KIND_EXIT, DSValue());
    }
    state.symbolTable.commit();
    return state.getExitStatus();
}

static int evalCode(DSState &state, const CompiledCode &code, DSError *dsError) {
    if(state.dumpTarget.files[DS_DUMP_KIND_CODE]) {
        auto *fp = state.dumpTarget.files[DS_DUMP_KIND_CODE].get();
        fprintf(fp, "### dump compiled code ###\n");
        ByteCodeDumper(fp, state.symbolTable)(code);
    }

    if(state.execMode == DS_EXEC_MODE_COMPILE_ONLY) {
        return 0;
    }
    int ret = evalCodeImpl(state, code, dsError);
    if(!state.isRootShell()) {
        exit(ret);
    }
    return ret;
}

static const char *getScriptDir(const DSState &state, unsigned short option) {
    return hasFlag(option, DS_MOD_FULLPATH) ? "" : state.getScriptDir();
}

class Compiler {
private:
    FrontEnd frontEnd;
    ByteCodeGenerator codegen;

public:
    Compiler(const DSState &state, SymbolTable &symbolTable, Lexer &&lexer, unsigned short option) :
            frontEnd(getScriptDir(state, option), std::move(lexer), symbolTable, state.execMode,
                    hasFlag(state.option, DS_OPTION_TOPLEVEL),
                    state.dumpTarget, hasFlag(option, DS_MOD_IGNORE_ENOENT)),
            codegen(symbolTable, hasFlag(state.option, DS_OPTION_ASSERT)) {}

    unsigned int lineNum() const {
        return this->frontEnd.getRootLineNum();
    }

    int operator()(DSError *dsError, CompiledCode &code);
};

int Compiler::operator()(DSError *dsError, CompiledCode &code) {
    if(dsError != nullptr) {
        *dsError = {.kind = DS_ERROR_KIND_SUCCESS, .fileName = nullptr, .lineNum = 0, .name = nullptr};
    }

    this->frontEnd.setupASTDump();
    if(!this->frontEnd.frontEndOnly()) {
        this->codegen.initialize(this->frontEnd.getCurrentSourceInfo());
    }
    while(this->frontEnd) {
        auto ret = this->frontEnd(dsError);
        if(ret.first == nullptr && ret.second == FrontEnd::IN_MODULE) {
            this->frontEnd.getSymbolTable().abort();
            return 1;
        }

        if(this->frontEnd.frontEndOnly()) {
            continue;
        }

        switch(ret.second) {
        case FrontEnd::ENTER_MODULE:
            this->codegen.enterModule(this->frontEnd.getCurrentSourceInfo());
            break;
        case FrontEnd::EXIT_MODULE:
            this->codegen.exitModule(static_cast<SourceNode&>(*ret.first));
            break;
        case FrontEnd::IN_MODULE:
            this->codegen.generate(ret.first.get());
            break;
        }
    }
    this->frontEnd.teardownASTDump();
    if(!this->frontEnd.frontEndOnly()) {
        code = this->codegen.finalize();
    }
    return 0;
}

static int compile(DSState &state, Lexer &&lexer, DSError *dsError,
                   CompiledCode &code, unsigned short option) {
    Compiler compiler(state, state.symbolTable, std::move(lexer), option);
    int ret = compiler(dsError, code);
    state.lineNum = compiler.lineNum();
    return ret;
}

static int evalScript(DSState &state, Lexer &&lexer, DSError *dsError, unsigned short modOption = 0) {
    CompiledCode code;
    int ret = compile(state, std::move(lexer), dsError, code, modOption);
    if(!code) {
        return ret;
    }
    return evalCode(state, code, dsError);
}

static void bindVariable(DSState *state, const char *varName, DSValue &&value, FieldAttribute attribute) {
    auto handle = state->symbolTable.newHandle(varName, *value->getType(), attribute);
    assert(static_cast<bool>(handle));
    state->setGlobal(handle.asOk()->getIndex(), std::move(value));
}

static void bindVariable(DSState *state, const char *varName, DSValue &&value) {
    bindVariable(state, varName, std::move(value), FieldAttribute::READ_ONLY);
}

static void bindVariable(DSState *state, const char *varName, const DSValue &value) {
    bindVariable(state, varName, DSValue(value));
}

static void initBuiltinVar(DSState *state) {
    // set builtin variables internally used

#define XSTR(V) #V
#define STR(V) XSTR(V)
    /**
     * for version detection
     * must be String_Object
     */
    bindVariable(state, "YDSH_VERSION", DSValue::create<String_Object>(
            state->symbolTable.get(TYPE::String),
            STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "." STR(X_INFO_PATCH_VERSION)));
#undef XSTR
#undef STR

    /**
     * default variable for read command.
     * must be String_Object
     */
    bindVariable(state, "REPLY", state->emptyStrObj);

    std::vector<DSType *> types = {&state->symbolTable.get(TYPE::String), &state->symbolTable.get(TYPE::String)};

    /**
     * holding read variable.
     * must be Map_Object
     */
    bindVariable(state, "reply", DSValue::create<Map_Object>(
            *state->symbolTable.createReifiedType(state->symbolTable.getMapTemplate(), std::move(types)).take()));

    /**
     * process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PID", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Int32), getpid()));

    /**
     * parent process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PPID", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Int32), getppid()));

    /**
     * must be Long_Object.
     */
    bindVariable(state, "SECONDS", DSValue::create<Long_Object>(state->symbolTable.get(TYPE::Uint64), 0), FieldAttribute::SECONDS);

    /**
     * for internal field splitting.
     * must be String_Object.
     */
    bindVariable(state, "IFS", DSValue::create<String_Object>(state->symbolTable.get(TYPE::String), " \t\n"), FieldAttribute());

    /**
     * must be String_Object
     */
    bindVariable(state, "CONFIG_DIR", DSValue::create<String_Object>(state->symbolTable.get(TYPE::String), SYSTEM_CONFIG_DIR));

    /**
     * must be String_Object
     */
    std::string str = ".";
    getWorkingDir(*state, false, str);
    bindVariable(state, "SCRIPT_DIR", DSValue::create<String_Object>(state->symbolTable.get(TYPE::String), std::move(str)));

    /**
     * contains exit status of most recent executed process. ($?)
     * must be Int_Object
     */
    bindVariable(state, "?", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Int32), 0), FieldAttribute());

    /**
     * process id of root shell. ($$)
     * must be Int_Object
     */
    bindVariable(state, "$", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Int32), getpid()));

    /**
     * contains script argument(exclude script name). ($@)
     * must be Array_Object
     */
    bindVariable(state, "@", DSValue::create<Array_Object>(state->symbolTable.get(TYPE::StringArray)));

    /**
     * contains size of argument. ($#)
     * must be Int_Object
     */
    bindVariable(state, "#", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Int32), 0));

    /**
     * represent shell or shell script name.
     * must be String_Object
     */
    bindVariable(state, "0", DSValue::create<String_Object>(state->symbolTable.get(TYPE::String), "ydsh"));

    /**
     * initialize positional parameter
     */
    for(unsigned int i = 0; i < 9; i++) {
        bindVariable(state, std::to_string(i + 1).c_str(), state->emptyStrObj);
    }


    // set builtin variables

    /**
     * uid of shell
     * must be Int_Object
     */
    bindVariable(state, "UID", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Uint32), getuid()));

    /**
     * euid of shell
     * must be Int_Object
     */
    bindVariable(state, "EUID", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Uint32), geteuid()));

    struct utsname name{};
    if(uname(&name) == -1) {
        perror("cannot get utsname");
        exit(1);
    }

    /**
     * must be String_Object
     */
    bindVariable(state, "OSTYPE", DSValue::create<String_Object>(state->symbolTable.get(TYPE::String), name.sysname));

    /**
     * must be String_Object
     */
    bindVariable(state, "MACHTYPE", DSValue::create<String_Object>(state->symbolTable.get(TYPE::String), name.machine));

    /**
     * dummy object for random number
     * must be Int_Object
     */
    bindVariable(state, "RANDOM", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Uint32), 0),
                 FieldAttribute::READ_ONLY | FieldAttribute ::RANDOM);

    /**
     * dummy object for signal handler setting
     * must be DSObject
     */
    bindVariable(state, "SIG", DSValue::create<DSObject>(state->symbolTable.get(TYPE::Signals)));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDIN, DSValue::create<UnixFD_Object>(state->symbolTable.get(TYPE::UnixFD), STDIN_FILENO));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDOUT, DSValue::create<UnixFD_Object>(state->symbolTable.get(TYPE::UnixFD), STDOUT_FILENO));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDERR, DSValue::create<UnixFD_Object>(state->symbolTable.get(TYPE::UnixFD), STDERR_FILENO));

    /**
     * must be Int_Object
     */
    bindVariable(state, "ON_EXIT", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Int32), TERM_ON_EXIT));
    bindVariable(state, "ON_ERR", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Int32), TERM_ON_ERR));
    bindVariable(state, "ON_ASSERT", DSValue::create<Int_Object>(state->symbolTable.get(TYPE::Int32), TERM_ON_ASSERT));
}

static void loadEmbeddedScript(DSState *state) {
    int ret = DSState_eval(state, "(embed)", embed_script, strlen(embed_script), nullptr);
    (void) ret;
    assert(ret == 0);

    // rest some state
    state->lineNum = 1;
    state->updateExitStatus(0);
}

static void initEnv(const DSState &state) {
    // set locale
    setlocale(LC_ALL, "");
    setlocale(LC_MESSAGES, "C");

    // set environmental variables

    // update shell level
    setenv(ENV_SHLVL, std::to_string(originalShellLevel() + 1).c_str(), 1);

    // set HOME
    struct passwd *pw = getpwuid(getuid());
    if(pw == nullptr) {
        perror("getpwuid failed\n");
        exit(1);
    }
    setenv(ENV_HOME, pw->pw_dir, 0);

    // set LOGNAME
    setenv(ENV_LOGNAME, pw->pw_name, 0);

    // set USER
    setenv(ENV_USER, pw->pw_name, 0);

    // set PWD/OLDPWD
    std::string str;
    const char *ptr = getWorkingDir(state, true, str);
    if(ptr == nullptr) {
        ptr = ".";
    }
    setenv(ENV_PWD, ptr, 0);
    setenv(ENV_OLDPWD, ptr, 0);
}

// ###################################
// ##     public api of DSState     ##
// ###################################

DSState *DSState_createWithMode(DSExecMode mode) {
    auto *ctx = new DSState();

    initEnv(*ctx);
    initBuiltinVar(ctx);
    loadEmbeddedScript(ctx);

    ctx->execMode = mode;
    ctx->symbolTable.closeBuiltin();
    return ctx;
}

void DSState_delete(DSState **st) {
    if(st != nullptr) {
        delete (*st);
        *st = nullptr;
    }
}

DSExecMode DSState_mode(const DSState *st) {
    return st->execMode;
}

void DSState_setLineNum(DSState *st, unsigned int lineNum) {
    st->lineNum = lineNum;
}

unsigned int DSState_lineNum(const DSState *st) {
    return st->lineNum;
}

void DSState_setShellName(DSState *st, const char *shellName) {
    if(shellName != nullptr) {
        unsigned int index = toIndex(BuiltinVarOffset::POS_0);
        st->setGlobal(index, DSValue::create<String_Object>(st->symbolTable.get(TYPE::String), std::string(shellName)));
    }
}

// set positional parameters
static void finalizeScriptArg(DSState *st) {
    unsigned int index = toIndex(BuiltinVarOffset::ARGS);
    auto *array = typeAs<Array_Object>(st->getGlobal(index));

    // update argument size
    const unsigned int size = array->getValues().size();
    index = toIndex(BuiltinVarOffset::ARGS_SIZE);
    st->setGlobal(index, DSValue::create<Int_Object>(st->symbolTable.get(TYPE::Int32), size));

    unsigned int limit = 9;
    if(size < limit) {
        limit = size;
    }

    // update positional parameter
    for(index = 0; index < limit; index++) {
        unsigned int i = toIndex(BuiltinVarOffset::POS_1) + index;
        st->setGlobal(i, array->getValues()[index]);
    }

    if(index < 9) {
        for(; index < 9; index++) {
            unsigned int i = toIndex(BuiltinVarOffset::POS_1) + index;
            st->setGlobal(i, st->emptyStrObj);
        }
    }
}

void DSState_setArguments(DSState *st, char *const *args) {
    if(args == nullptr) {
        return;
    }

    // clear previous arguments
    typeAs<Array_Object>(st->getGlobal(BuiltinVarOffset::ARGS))->refValues().clear();

    for(unsigned int i = 0; args[i] != nullptr; i++) {
        auto *array = typeAs<Array_Object>(st->getGlobal(BuiltinVarOffset::ARGS));
        array->append(DSValue::create<String_Object>(st->symbolTable.get(TYPE::String), std::string(args[i])));
    }
    finalizeScriptArg(st);
}

/**
 *
 * @param st
 * @param scriptDir
 * full path
 */
static void setScriptDir(DSState *st, const char *scriptDir) {
    unsigned int index = toIndex(BuiltinVarOffset::SCRIPT_DIR);
    std::string str = scriptDir;
    st->setGlobal(index, DSValue::create<String_Object>(st->symbolTable.get(TYPE::String), std::move(str)));
}

int DSState_setScriptDir(DSState *st, const char *scriptDir) {
    char *real = realpath(scriptDir, nullptr);
    if(real == nullptr) {
        return -1;
    }
    setScriptDir(st, real);
    free(real);
    return 0;
}

int DSState_getExitStatus(const DSState *st) {
    return st->getExitStatus();
}

void DSState_setExitStatus(DSState *st, int status) {
    st->updateExitStatus(status);
}

int DSState_setDumpTarget(DSState *st, DSDumpKind kind, const char *target) {
    FilePtr file;
    if(target != nullptr) {
        file.reset(strlen(target) == 0 ? fdopen(fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0), "w") : fopen(target, "we"));
        if(!file) {
            return -1;
        }
    }
    st->dumpTarget.files[kind] = std::move(file);
    return 0;
}

unsigned short DSState_option(const DSState *st) {
    return st->option;
}

void DSState_setOption(DSState *st, unsigned short optionSet) {
    setFlag(st->option, optionSet);

    if(hasFlag(optionSet, DS_OPTION_JOB_CONTROL)) {
        setJobControlSignalSetting(*st, true);
    }
}

void DSState_unsetOption(DSState *st, unsigned short optionSet) {
    unsetFlag(st->option, optionSet);

    if(hasFlag(optionSet, DS_OPTION_JOB_CONTROL)) {
        setJobControlSignalSetting(*st, false);
    }
}

void DSError_release(DSError *e) {
    if(e != nullptr) {
        free(e->fileName);
        e->fileName = nullptr;
        free(e->name);
        e->name = nullptr;
    }
}

int DSState_eval(DSState *st, const char *sourceName, const char *data, unsigned int size, DSError *e) {
    Lexer lexer(sourceName == nullptr ? "(stdin)" : sourceName, data, size);
    lexer.setLineNum(st->lineNum);
    return evalScript(*st, std::move(lexer), e);
}

static void reportFileError(const char *sourceName, bool isIO, DSError *e) {
    int old = errno;
    fprintf(stderr, "ydsh: %s: %s, by `%s'\n",
            isIO ? "cannot read file" : "cannot open file", sourceName, strerror(old));
    if(e) {
        *e = {
                .kind = DS_ERROR_KIND_FILE_ERROR,
                .fileName = strdup(sourceName),
                .lineNum = 0,
                .name = strdup(strerror(old))
        };
    }
    errno = old;
}

int DSState_loadAndEval(DSState *st, const char *sourceName, DSError *e) {
    FilePtr filePtr;
    if(sourceName == nullptr) {
        filePtr = createFilePtr(fdopen, dup(STDIN_FILENO), "rb");
    } else {
        auto ret = st->symbolTable.tryToLoadModule(nullptr, sourceName, filePtr);
        if(is<ModLoadingError>(ret)) {
            if(get<ModLoadingError>(ret) == ModLoadingError::CIRCULAR) {
                errno = ETXTBSY;
            }
            reportFileError(sourceName, false, e);
            return 1;
        } else if(is<ModType *>(ret)) {
            return 0;   // do nothing.
        }
        char *real = strdup(get<const char *>(ret));
        const char *dirName = dirname(real);
        setScriptDir(st, dirName);
        free(real);
    }

    // read data
    assert(filePtr);
    ByteBuffer buf;
    sourceName = sourceName == nullptr ? "(stdin)" : sourceName;
    if(!readAll(filePtr, buf)) {
        reportFileError(sourceName, true, e);
        return 1;
    }
    filePtr.reset(nullptr);
    return evalScript(*st, Lexer(sourceName, std::move(buf)), e);
}

int DSState_loadModule(DSState *st, const char *fileName,
                       const char *varName, unsigned short option, DSError *e) {
    CompiledCode code;
    std::string line = "source ";
    line += fileName;
    if(varName != nullptr) {
        line += " as ";
        line += varName;
    }
    st->lineNum = 0;
    Lexer lexer("ydsh", line.c_str(), line.size());
    lexer.setLineNum(st->lineNum);
    return evalScript(*st, std::move(lexer), e, option);
}

int DSState_exec(DSState *st, char *const *argv) {
    if(st->execMode != DS_EXEC_MODE_NORMAL) {
        return 0;   // do nothing
    }
    if(!st->execCommand(argv)) {
        handleRuntimeError(*st, nullptr);
    }
    return st->getExitStatus();
}

const char *DSState_prompt(DSState *st, unsigned int n) {
    const char *psName = nullptr;
    if(n == 1) {
        psName = VAR_PS1;
    } else if(n == 2) {
        psName = VAR_PS2;
    } else {
        return "";
    }
    const DSValue &obj = getGlobal(*st, psName);
    st->prompt = interpretPromptString(*st, typeAs<String_Object>(obj)->getValue());
    return st->prompt.c_str();
}

const char *DSState_version(DSVersion *version) {
    if(version != nullptr) {
        version->major = X_INFO_MAJOR_VERSION;
        version->minor = X_INFO_MINOR_VERSION;
        version->patch = X_INFO_PATCH_VERSION;
    }
    return "ydsh, version " X_INFO_VERSION ", build by " X_INFO_CPP " " X_INFO_CPP_V;
}

const char *DSState_copyright() {
    return "Copyright (C) 2015-2019 Nagisa Sekiguchi";
}

const char *DSState_configDir() {
    return SYSTEM_CONFIG_DIR;
}

static constexpr unsigned int featureBit() {
    unsigned int featureBit = 0;

#ifdef USE_LOGGING
    setFlag(featureBit, DS_FEATURE_LOGGING);
#endif

#ifdef USE_SAFE_CAST
    setFlag(featureBit, DS_FEATURE_SAFE_CAST);
#endif

#ifdef USE_FIXED_TIME
    setFlag(featureBit, DS_FEATURE_FIXED_TIME);
#endif
    return featureBit;
}

unsigned int DSState_featureBit() {
    constexpr auto flag = featureBit();
    return flag;
}

DSCandidates *DSState_complete(DSState *st, const char *buf, size_t cursor) {
    if(st == nullptr || buf == nullptr || cursor == 0) {
        return nullptr;
    }

    std::string line(buf, cursor);
    LOG(DUMP_CONSOLE, "line: %s, cursor: %zu", line.c_str(), cursor);

    line += '\n';
    auto *can = new DSCandidates();
    *can = completeLine(*st, line);
    return can;
}

const char *DSCandidates_get(const DSCandidates *c, unsigned int index) {
    if(c != nullptr && index < c->buf.size()) {
        return c->buf[index];
    }
    return nullptr;
}

unsigned int DSCandidates_size(const DSCandidates *c) {
    return c != nullptr ? c->buf.size() : 0;
}

void DSCandidates_release(DSCandidates **c) {
    if(c != nullptr) {
        delete *c;
        *c = nullptr;
    }
}

DSHistory *DSState_history(DSState *st) {
    if(!st->history) {
        auto *handle = st->execMode != DS_EXEC_MODE_NORMAL ? nullptr : st->symbolTable.lookupHandle(VAR_HISTORY);
        st->history.initialize(
                handle != nullptr ? typeAs<Array_Object>(st->getGlobal(handle->getIndex())) : nullptr);
    }
    return st->history.hasValue() ? &st->history : nullptr;
}

unsigned int DSHistory_size(const DSHistory *history) {
    return history != nullptr ? history->get().size() : 0;
}

const char *DSHistory_get(const DSHistory *history, unsigned int index) {
    if(history != nullptr && index < history->get().size()) {
        return typeAs<String_Object>(history->get()[index])->getValue();
    }
    return nullptr;
}

void DSHistory_set(DSHistory *history, unsigned int index, const char *value) {
    if(history != nullptr && index < history->get().size()) {
        DSType *type = history->type()->getElementTypes()[0];
        history->get()[index] = DSValue::create<String_Object>(*type, value);
    }
}

void DSHistory_delete(DSHistory *history, unsigned int index) {
    if(history != nullptr && index < history->get().size()) {
        history->get().erase(history->get().begin() + index);
    }
}
