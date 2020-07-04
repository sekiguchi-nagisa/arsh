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

#include <ydsh/ydsh.h>
#include <embed.h>

#include "vm.h"
#include "logger.h"
#include "frontend.h"
#include "codegen.h"
#include "misc/files.h"

using namespace ydsh;

static int evalCode(DSState &state, const CompiledCode &code, DSError *dsError) {
    if(state.dumpTarget.files[DS_DUMP_KIND_CODE]) {
        auto *fp = state.dumpTarget.files[DS_DUMP_KIND_CODE].get();
        fprintf(fp, "### dump compiled code ###\n");
        ByteCodeDumper(fp, state.symbolTable)(code);
    }

    if(state.execMode == DS_EXEC_MODE_COMPILE_ONLY) {
        return 0;
    }
    return callToplevel(state, code, dsError);
}

static ErrorReporter newReporter() {
#ifdef FUZZING_BUILD_MODE
    bool ignore = getenv("YDSH_SUPPRESS_COMPILE_ERROR") != nullptr;
    return ErrorReporter(ignore ? fopen("/dev/null", "w") : stderr, ignore);
#else
    return ErrorReporter(stderr, false);
#endif
}

class Compiler {
private:
    FrontEnd frontEnd;
    ErrorReporter reporter;
    NodeDumper uastDumper;
    NodeDumper astDumper;
    ByteCodeGenerator codegen;

public:
    Compiler(const DSState &state, SymbolTable &symbolTable, Lexer &&lexer) :
            frontEnd(std::move(lexer), symbolTable, state.execMode,
                     hasFlag(state.compileOption, CompileOption::INTERACTIVE)),
            reporter(newReporter()),
            uastDumper(state.dumpTarget.files[DS_DUMP_KIND_UAST].get(), symbolTable),
            astDumper(state.dumpTarget.files[DS_DUMP_KIND_AST].get(), symbolTable),
            codegen(symbolTable, hasFlag(state.compileOption, CompileOption::ASSERT)) {
        this->frontEnd.setErrorReporter(this->reporter);
        if(this->uastDumper) {
            this->frontEnd.setUASTDumper(this->uastDumper);
        }
        if(this->astDumper) {
            this->frontEnd.setASTDumper(this->astDumper);
        }
    }

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
        this->codegen.initialize(this->frontEnd.getCurrentLexer());
    }
    while(this->frontEnd) {
        auto ret = this->frontEnd(dsError);
        if(!ret) {
            this->frontEnd.getSymbolTable().abort();
            return 1;
        }

        if(this->frontEnd.frontEndOnly()) {
            continue;
        }

        switch(ret.status) {
        case FrontEnd::ENTER_MODULE:
            this->codegen.enterModule(this->frontEnd.getCurrentLexer());
            break;
        case FrontEnd::EXIT_MODULE:
            if(!this->codegen.exitModule(cast<SourceNode>(*ret.node))) {
                goto END;
            }
            break;
        case FrontEnd::IN_MODULE:
            if(!this->codegen.generate(ret.node.get())) {
                goto END;
            }
            break;
        default:
            break;
        }
    }
    this->frontEnd.teardownASTDump();
    if(!this->frontEnd.frontEndOnly()) {
        code = this->codegen.finalize();
    }

    END:
    if(this->codegen.hasError()) {
        auto &e = this->codegen.getError();
        this->frontEnd.handleError(DS_ERROR_KIND_CODEGEN_ERROR,
                e.getKind(), e.getToken(), e.getMessage(), dsError);
        this->frontEnd.getSymbolTable().abort();
        return 1;
    }
    return 0;
}

static int compile(DSState &state, Lexer &&lexer, DSError *dsError, CompiledCode &code) {
    Compiler compiler(state, state.symbolTable, std::move(lexer));
    int ret = compiler(dsError, code);
    state.lineNum = compiler.lineNum();
    return ret;
}

static int evalScript(DSState &state, Lexer &&lexer, DSError *dsError) {
    CompiledCode code;
    int ret = compile(state, std::move(lexer), dsError, code);
    if(!code) {
        return ret;
    }
    return evalCode(state, code, dsError);
}

static void bindVariable(DSState &state, const char *varName,
        DSType *type, DSValue &&value, FieldAttribute attribute) {
    assert(type != nullptr);
    auto handle = state.symbolTable.newHandle(varName, *type, attribute);
    assert(static_cast<bool>(handle));
    state.setGlobal(handle.asOk()->getIndex(), std::move(value));
}

static void bindVariable(DSState &state, const char *varName, DSValue &&value, FieldAttribute attribute) {
    auto id = value.getTypeID();
    return bindVariable(state, varName, &state.symbolTable.get(id), std::move(value), attribute);
}

static void bindVariable(DSState &state, const char *varName, DSValue &&value) {
    bindVariable(state, varName, std::move(value), FieldAttribute::READ_ONLY);
}

static void initBuiltinVar(DSState &state) {
    // set builtin variables internally used

    /**
     * dummy object.
     * must be String_Object
     */
    bindVariable(state, CVAR_SCRIPT_NAME, DSValue::createStr(),
                 FieldAttribute::MOD_CONST | FieldAttribute::READ_ONLY);

    /**
     * dummy object
     * must be String_Object
     */
    bindVariable(state, CVAR_SCRIPT_DIR, DSValue::createStr(),
            FieldAttribute::MOD_CONST | FieldAttribute::READ_ONLY);

    /**
     * default variable for read command.
     * must be String_Object
     */
    bindVariable(state, "REPLY", DSValue::createStr(), FieldAttribute());

    /**
     * holding read variable.
     * must be Map_Object
     */
    bindVariable(state, "reply", DSValue::create<MapObject>(
            *state.symbolTable.createMapType(state.symbolTable.get(TYPE::String),
                    state.symbolTable.get(TYPE::String)).take()));

    /**
     * process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PID", DSValue::createInt(getpid()));

    /**
     * parent process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PPID", DSValue::createInt(getppid()));

    /**
     * must be Long_Object.
     */
    bindVariable(state, "SECONDS", DSValue::createInt(0), FieldAttribute::SECONDS);

    /**
     * for internal field splitting.
     * must be String_Object.
     */
    bindVariable(state, "IFS", DSValue::createStr(" \t\n"), FieldAttribute());

    /**
     * maintain completion result.
     * must be Array_Object
     */
    bindVariable(state, "COMPREPLY", DSValue::create<ArrayObject>(state.symbolTable.get(TYPE::StringArray)));

    /**
     * contains latest executed pipeline status.
     * must be Array_Object
     */
    bindVariable(state, "PIPESTATUS", DSValue::create<ArrayObject>(
            *state.symbolTable.createArrayType(state.symbolTable.get(TYPE::Int)).take()));

    /**
     * contains exit status of most recent executed process. ($?)
     * must be Int_Object
     */
    bindVariable(state, "?", DSValue::createInt(0), FieldAttribute());

    /**
     * process id of root shell. ($$)
     * must be Int_Object
     */
    bindVariable(state, "$", DSValue::createInt(getpid()));

    /**
     * contains script argument(exclude script name). ($@)
     * must be Array_Object
     */
    bindVariable(state, "@", DSValue::create<ArrayObject>(state.symbolTable.get(TYPE::StringArray)));

    /**
     * contains size of argument. ($#)
     * must be Int_Object
     */
    bindVariable(state, "#", DSValue::createInt(0));

    /**
     * represent shell or shell script name.
     * must be String_Object
     */
    bindVariable(state, "0", DSValue::createStr("ydsh"));

    /**
     * initialize positional parameter
     */
    for(unsigned int i = 0; i < 9; i++) {
        bindVariable(state, std::to_string(i + 1).c_str(), DSValue::createStr());
    }


    // set builtin variables
    /**
     * for version detection
     * must be String_Object
     */
    bindVariable(state, CVAR_VERSION, DSValue::createStr(X_INFO_VERSION_CORE));

    /**
     * uid of shell
     * must be Int_Object
     */
    bindVariable(state, "UID", DSValue::createInt(getuid()));

    /**
     * euid of shell
     * must be Int_Object
     */
    bindVariable(state, "EUID", DSValue::createInt(geteuid()));

    struct utsname name{};
    if(uname(&name) == -1) {
        fatal_perror("cannot get utsname");
    }

    /**
     * must be String_Object
     */
    bindVariable(state, CVAR_OSTYPE, DSValue::createStr(name.sysname));

    /**
     * must be String_Object
     */
    bindVariable(state, CVAR_MACHTYPE, DSValue::createStr(BUILD_ARCH));

    /**
     * must be String_Object
     */
    bindVariable(state, CVAR_CONFIG_DIR, DSValue::createStr(SYSTEM_CONFIG_DIR));

    /**
     * dummy object for random number
     * must be Int_Object
     */
    bindVariable(state, "RANDOM", DSValue::createInt(0),
                 FieldAttribute::READ_ONLY | FieldAttribute ::RANDOM);

    /**
     * dummy object for signal handler setting
     * must be DSObject
     */
    bindVariable(state, "SIG", DSValue::createDummy(state.symbolTable.get(TYPE::Signals)));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDIN, DSValue::create<UnixFdObject>(STDIN_FILENO));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDOUT, DSValue::create<UnixFdObject>(STDOUT_FILENO));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDERR, DSValue::create<UnixFdObject>(STDERR_FILENO));

    /**
     * must be Int_Object
     */
    bindVariable(state, "ON_EXIT", DSValue::createInt(TERM_ON_EXIT));
    bindVariable(state, "ON_ERR", DSValue::createInt(TERM_ON_ERR));
    bindVariable(state, "ON_ASSERT", DSValue::createInt(TERM_ON_ASSERT));
}

static void loadEmbeddedScript(DSState *state) {
    int ret = DSState_eval(state, "(embed)", embed_script, strlen(embed_script), nullptr);
    (void) ret;
    assert(ret == 0);

    // rest some state
    state->lineNum = 1;
    state->setExitStatus(0);

    // force initialize 'termHookIndex'
    state->symbolTable.getTermHookIndex();
}

// ###################################
// ##     public api of DSState     ##
// ###################################

DSState *DSState_createWithMode(DSExecMode mode) {
    auto *ctx = new DSState();
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
        st->setGlobal(BuiltinVarOffset::POS_0, DSValue::createStr(shellName));
    }
}

// set positional parameters
static void finalizeScriptArg(DSState *st) {
    auto &array = typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::ARGS));

    // update argument size
    const unsigned int size = array.getValues().size();
    st->setGlobal(BuiltinVarOffset::ARGS_SIZE, DSValue::createInt(size));

    unsigned int limit = 9;
    if(size < limit) {
        limit = size;
    }

    // update positional parameter
    unsigned int index = 0;
    for(; index < limit; index++) {
        unsigned int i = toIndex(BuiltinVarOffset::POS_1) + index;
        st->setGlobal(i, array.getValues()[index]);
    }

    if(index < 9) {
        for(; index < 9; index++) {
            unsigned int i = toIndex(BuiltinVarOffset::POS_1) + index;
            st->setGlobal(i, DSValue::createStr());
        }
    }
}

void DSState_setArguments(DSState *st, char *const *args) {
    if(args == nullptr) {
        return;
    }

    // clear previous arguments
    typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::ARGS)).refValues().clear();

    for(unsigned int i = 0; args[i] != nullptr; i++) {
        auto &array = typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::ARGS));
        array.append(DSValue::createStr(args[i]));
    }
    finalizeScriptArg(st);
}

int DSState_getExitStatus(const DSState *st) {
    return st->getMaskedExitStatus();
}

void DSState_setExitStatus(DSState *st, int status) {
    st->setExitStatus(status);
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
    unsigned short option = 0;

    // get compile option
    if(hasFlag(st->compileOption, CompileOption::ASSERT)) {
        setFlag(option, DS_OPTION_ASSERT);
    }
    if(hasFlag(st->compileOption, CompileOption::INTERACTIVE)) {
        setFlag(option, DS_OPTION_INTERACTIVE);
    }

    // get runtime option
    if(hasFlag(st->runtimeOption, RuntimeOption::TRACE_EXIT)) {
        setFlag(option, DS_OPTION_TRACE_EXIT);
    }
    if(hasFlag(st->runtimeOption, RuntimeOption::MONITOR)) {
        setFlag(option, DS_OPTION_JOB_CONTROL);
    }
    return option;
}

void DSState_setOption(DSState *st, unsigned short optionSet) {
    // set compile option
    if(hasFlag(optionSet, DS_OPTION_ASSERT)) {
        setFlag(st->compileOption, CompileOption::ASSERT);
    }
    if(hasFlag(optionSet, DS_OPTION_INTERACTIVE)) {
        setFlag(st->compileOption, CompileOption::INTERACTIVE);
    }

    // set runtime option
    if(hasFlag(optionSet, DS_OPTION_TRACE_EXIT)) {
        setFlag(st->runtimeOption, RuntimeOption::TRACE_EXIT);
    }
    if(hasFlag(optionSet, DS_OPTION_JOB_CONTROL)) {
        setFlag(st->runtimeOption, RuntimeOption::MONITOR);
        setJobControlSignalSetting(*st, true);
    }
}

void DSState_unsetOption(DSState *st, unsigned short optionSet) {
    // unset compile option
    if(hasFlag(optionSet, DS_OPTION_ASSERT)) {
        unsetFlag(st->compileOption, CompileOption::ASSERT);
    }
    if(hasFlag(optionSet, DS_OPTION_INTERACTIVE)) {
        unsetFlag(st->compileOption, CompileOption::INTERACTIVE);
    }

    // unset runtime option
    if(hasFlag(optionSet, DS_OPTION_TRACE_EXIT)) {
        unsetFlag(st->runtimeOption, RuntimeOption::TRACE_EXIT);
    }
    if(hasFlag(optionSet, DS_OPTION_JOB_CONTROL)) {
        unsetFlag(st->runtimeOption, RuntimeOption::MONITOR);
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
    Lexer lexer(sourceName == nullptr ? "(stdin)" : sourceName,
            ByteBuffer(data, data + size), getCWD());
    lexer.setLineNumOffset(st->lineNum);
    return evalScript(*st, std::move(lexer), e);
}

static void reportFileError(const char *sourceName, bool isIO, int errNum, DSError *e) {
    fprintf(stderr, "ydsh: %s: %s, by `%s'\n",
            isIO ? "cannot read file" : "cannot open file", sourceName, strerror(errNum));
    if(e) {
        *e = {
                .kind = DS_ERROR_KIND_FILE_ERROR,
                .fileName = strdup(sourceName),
                .lineNum = 0,
                .name = strdup(strerror(errNum))
        };
    }
    errno = errNum;
}

int DSState_loadAndEval(DSState *st, const char *sourceName, DSError *e) {
    FilePtr filePtr;
    CStrPtr scriptDir;
    if(sourceName == nullptr) {
        scriptDir = getCWD();
        filePtr = createFilePtr(fdopen, dup(STDIN_FILENO), "rb");
    } else {
        auto ret = st->symbolTable.tryToLoadModule(nullptr, sourceName, filePtr, ModLoadOption{});
        if(is<ModLoadingError>(ret)) {
            int errNum = get<ModLoadingError>(ret).getErrNo();
            if(get<ModLoadingError>(ret).isCircularLoad()) {
                errNum = ETXTBSY;
            }
            reportFileError(sourceName, false, errNum, e);
            return 1;
        } else if(is<unsigned int>(ret)) {
            return 0;   // do nothing.
        }
        char *real = strdup(get<const char *>(ret));
        assert(*real == '/');
        const char *ptr = strrchr(real, '/');
        real[ptr == real ? 1 : (ptr - real)] = '\0';
        scriptDir.reset(real);
    }

    // read data
    assert(filePtr);
    ByteBuffer buf;
    sourceName = sourceName == nullptr ? "(stdin)" : sourceName;
    if(!readAll(filePtr, buf)) {
        reportFileError(sourceName, true, errno, e);
        return 1;
    }
    filePtr.reset(nullptr);
    return evalScript(*st, Lexer(sourceName, std::move(buf), std::move(scriptDir)), e);
}

static void appendAsEscaped(std::string &line, const char *path) {
    line += '"';
    while(*path) {
        int ch = *(path++);
        switch(ch) {
        case '"': case '$': case '\\':
            line +='\\';
            break;
        default:
            break;
        }
        line += static_cast<char>(ch);
    }
    line += '"';
}

int DSState_loadModule(DSState *st, const char *fileName, unsigned short option, DSError *e) {
    CStrPtr scriptDir;
    if(!hasFlag(option, DS_MOD_FULLPATH)) {
        scriptDir = getCWD();
    }

    std::string line = "source";
    line += hasFlag(option, DS_MOD_IGNORE_ENOENT) ? "! " : " ";
    appendAsEscaped(line, fileName);

    st->lineNum = 0;
    Lexer lexer("ydsh", ByteBuffer(line.c_str(), line.c_str() + line.size()), std::move(scriptDir));
    lexer.setLineNumOffset(st->lineNum);
    return evalScript(*st, std::move(lexer), e);
}

int DSState_exec(DSState *st, char *const *argv) {
    if(st->execMode != DS_EXEC_MODE_NORMAL) {
        return 0;   // do nothing.
    }

    std::vector<DSValue> values;
    for(; *argv != nullptr; argv++) {
        values.push_back(DSValue::createStr(*argv));
    }
    execCommand(*st, std::move(values), false);
    return st->getMaskedExitStatus();
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
    return "Copyright (C) 2015-2020 Nagisa Sekiguchi";
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

    auto &compreply = typeAs<ArrayObject>(st->getGlobal(BuiltinVarOffset::COMPREPLY));

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
        if(index < compreply.getValues().size()) {
            *value = compreply.getValues()[index].asCStr();
        }
        break;
    case DS_COMP_SIZE:
        return compreply.getValues().size();
    case DS_COMP_CLEAR:
        compreply.refValues().clear();
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
            DSValue::createInt(op), DSValue::createInt(index),
            DSValue::createStr((value && *value) ? value : "")
    );
    auto old = st->getGlobal(BuiltinVarOffset::EXIT_STATUS);
    st->editOpReply = callFunction(*st, std::move(func), std::move(args));
    st->setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(old));
    if(st->hasError()) {
        return 0;
    }

    auto &type = st->symbolTable.get(st->editOpReply.getTypeID());
    switch(op) {
    case DS_EDIT_HIST_SIZE:
        if(type.is(TYPE::Int)) {
            auto ret = st->editOpReply.asInt();
            return ret <= 0 ? 0 : static_cast<unsigned int>(ret);
        }
        return 0;
    case DS_EDIT_HIST_GET:
    case DS_EDIT_HIST_SEARCH:
    case DS_EDIT_PROMPT:
        if(!type.is(TYPE::String) || buf == nullptr) {
            return 0;
        }
        if(op == DS_EDIT_PROMPT) {
            st->prompt = st->editOpReply;
            *buf = st->prompt.asCStr();
        } else {
            *buf = st->editOpReply.asCStr();
        }
        break;
    default:
        break;
    }
    return 1;
}