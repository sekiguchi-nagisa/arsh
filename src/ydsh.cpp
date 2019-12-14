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
#include <cassert>

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
#include "misc/num_util.hpp"

using namespace ydsh;

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

static int evalCode(DSState &state, const CompiledCode &code, DSError *dsError) {
    if(state.dumpTarget.files[DS_DUMP_KIND_CODE]) {
        auto *fp = state.dumpTarget.files[DS_DUMP_KIND_CODE].get();
        fprintf(fp, "### dump compiled code ###\n");
        ByteCodeDumper(fp, state.symbolTable)(code);
    }

    if(state.execMode == DS_EXEC_MODE_COMPILE_ONLY) {
        return 0;
    }
    return state.callToplevel(code, dsError);
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
                    hasFlag(state.option, DS_OPTION_TOPLEVEL), state.dumpTarget),
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

static void bindVariable(DSState &state, const char *varName, DSValue &&value, FieldAttribute attribute) {
    auto handle = state.symbolTable.newHandle(varName, *value->getType(), attribute);
    assert(static_cast<bool>(handle));
    state.setGlobal(handle.asOk()->getIndex(), std::move(value));
}

static void bindVariable(DSState &state, const char *varName, DSValue &&value) {
    bindVariable(state, varName, std::move(value), FieldAttribute::READ_ONLY);
}

static void bindVariable(DSState &state, const char *varName, const DSValue &value) {
    bindVariable(state, varName, DSValue(value));
}

static void initBuiltinVar(DSState &state) {
    // set builtin variables internally used

#define XSTR(V) #V
#define STR(V) XSTR(V)
    /**
     * for version detection
     * must be String_Object
     */
    bindVariable(state, "YDSH_VERSION", DSValue::create<String_Object>(
            state.symbolTable.get(TYPE::String),
            STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "." STR(X_INFO_PATCH_VERSION)));
#undef XSTR
#undef STR

    /**
     * default variable for read command.
     * must be String_Object
     */
    bindVariable(state, "REPLY", DSValue(state.emptyStrObj), FieldAttribute());

    std::vector<DSType *> types = {&state.symbolTable.get(TYPE::String), &state.symbolTable.get(TYPE::String)};

    /**
     * holding read variable.
     * must be Map_Object
     */
    bindVariable(state, "reply", DSValue::create<Map_Object>(
            *state.symbolTable.createReifiedType(state.symbolTable.getMapTemplate(), std::move(types)).take()));

    /**
     * process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PID", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), getpid()));

    /**
     * parent process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PPID", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), getppid()));

    /**
     * must be Long_Object.
     */
    bindVariable(state, "SECONDS", DSValue::create<Long_Object>(state.symbolTable.get(TYPE::Int64), 0), FieldAttribute::SECONDS);

    /**
     * for internal field splitting.
     * must be String_Object.
     */
    bindVariable(state, "IFS", DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), " \t\n"), FieldAttribute());

    /**
     * must be String_Object
     */
    std::string str = ".";
    getWorkingDir(state, false, str);
    bindVariable(state, "SCRIPT_DIR", DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), std::move(str)));

    /**
     * maintain completion result.
     * must be Array_Object
     */
    bindVariable(state, "COMPREPLY", DSValue::create<Array_Object>(state.symbolTable.get(TYPE::StringArray)));

    /**
     * contains latest executed pipeline status.
     * must be Array_Object
     */
    bindVariable(state, "PIPESTATUS", DSValue::create<Array_Object>(
            *state.symbolTable.createReifiedType(
                    state.symbolTable.getArrayTemplate(),
                    {&state.symbolTable.get(TYPE::Int32)}).take()));

    /**
     * contains exit status of most recent executed process. ($?)
     * must be Int_Object
     */
    bindVariable(state, "?", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), 0), FieldAttribute());

    /**
     * process id of root shell. ($$)
     * must be Int_Object
     */
    bindVariable(state, "$", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), getpid()));

    /**
     * contains script argument(exclude script name). ($@)
     * must be Array_Object
     */
    bindVariable(state, "@", DSValue::create<Array_Object>(state.symbolTable.get(TYPE::StringArray)));

    /**
     * contains size of argument. ($#)
     * must be Int_Object
     */
    bindVariable(state, "#", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), 0));

    /**
     * represent shell or shell script name.
     * must be String_Object
     */
    bindVariable(state, "0", DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), "ydsh"));

    /**
     * initialize positional parameter
     */
    for(unsigned int i = 0; i < 9; i++) {
        bindVariable(state, std::to_string(i + 1).c_str(), state.emptyStrObj);
    }


    // set builtin variables

    /**
     * uid of shell
     * must be Int_Object
     */
    bindVariable(state, "UID", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), getuid()));

    /**
     * euid of shell
     * must be Int_Object
     */
    bindVariable(state, "EUID", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), geteuid()));

    struct utsname name{};
    if(uname(&name) == -1) {
        perror("cannot get utsname");
        exit(1);
    }

    /**
     * must be String_Object
     */
    bindVariable(state, "OSTYPE", DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), name.sysname));

    /**
     * must be String_Object
     */
    bindVariable(state, "MACHTYPE", DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), name.machine));

    /**
     * must be String_Object
     */
    bindVariable(state, "CONFIG_DIR", DSValue::create<String_Object>(state.symbolTable.get(TYPE::String), SYSTEM_CONFIG_DIR));

    /**
     * dummy object for random number
     * must be Int_Object
     */
    bindVariable(state, "RANDOM", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), 0),
                 FieldAttribute::READ_ONLY | FieldAttribute ::RANDOM);

    /**
     * dummy object for signal handler setting
     * must be DSObject
     */
    bindVariable(state, "SIG", DSValue::create<DSObject>(state.symbolTable.get(TYPE::Signals)));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDIN, DSValue::create<UnixFD_Object>(state.symbolTable.get(TYPE::UnixFD), STDIN_FILENO));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDOUT, DSValue::create<UnixFD_Object>(state.symbolTable.get(TYPE::UnixFD), STDOUT_FILENO));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDERR, DSValue::create<UnixFD_Object>(state.symbolTable.get(TYPE::UnixFD), STDERR_FILENO));

    /**
     * must be Int_Object
     */
    bindVariable(state, "ON_EXIT", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), TERM_ON_EXIT));
    bindVariable(state, "ON_ERR", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), TERM_ON_ERR));
    bindVariable(state, "ON_ASSERT", DSValue::create<Int_Object>(state.symbolTable.get(TYPE::Int32), TERM_ON_ASSERT));
}

static void loadEmbeddedScript(DSState *state) {
    int ret = DSState_eval(state, "(embed)", embed_script, strlen(embed_script), nullptr);
    (void) ret;
    assert(ret == 0);

    // rest some state
    state->lineNum = 1;
    state->updateExitStatus(0);

    // force initialize 'termHookIndex'
    state->symbolTable.getTermHookIndex();
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
    initBuiltinVar(*ctx);
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
        st->setGlobal(BuiltinVarOffset::POS_0,
                DSValue::create<String_Object>(st->symbolTable.get(TYPE::String), std::string(shellName)));
    }
}

// set positional parameters
static void finalizeScriptArg(DSState *st) {
    auto *array = typeAs<Array_Object>(st->getGlobal(BuiltinVarOffset::ARGS));

    // update argument size
    const unsigned int size = array->getValues().size();
    st->setGlobal(BuiltinVarOffset::ARGS_SIZE,
            DSValue::create<Int_Object>(st->symbolTable.get(TYPE::Int32), size));

    unsigned int limit = 9;
    if(size < limit) {
        limit = size;
    }

    // update positional parameter
    unsigned int index = 0;
    for(; index < limit; index++) {
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
    std::string str = scriptDir;
    st->setGlobal(BuiltinVarOffset::SCRIPT_DIR,
            DSValue::create<String_Object>(st->symbolTable.get(TYPE::String), std::move(str)));
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

static void appendAsEscaped(std::string &line, const char *path) {  //FIXME: escape newline
    while(*path) {
        int ch = *(path++);
        switch(ch) {
        case ' ': case '\t': case '\r': case '\n':
        case '\\': case ';': case '\'': case '"':
        case '`': case '|': case '&': case '<':
        case '>': case '(': case ')': case '$':
        case '#':
            line +='\\';
            break;
        default:
            break;
        }
        line += static_cast<char>(ch);
    }
}

int DSState_loadModule(DSState *st, const char *fileName, unsigned short option, DSError *e) {
    CompiledCode code;
    std::string line = "source";
    line += hasFlag(option, DS_MOD_IGNORE_ENOENT) ? "! " : " ";
    appendAsEscaped(line, fileName);
    st->lineNum = 0;
    Lexer lexer("ydsh", line.c_str(), line.size());
    lexer.setLineNum(st->lineNum);
    return evalScript(*st, std::move(lexer), e, option);
}

int DSState_exec(DSState *st, char *const *argv) {
    if(st->execMode != DS_EXEC_MODE_NORMAL) {
        return 0;   // do nothing.
    }

    std::vector<DSValue> values;
    for(; *argv != nullptr; argv++) {
        values.push_back(DSValue::create<String_Object>(st->symbolTable.get(TYPE::String), std::string(*argv)));
    }
    st->execCommand(std::move(values), false);
    return st->getExitStatus();
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
    return featureBit;
}

unsigned int DSState_featureBit() {
    constexpr auto flag = featureBit();
    return flag;
}

unsigned int DSState_completionOp(DSState *st, DSCompletionOp op, unsigned int index, const char **value) {
    if(st == nullptr) {
        return 0;
    }

    auto *compreply = typeAs<Array_Object>(st->getGlobal(BuiltinVarOffset::COMPREPLY));

    switch(op) {
    case DS_COMP_INVOKE: {
        if(value == nullptr || *value == nullptr || index == 0) {
            return 0;
        }

        auto old = st->getGlobal(BuiltinVarOffset::EXIT_STATUS);
        completeLine(*st, *value, index);
        st->setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(old));
        break;
    }
    case DS_COMP_GET:
        if(value == nullptr) {
            break;
        }
        *value = nullptr;
        if(index < compreply->getValues().size()) {
            *value = typeAs<String_Object>(compreply->getValues()[index])->getValue();
        }
        break;
    case DS_COMP_SIZE:
        return compreply->getValues().size();
    case DS_COMP_CLEAR:
        compreply->refValues().clear();
        break;
    }
    return 0;
}

#define XSTR(v) #v
#define STR(v) XSTR(v)

static const char *defaultPrompt(int n) {
    switch(n) {
    case 1:
        if(getuid()) {
            return "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "$ ";
        } else {
            return "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "# ";
        }
    case 2:
        return "> ";
    default:
        return "";
    }
}

#undef XSTR
#undef STR

unsigned int DSState_lineEditOp(DSState *st, DSLineEditOp op, int index, const char **buf) {
    const char *value = nullptr;
    if(buf) {
        value = *buf;
        *buf = nullptr;
    }

    if(st == nullptr) {
        return 0;
    }

    auto func = getGlobal(*st, VAR_EIDT_HOOK);
    if(func.isInvalid()) {
        if(op == DS_EDIT_PROMPT) {
            *buf = defaultPrompt(index);
        }
        return 0;
    }

    auto args = makeArgs(
            DSValue::create<Int_Object>(st->symbolTable.get(TYPE::Int32), op),
            DSValue::create<Int_Object>(st->symbolTable.get(TYPE::Int32), index),
            (value && *value) ? DSValue::create<String_Object>(st->symbolTable.get(TYPE::String), value)
                    : st->emptyStrObj
    );
    auto old = st->getGlobal(BuiltinVarOffset::EXIT_STATUS);
    st->editOpReply = st->callFunction(std::move(func), std::move(args));
    st->setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(old));
    if(st->hasError()) {
        return 0;
    }

    auto *type = st->editOpReply->getType();
    switch(op) {
    case DS_EDIT_HIST_SIZE:
        if(type->is(TYPE::Int32)) {
            return typeAs<Int_Object>(st->editOpReply)->getValue();
        }
        return 0;
    case DS_EDIT_HIST_GET:
    case DS_EDIT_HIST_SEARCH:
    case DS_EDIT_PROMPT:
        if(!type->is(TYPE::String) || buf == nullptr) {
            return 0;
        }
        if(op == DS_EDIT_PROMPT) {
            st->prompt = st->editOpReply;
        }
        *buf = typeAs<String_Object>(st->editOpReply)->getValue();
        break;
    default:
        break;
    }
    return 1;
}