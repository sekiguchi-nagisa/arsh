/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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
#include <pwd.h>
#include <csignal>
#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <unistd.h>
#include <sys/utsname.h>

#include <ydsh/ydsh.h>
#include <config.h>
#include <embed.h>

#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "vm.h"
#include "symbol.h"
#include "logger.h"
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


static void setErrorInfo(DSError *error, DSErrorKind type, unsigned int lineNum, const char *errorName) {
    if(error != nullptr) {
        error->kind = type;
        error->lineNum = lineNum;
        error->name = errorName;
    }
}

/**
 * not allow dumb terminal
 */
static bool isSupportedTerminal(int fd) {
    const char *term = getenv(ENV_TERM);
    return term != nullptr && strcasecmp(term, "dumb") != 0 && isatty(fd) != 0;
}

#define EACH_TERM_COLOR(C) \
    C(Reset,    0) \
    C(Bold,     1) \
    /*C(Black,   30)*/ \
    /*C(Red,     31)*/ \
    C(Green,   32) \
    /*C(Yellow,  33)*/ \
    /*C(Blue,    34)*/ \
    C(Magenta, 35) \
    C(Cyan,    36) /*\
    C(white,   37)*/

enum class TermColor : unsigned int {   // ansi color code
#define GEN_ENUM(E, N) E,
    EACH_TERM_COLOR(GEN_ENUM)
#undef GEN_ENUM
};

static const char *color(TermColor color, bool isatty) {
    if(isatty) {
#define GEN_STR(E, C) "\033[" #C "m",
        const char *ansi[] = {
                EACH_TERM_COLOR(GEN_STR)
        };
#undef GEN_STR
        return ansi[static_cast<unsigned int>(color)];
    }
    return "";
}

static std::vector<std::string> split(const std::string &str) {
    std::vector<std::string> bufs;
    bufs.emplace_back();
    for(auto ch : str) {
        if(ch == '\n') {
            bufs.emplace_back();
        } else {
            bufs.back() += ch;
        }
    }
    return bufs;
}

static void formatErrorLine(bool isatty, const Lexer &lexer, Token errorToken) {
    errorToken = lexer.shiftEOS(errorToken);
    Token lineToken = lexer.getLineToken(errorToken);
    auto line = lexer.toTokenText(lineToken);
    auto marker = lexer.formatLineMarker(lineToken, errorToken);

    auto lines = split(line);
    auto markers = split(marker);
    unsigned int size = lines.size();
    assert(size == markers.size());
    for(unsigned int i = 0; i < size; i++) {
        // print error line
        fprintf(stderr, "%s%s%s\n", color(TermColor::Cyan, isatty),
                lines[i].c_str(), color(TermColor::Reset, isatty));

        // print line marker
        fprintf(stderr, "%s%s%s%s\n", color(TermColor::Green, isatty), color(TermColor::Bold, isatty),
                markers[i].c_str(), color(TermColor::Reset, isatty));
    }

    fflush(stderr);
}

static void handleError(const Lexer &lexer, DSErrorKind type, const char *errorKind,
                        Token errorToken, const std::string &message, DSError *dsError) {
    unsigned int errorLineNum = lexer.getSourceInfoPtr()->getLineNum(errorToken.pos);
    const bool isatty = isSupportedTerminal(STDERR_FILENO);

    /**
     * show error message
     */
    fprintf(stderr, "%s:%d:%s%s ",
            lexer.getSourceInfoPtr()->getSourceName().c_str(), errorLineNum,
            color(TermColor::Magenta, isatty), color(TermColor::Bold, isatty));
    fprintf(stderr, "[%s error] %s%s\n",
            type == DS_ERROR_KIND_PARSE_ERROR ? "syntax" : "semantic",
            color(TermColor::Reset, isatty), message.c_str());
    formatErrorLine(isatty, lexer, errorToken);

    setErrorInfo(dsError, type, errorLineNum, errorKind);
}

static void handleParseError(const Lexer &lexer, const ParseError &e, DSError *dsError) {
    Token errorToken = lexer.shiftEOS(e.getErrorToken());
    handleError(lexer, DS_ERROR_KIND_PARSE_ERROR, e.getErrorKind(), errorToken, e.getMessage(), dsError);
}

static void handleTypeError(const Lexer &lexer, const TypeCheckError &e, DSError *dsError) {
    handleError(lexer, DS_ERROR_KIND_TYPE_ERROR, e.getKind(), e.getToken(), e.getMessage(), dsError);
}

/**
 * if called from child process, exit(1).
 * @param except
 */
static void handleUncaughtException(DSState *st, DSValue &&except) {
    fputs("[runtime error]\n", stderr);
    const bool bt = st->pool.getErrorType().isSameOrBaseTypeOf(*except->getType());
    auto *handle = except->getType()->lookupMethodHandle(st->pool, bt ? "backtrace" : OP_STR);

    DSValue ret = callMethod(*st, handle, std::move(except), std::vector<DSValue>());
    if(st->getThrownObject()) {
        fputs("cannot obtain string representation\n", stderr);
    } else if(!bt) {
        fprintf(stderr, "%s\n", typeAs<String_Object>(ret)->getValue());
    }
    fflush(stderr);

    if(!st->isRootShell()) {
        exit(1);    // in child process.
    }
}

static int evalCode(DSState *state, CompiledCode &code, DSError *dsError) {
    if(state->dumpTarget.fps[DS_DUMP_KIND_CODE] != nullptr) {
        auto *fp = state->dumpTarget.fps[DS_DUMP_KIND_CODE];
        fprintf(fp, "### dump compiled code ###\n");
        dumpCode(fp, *state, code);
    }

    if(state->execMode == DS_EXEC_MODE_COMPILE_ONLY) {
        return 0;
    }

    if(!vmEval(*state, code)) {
        unsigned int errorLineNum = 0;
        DSValue thrownObj = state->getThrownObject();
        if(dynamic_cast<Error_Object *>(thrownObj.get()) != nullptr) {
            auto *obj = typeAs<Error_Object>(thrownObj);
            errorLineNum = getOccurredLineNum(obj->getStackTrace());
        }

        state->loadThrownObject();
        handleUncaughtException(state, state->pop());
        state->recover(false);
        setErrorInfo(dsError, DS_ERROR_KIND_RUNTIME_ERROR, errorLineNum,
                     state->pool.getTypeName(*thrownObj->getType()).c_str());
        return 1;
    }
    return state->getExitStatus();
}

static int compileImpl(DSState *state, Lexer &&lexer, DSError *dsError, CompiledCode &code) {
    setErrorInfo(dsError, DS_ERROR_KIND_SUCCESS, 0, nullptr);
    lexer.setLineNum(state->lineNum);

    // parse
    Parser parser(lexer);
    auto rootNode = parser();
    state->lineNum = lexer.getLineNum();
    if(parser.hasError()) {
        handleParseError(lexer, parser.getError(), dsError);
        return 1;
    }

    if(state->dumpTarget.fps[DS_DUMP_KIND_UAST] != nullptr) {
        auto *fp = state->dumpTarget.fps[DS_DUMP_KIND_UAST];
        fputs("### dump untyped AST ###\n", fp);
        NodeDumper::dump(fp, state->pool, *rootNode);
    }

    if(state->execMode == DS_EXEC_MODE_PARSE_ONLY) {
        return 0;
    }

    // type check
    try {
        TypeChecker checker(state->pool, state->symbolTable, hasFlag(state->option, DS_OPTION_TOPLEVEL));
        checker.checkTypeRootNode(*rootNode);

        if(state->dumpTarget.fps[DS_DUMP_KIND_AST] != nullptr) {
            auto *fp = state->dumpTarget.fps[DS_DUMP_KIND_AST];
            fputs("### dump typed AST ###\n", fp);
            NodeDumper::dump(fp, state->pool, *rootNode);
        }
    } catch(const TypeCheckError &e) {
        handleTypeError(lexer, e, dsError);
        state->recover();
        return 1;
    }

    if(state->execMode == DS_EXEC_MODE_CHECK_ONLY) {
        return 0;
    }

    // code generation
    ByteCodeGenerator codegen(state->pool, hasFlag(state->option, DS_OPTION_ASSERT));
    code = codegen.generateToplevel(*rootNode);
    return 0;
}

static void bindVariable(DSState *state, const char *varName, DSValue &&value, FieldAttributes attribute) {
    auto handle = state->symbolTable.registerHandle(varName, *value->getType(), attribute);
    assert(handle.first != nullptr);
    state->setGlobal(handle.first->getFieldIndex(), std::move(value));
}

static void bindVariable(DSState *state, const char *varName, DSValue &&value) {
    bindVariable(state, varName, std::move(value), FieldAttribute::READ_ONLY);
}

static void bindVariable(DSState *state, const char *varName, const DSValue &value) {
    bindVariable(state, varName, DSValue(value));
}

static void initBuiltinVar(DSState *state) {
    // set builtin variables internally used

    /**
     * management object for D-Bus related function
     * must be DBus_Object
     */
    bindVariable(state, "DBus", newDBusObject(state->pool));

#define XSTR(V) #V
#define STR(V) XSTR(V)
    /**
     * for version detection
     * must be String_Object
     */
    bindVariable(state, "YDSH_VERSION", DSValue::create<String_Object>(
            state->pool.getStringType(),
            STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "." STR(X_INFO_PATCH_VERSION)));
#undef XSTR
#undef STR

    /**
     * default variable for read command.
     * must be String_Object
     */
    bindVariable(state, "REPLY", state->emptyStrObj);

    std::vector<DSType *> types = {&state->pool.getStringType(), &state->pool.getStringType()};

    /**
     * holding read variable.
     * must be Map_Object
     */
    bindVariable(state, "reply", DSValue::create<Map_Object>(
            state->pool.createReifiedType(state->pool.getMapTemplate(), std::move(types))));

    /**
     * process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PID", DSValue::create<Int_Object>(state->pool.getInt32Type(), getpid()));

    /**
     * parent process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PPID", DSValue::create<Int_Object>(state->pool.getInt32Type(), getppid()));

    /**
     * must be Long_Object.
     */
    bindVariable(state, "SECONDS", DSValue::create<Long_Object>(state->pool.getUint64Type(), 0), FieldAttribute::SECONDS);

    /**
     * for internal field splitting.
     * must be String_Object.
     */
    bindVariable(state, "IFS", DSValue::create<String_Object>(state->pool.getStringType(), " \t\n"), FieldAttributes());

    /**
     * for history api.
     * must be Int_Object.
     */
    bindVariable(state, "HISTCMD", DSValue::create<Int_Object>(state->pool.getUint32Type(), 1));

    /**
     * contains exit status of most recent executed process. ($?)
     * must be Int_Object
     */
    bindVariable(state, "?", DSValue::create<Int_Object>(state->pool.getInt32Type(), 0));

    /**
     * process id of root shell. ($$)
     * must be Int_Object
     */
    bindVariable(state, "$", DSValue::create<Int_Object>(state->pool.getInt32Type(), getpid()));

    /**
     * contains script argument(exclude script name). ($@)
     * must be Array_Object
     */
    bindVariable(state, "@", DSValue::create<Array_Object>(state->pool.getStringArrayType()));

    /**
     * contains size of argument. ($#)
     * must be Int_Object
     */
    bindVariable(state, "#", DSValue::create<Int_Object>(state->pool.getInt32Type(), 0));

    /**
     * represent shell or shell script name.
     * must be String_Object
     */
    bindVariable(state, "0", DSValue::create<String_Object>(state->pool.getStringType(), "ydsh"));

    /**
     * initialize positional parameter
     */
    for(unsigned int i = 0; i < 9; i++) {
        auto num = std::to_string(i + 1);
        bindVariable(state, num.c_str(), state->emptyStrObj);
    }


    // set builtin variables

    /**
     * uid of shell
     * must be Int_Object
     */
    bindVariable(state, "UID", DSValue::create<Int_Object>(state->pool.getUint32Type(), getuid()));

    /**
     * euid of shell
     * must be Int_Object
     */
    bindVariable(state, "EUID", DSValue::create<Int_Object>(state->pool.getUint32Type(), geteuid()));

    struct utsname name{};
    if(uname(&name) == -1) {
        perror("cannot get utsname");
        exit(1);
    }

    /**
     * must be String_Object
     */
    bindVariable(state, "OSTYPE", DSValue::create<String_Object>(state->pool.getStringType(), name.sysname));

    /**
     * must be String_Object
     */
    bindVariable(state, "MACHTYPE", DSValue::create<String_Object>(state->pool.getStringType(), name.machine));

    /**
     * dummy object for random number
     * must be Int_Object
     */
    bindVariable(state, "RANDOM", DSValue::create<Int_Object>(state->pool.getUint32Type(), 0),
                 FieldAttribute::READ_ONLY | FieldAttribute ::RANDOM);
    srand(static_cast<unsigned int>(time(nullptr)));    // init rand for $RANDOM

    /**
     * dummy object for signal handler setting
     * must be DSObject
     */
    bindVariable(state, "SIG", DSValue::create<DSObject>(state->pool.getSignalsType()));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDIN, DSValue::create<UnixFD_Object>(state->pool, STDIN_FILENO));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDOUT, DSValue::create<UnixFD_Object>(state->pool, STDOUT_FILENO));

    /**
     * must be UnixFD_Object
     */
    bindVariable(state, VAR_STDERR, DSValue::create<UnixFD_Object>(state->pool, STDERR_FILENO));
}

static void loadEmbeddedScript(DSState *state) {
    int ret = DSState_eval(state, "(embed)", embed_script, strlen(embed_script), nullptr);
    if(ret != 0) {
        fatal("broken embedded script\n");
    }
    state->pool.commit();

    // rest some state
    state->lineNum = 1;
    state->updateExitStatus(0);
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
        perror("getpwuid failed\n");
        exit(1);
    }
    setenv(ENV_HOME, pw->pw_dir, 0);

    // set LOGNAME
    setenv(ENV_LOGNAME, pw->pw_name, 0);
}

// ###################################
// ##     public api of DSState     ##
// ###################################

DSState *DSState_createWithMode(DSExecMode mode) {
    initEnv();

    auto *ctx = new DSState();

    /**
     * set execution mode before embedded script loading.
     * due to suppress fuzzer error
     */
#ifdef FUZZING_BUILD_MODE
    ctx->execMode = mode;
#endif

    initBuiltinVar(ctx);
    loadEmbeddedScript(ctx);

    ctx->execMode = mode;
    return ctx;
}

void DSState_delete(DSState **st) {
    if(st != nullptr) {
        delete (*st);
        *st = nullptr;
    }
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
        st->setGlobal(index, DSValue::create<String_Object>(st->pool.getStringType(), std::string(shellName)));
    }
}

// set positional parameters
static void finalizeScriptArg(DSState *st) {
    unsigned int index = toIndex(BuiltinVarOffset::ARGS);
    auto *array = typeAs<Array_Object>(st->getGlobal(index));

    // update argument size
    const unsigned int size = array->getValues().size();
    index = toIndex(BuiltinVarOffset::ARGS_SIZE);
    st->setGlobal(index, DSValue::create<Int_Object>(st->pool.getInt32Type(), size));

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
    unsigned int index = toIndex(BuiltinVarOffset::ARGS);
    typeAs<Array_Object>(st->getGlobal(index))->refValues().clear();

    for(unsigned int i = 0; args[i] != nullptr; i++) {
        auto *array = typeAs<Array_Object>(st->getGlobal(toIndex(BuiltinVarOffset::ARGS)));
        array->append(DSValue::create<String_Object>(st->pool.getStringType(), std::string(args[i])));
    }
    finalizeScriptArg(st);
}

int DSState_setScriptDir(DSState *st, const char *scriptPath) {
    char *real = realpath(scriptPath, nullptr);
    if(real == nullptr) {
        return -1;
    }

    unsigned int index = st->symbolTable.lookupHandle(VAR_SCRIPT_DIR)->getFieldIndex();
    const char *ptr = strrchr(real, '/');

    std::string str(real, real == ptr ? 1 : ptr - real);
    free(real);
    st->setGlobal(index, DSValue::create<String_Object>(st->pool.getStringType(), std::move(str)));
    return 0;
}

void DSState_setDumpTarget(DSState *st, DSDumpKind kind, FILE *fp) {
    assert(fp != nullptr);

    if(st->dumpTarget.fps[kind] != nullptr) {
        fclose(st->dumpTarget.fps[kind]);
    }
    st->dumpTarget.fps[kind] = fp;
}

unsigned short DSState_option(const DSState *st) {
    return st->option;
}

void DSState_setOption(DSState *st, unsigned short optionSet) {
    setFlag(st->option, optionSet);

    if(hasFlag(optionSet, DS_OPTION_INTERACTIVE)) {
        auto ign = getGlobal(*st, VAR_SIG_IGN);
        installSignalHandler(*st, SIGINT, ign);
        installSignalHandler(*st, SIGQUIT, ign);
        installSignalHandler(*st, SIGTSTP, ign);
        installSignalHandler(*st, SIGTTIN, ign);
        installSignalHandler(*st, SIGTTOU, ign);
    }
}

void DSState_unsetOption(DSState *st, unsigned short optionSet) {
    unsetFlag(st->option, optionSet);

    if(hasFlag(optionSet, DS_OPTION_INTERACTIVE)) {
        auto dfl = getGlobal(*st, VAR_SIG_DFL);
        installSignalHandler(*st, SIGINT, dfl);
        installSignalHandler(*st, SIGQUIT, dfl);
        installSignalHandler(*st, SIGTSTP, dfl);
        installSignalHandler(*st, SIGTTIN, dfl);
        installSignalHandler(*st, SIGTTOU, dfl);
    }
}

int DSState_eval(DSState *st, const char *sourceName, const char *data, unsigned int size, DSError *e) {
    CompiledCode code;
    int ret = compileImpl(st, Lexer(sourceName == nullptr ? "(stdin)" : sourceName, data, size), e, code);
    if(!code) {
        return ret;
    }
    return evalCode(st, code, e);
}

int DSState_loadAndEval(DSState *st, const char *sourceName, FILE *fp, DSError *e) {
    CompiledCode code;
    int ret = compileImpl(st, Lexer(sourceName == nullptr ? "(stdin)" : sourceName, fp), e, code);
    if(!code) {
        return ret;
    }
    return evalCode(st, code, e);
}

int DSState_exec(DSState *st, char *const *argv) {
    int status = execBuiltinCommand(*st, argv);
    if(st->getThrownObject()) {
        auto &obj = typeAs<Error_Object>(st->getThrownObject())->getMessage();
        const char *str = typeAs<String_Object>(obj)->getValue();
        fprintf(stderr, "ydsh: %s\n", str + strlen(EXEC_ERROR));
    }
    return status;
}

const char *DSState_prompt(DSState *st, unsigned int n) {
    const char *psName = nullptr;
    switch(n) {
    case 1:
        psName = VAR_PS1;
        break;
    case 2:
        psName = VAR_PS2;
        break;
    default:
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
    return "Copyright (C) 2015-2017 Nagisa Sekiguchi";
}

unsigned int DSState_featureBit() {
    unsigned int featureBit = 0;

#ifdef USE_LOGGING
    setFlag(featureBit, DS_FEATURE_LOGGING);
#endif

#ifdef USE_DBUS
    setFlag(featureBit, DS_FEATURE_DBUS);
#endif

#ifdef USE_SAFE_CAST
    setFlag(featureBit, DS_FEATURE_SAFE_CAST);
#endif

#ifdef USE_FIXED_TIME
    setFlag(featureBit, DS_FEATURE_FIXED_TIME);
#endif
    return featureBit;
}

void DSState_addTerminationHook(DSState *st, TerminationHook hook) {
    st->terminationHook = hook;
}

void DSState_complete(const DSState *st, const char *buf, size_t cursor, DSCandidates *c) {
    if(c == nullptr) {
        return;
    }

    // init candidates
    c->size = 0;
    c->values = nullptr;

    if(st == nullptr || buf == nullptr || cursor == 0) {
        return;
    }

    std::string line(buf, cursor);
    LOG(DUMP_CONSOLE, "line: " << line << ", cursor: " << cursor);

    line += '\n';
    CStrBuffer sbuf = completeLine(*st, line);

    // write to DSCandidates
    c->size = sbuf.size();
    c->values = extract(std::move(sbuf));
}

void DSCandidates_release(DSCandidates *c) {
    if(c != nullptr) {
        for(unsigned int i = 0; i < c->size; i++) {
            free(c->values[i]);
        }
        c->size = 0;
        if(c->values != nullptr) {
            free(c->values);
            c->values = nullptr;
        }
    }
}

const DSHistory *DSState_history(const DSState *st) {
    return &st->history;
}

static void resizeHistory(DSHistory &history, unsigned int cap) {
    if(cap == history.capacity) {
        return;
    }

    if(cap < history.size) {
        // if cap < history.size, free remain entry
        for(unsigned int i = cap; i < history.size; i++) {
            free(history.data[i]);
        }
        history.size = cap;
    }

    void *ret = realloc(history.data, sizeof(char *) * cap);
    if(cap == 0 || ret != nullptr) {
        history.capacity = cap;
        history.data = reinterpret_cast<char **>(ret);
    }
}

void DSState_syncHistorySize(DSState *st) {
    if(hasFlag(st->option, DS_OPTION_HISTORY)) {
        unsigned int cap = typeAs<Int_Object>(getGlobal(*st, VAR_HISTSIZE))->getValue();
        if(cap > DS_HISTSIZE_LIMIT) {
            cap = DS_HISTSIZE_LIMIT;
        }
        resizeHistory(st->history, cap);
    }
}

void DSState_setHistoryAt(DSState *st, unsigned int index, const char *str) {
    if(index < st->history.size) {
        free(st->history.data[index]);
        st->history.data[index] = strdup(str);
    }
}

static void updateHistCmd(DSState *st, unsigned int offset, bool inc) {
    const unsigned int index = toIndex(BuiltinVarOffset::HIST_CMD);
    unsigned int value = typeAs<Int_Object>(st->getGlobal(index))->getValue();
    if(inc) {
        value += offset;
    } else {
        value -= offset;
    }
    st->setGlobal(index, DSValue::create<Int_Object>(st->pool.getUint32Type(), value));
}

static void unsafeDeleteHistory(DSHistory &history, unsigned int index) {
    free(history.data[index]);
    memmove(history.data + index, history.data + index + 1,
            sizeof(char *) * (history.size - index - 1));
    history.size--;
}

void DSState_addHistory(DSState *st, const char *str) {
    if(st->history.capacity > 0) {
        if(st->history.size > 0 && strcmp(str, st->history.data[st->history.size - 1]) == 0) {
            return; // skip duplicated line
        }

        if(st->history.size == st->history.capacity) {
            unsafeDeleteHistory(st->history, 0);
        }
        st->history.data[st->history.size++] = strdup(str);
        updateHistCmd(st, 1, true);
    }
}

void DSState_deleteHistoryAt(DSState *st, unsigned int index) {
    if(index < st->history.size) {
        unsafeDeleteHistory(st->history, index);
        updateHistCmd(st, 1, false);
    }
}

void DSState_clearHistory(DSState *st) {
    updateHistCmd(st, st->history.size, false);
    while(st->history.size > 0) {
        unsafeDeleteHistory(st->history, st->history.size - 1);
    }
}

static std::string histFile(const DSState *st) {
    std::string path = typeAs<String_Object>(getGlobal(*st, VAR_HISTFILE))->getValue();
    expandTilde(path);
    return path;
}

void DSState_loadHistory(DSState *st, const char *fileName) {
    DSState_syncHistorySize(st);
    if(st->history.capacity > 0) {
        std::ifstream input(fileName != nullptr ? fileName : histFile(st).c_str());
        if(input) {
            unsigned int count = 0;
            for(std::string line; st->history.size < st->history.capacity && std::getline(input, line);) {
                st->history.data[st->history.size++] = strdup(line.c_str());
                count++;
            }
            updateHistCmd(st, count, true);
        }
    }
}

void DSState_saveHistory(const DSState *st, const char *fileName) {
    unsigned int histFileSize = typeAs<Int_Object>(getGlobal(*st, VAR_HISTFILESIZE))->getValue();
    if(histFileSize > DS_HISTFILESIZE_LIMIT) {
        histFileSize = DS_HISTFILESIZE_LIMIT;
    }

    if(histFileSize > 0 && st->history.size > 0) {
        FILE *fp = fopen(fileName != nullptr ? fileName : histFile(st).c_str(), "w");
        if(fp != nullptr) {
            for(unsigned int i = 0; i < histFileSize && i < st->history.size; i++) {
                fprintf(fp, "%s\n", st->history.data[i]);
            }
            fclose(fp);
        }
    }
}