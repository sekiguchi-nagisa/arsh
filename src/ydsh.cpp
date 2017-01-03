/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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
#include <iostream>
#include <csignal>
#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <unistd.h>
#include <sys/utsname.h>

#include <ydsh/ydsh.h>
#include <config.h>
#include <embed.h>

#include "node_dumper.h"
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


static void setErrorInfo(DSError *error, unsigned int type, unsigned int lineNum, const char *errorName) {
    if(error != nullptr) {
        error->kind = type;
        error->lineNum = lineNum;
        error->name = errorName != nullptr ? strdup(errorName) : nullptr;
    }
}

/**
 * not allow dumb terminal
 */
static bool isSupportedTerminal(int fd) {
    const char *term = getenv(ENV_TERM);
    return term != nullptr && strcasecmp(term, "dumb") != 0 && isatty(fd) != 0;
}

enum class TermColor : int {   // ansi color code
    NOP     = -1,
    Reset   = 0,
    Bold    = 1,
    // actual term color
    Black   = 30,
    Red     = 31,
    Green   = 32,
    Yellow  = 33,
    Blue    = 34,
    Magenta = 35,
    Cyan    = 36,
    White   = 37,
};

static TermColor color(TermColor color, bool isatty) {
    return isatty ? color : TermColor::NOP;
}

static std::ostream &operator<<(std::ostream &stream, TermColor color) {
    if(color != TermColor::NOP) {
        stream << "\033[" << static_cast<unsigned int>(color) << "m";
    }
    return stream;
}

static void formatErrorLine(bool isatty, const Lexer &lexer, Token errorToken) {
    errorToken = lexer.shiftEOS(errorToken);
    Token lineToken = lexer.getLineToken(errorToken);

    // print error line
    std::cerr << color(TermColor::Cyan, isatty) << lexer.toTokenText(lineToken)
    << color(TermColor::Reset, isatty) << std::endl;

    // print line marker
    std::cerr << color(TermColor::Green, isatty) << color(TermColor::Bold, isatty)
              << lexer.formatLineMarker(lineToken, errorToken)
              << color(TermColor::Reset, isatty) << std::endl;
}

static void handleParseError(const Lexer &lexer, const ParseError &e, DSError *dsError) {
    Token errorToken = lexer.shiftEOS(e.getErrorToken());

    /**
     * show parse error message
     */
    unsigned int errorLineNum = lexer.getSourceInfoPtr()->getLineNum(errorToken.pos);

    const bool isatty = isSupportedTerminal(STDERR_FILENO);

    std::cerr << lexer.getSourceInfoPtr()->getSourceName() << ":" << errorLineNum << ":"
              << color(TermColor::Magenta, isatty) << color(TermColor::Bold, isatty)
              << " [syntax error] " << color(TermColor::Reset, isatty)
              << e.getMessage() << std::endl;
    formatErrorLine(isatty, lexer, errorToken);

    setErrorInfo(dsError, DS_ERROR_KIND_PARSE_ERROR, errorLineNum, e.getErrorKind());
}

static void handleTypeError(const Lexer &lexer, const TypeCheckError &e, DSError *dsError) {
    unsigned int errorLineNum = lexer.getSourceInfoPtr()->getLineNum(e.getStartPos());

    const bool isatty = isSupportedTerminal(STDERR_FILENO);

    /**
     * show type error message
     */
    std::cerr << lexer.getSourceInfoPtr()->getSourceName() << ":" << errorLineNum << ":"
              << color(TermColor::Magenta, isatty) << color(TermColor::Bold, isatty)
              << " [semantic error] " << color(TermColor::Reset, isatty)
              << e.getMessage() << std::endl;
    formatErrorLine(isatty, lexer, e.getToken());

    setErrorInfo(dsError, DS_ERROR_KIND_TYPE_ERROR,
                 lexer.getSourceInfoPtr()->getLineNum(e.getStartPos()), e.getKind());
}

/**
 * if called from child process, exit(1).
 * @param except
 */
static void handleUncaughtException(DSState *st, DSValue &&except) {
    std::cerr << "[runtime error]" << std::endl;
    const bool bt = st->pool.getErrorType().isSameOrBaseTypeOf(*except->getType());
    auto *handle = except->getType()->lookupMethodHandle(st->pool, bt ? "backtrace" : OP_STR);

    try {
        DSValue ret = ::callMethod(*st, handle, std::move(except), std::vector<DSValue>());
        if(!bt) {
            std::cerr << typeAs<String_Object>(ret)->getValue() << std::endl;
        }
    } catch(const DSException &) {
        std::cerr << "cannot obtain string representation" << std::endl;
    }

    if(typeAs<Int_Object>(st->getGlobal(toIndex(BuiltinVarOffset::SHELL_PID)))->getValue() !=
       typeAs<Int_Object>(st->getGlobal(toIndex(BuiltinVarOffset::PID)))->getValue()) {
        exit(1);    // in child process.
    }
}

static int evalCode(DSState *state, CompiledCode &code, DSError *dsError) {
    if(hasFlag(state->option, DS_OPTION_DUMP_CODE)) {
        std::cout << "### dump compiled code ###" << std::endl;
        dumpCode(std::cout, *state, code);
    }

    if(!vmEval(*state, code)) {
        unsigned int errorLineNum = 0;
        DSValue thrownObj = state->getThrownObject();
        if(dynamic_cast<Error_Object *>(thrownObj.get()) != nullptr) {
            Error_Object *obj = typeAs<Error_Object>(thrownObj);
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

static int compileImpl(DSState *state, Lexer &lexer, DSError *dsError, CompiledCode &code) {
    setErrorInfo(dsError, DS_ERROR_KIND_SUCCESS, 0, nullptr);
    lexer.setLineNum(state->lineNum);
    RootNode rootNode;

    // parse
    try {
        Parser().parse(lexer, rootNode);
        state->lineNum = lexer.getLineNum();

        if(hasFlag(state->option, DS_OPTION_DUMP_UAST)) {
            std::cout << "### dump untyped AST ###" << std::endl;
            NodeDumper::dump(std::cout, state->pool, rootNode);
            std::cout << std::endl;
        }
    } catch(const ParseError &e) {
        handleParseError(lexer, e, dsError);
        state->lineNum = lexer.getLineNum();
        return 1;
    }

    // type check
    try {
        TypeChecker checker(state->pool, state->symbolTable, hasFlag(state->option, DS_OPTION_TOPLEVEL));
        checker.checkTypeRootNode(rootNode);

        if(hasFlag(state->option, DS_OPTION_DUMP_AST)) {
            std::cout << "### dump typed AST ###" << std::endl;
            NodeDumper::dump(std::cout, state->pool, rootNode);
            std::cout << std::endl;
        }
    } catch(const TypeCheckError &e) {
        handleTypeError(lexer, e, dsError);
        state->recover();
        return 1;
    }

    if(hasFlag(state->option, DS_OPTION_PARSE_ONLY)) {
        return 0;
    }

    // code generation
    ByteCodeGenerator codegen(state->pool, hasFlag(state->option, DS_OPTION_ASSERT));
    code = codegen.generateToplevel(rootNode);
    return 0;
}

static int compile(DSState *state, const char *sourceName, FILE *fp, DSError *dsError, CompiledCode &code) {
    Lexer lexer(sourceName, fp);
    return compileImpl(state, lexer, dsError, code);
}

static int compile(DSState *state, const char *sourceName, const char *source, DSError *dsError, CompiledCode &code) {
    Lexer lexer(sourceName, source);
    return compileImpl(state, lexer, dsError, code);
}

static void bindVariable(DSState *state, const char *varName, DSValue &&value, FieldAttributes attribute) {
    auto handle = state->symbolTable.registerHandle(varName, *value.get()->getType(), attribute);
    assert(handle != nullptr);
    state->setGlobal(handle->getFieldIndex(), std::move(value));
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

    std::vector<DSType *> types(2);
    types[0] = &state->pool.getStringType();
    types[1] = types[0];

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
    bindVariable(state, "PID", DSValue::create<Int_Object>(state->pool.getUint32Type(), getpid()));

    /**
     * parent process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PPID", DSValue::create<Int_Object>(state->pool.getUint32Type(), getppid()));

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
     * contains exit status of most recent executed process. ($?)
     * must be Int_Object
     */
    bindVariable(state, "?", DSValue::create<Int_Object>(state->pool.getInt32Type(), 0));

    /**
     * process id of root shell. ($$)
     * must be Int_Object
     */
    bindVariable(state, "$", DSValue::create<Int_Object>(state->pool.getUint32Type(), getpid()));

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
        bindVariable(state, std::to_string(i + 1).c_str(), state->emptyStrObj);
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

    struct utsname name;
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
}

static void loadEmbeddedScript(DSState *state) {
    int ret = DSState_eval(state, "(embed)", embed_script, nullptr);
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

DSState *DSState_create() {
    initEnv();

    DSState *ctx = new DSState();

    initBuiltinVar(ctx);
    loadEmbeddedScript(ctx);
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

unsigned int DSState_option(const DSState *st) {
    return st->option;
}

void DSState_setOption(DSState *st, unsigned int optionSet) {
    setFlag(st->option, optionSet);
}

void DSState_unsetOption(DSState *st, unsigned int optionSet) {
    unsetFlag(st->option, optionSet);
}

void DSError_release(DSError *e) {
    if(e != nullptr) {
        e->kind = 0;
        e->lineNum = 0;
        free(e->name);
        e->name = nullptr;
    }
}

int DSState_eval(DSState *st, const char *sourceName, const char *source, DSError *e) {
    CompiledCode code;
    int ret = compile(st, sourceName == nullptr ? "(stdin)" : sourceName, source, e, code);
    if(!code) {
        return ret;
    }
    return evalCode(st, code, e);
}

int DSState_loadAndEval(DSState *st, const char *sourceName, FILE *fp, DSError *e) {
    CompiledCode code;
    int ret = compile(st, sourceName == nullptr ? "(stdin)" : sourceName, fp, e, code);
    if(!code) {
        return ret;
    }
    return evalCode(st, code, e);
}

int DSState_exec(DSState *st, char *const *argv) {
    return execBuiltinCommand(*st, argv);
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

    unsigned int index = st->symbolTable.lookupHandle(psName)->getFieldIndex();
    const DSValue &obj = st->getGlobal(index);

    interpretPromptString(*st, typeAs<String_Object>(obj)->getValue(), st->prompt);
    return st->prompt.c_str();
}

unsigned int DSState_majorVersion() {
    return X_INFO_MAJOR_VERSION;
}

unsigned int DSState_minorVersion() {
    return X_INFO_MINOR_VERSION;
}

unsigned int DSState_patchVersion() {
    return X_INFO_PATCH_VERSION;
}

const char *DSState_version() {
    return "ydsh, version " X_INFO_VERSION
            " (" X_INFO_SYSTEM "), build by " X_INFO_CPP " " X_INFO_CPP_V;
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
    unsigned int index = st->symbolTable.lookupHandle(VAR_HISTSIZE)->getFieldIndex();
    unsigned int cap = typeAs<Int_Object>(st->getGlobal(index))->getValue();
    if(cap > DS_HISTSIZE_LIMIT) {
        cap = DS_HISTSIZE_LIMIT;
    }
    resizeHistory(st->history, cap);
}

void DSState_setHistoryAt(DSState *st, unsigned int index, const char *str) {
    if(index < st->history.size) {
        free(st->history.data[index]);
        st->history.data[index] = strdup(str);
    }
}

static void updateHistCmd(DSState *st, unsigned int offset, bool inc) {
    const unsigned int index = st->symbolTable.lookupHandle(VAR_HISTCMD)->getFieldIndex();
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

static const std::string histFile(const DSState *st) {
    unsigned int index = st->symbolTable.lookupHandle(VAR_HISTFILE)->getFieldIndex();
    std::string path = typeAs<String_Object>(st->getGlobal(index))->getValue();
    expandTilde(path);
    return path;
}

void DSState_loadHistory(DSState *st) {
    DSState_syncHistorySize(st);
    if(st->history.capacity > 0) {
        std::ifstream input(histFile(st));
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

static void appendWithFixedSize(std::vector<std::string> &buf, const unsigned int limit, std::string &&line) {
    if(buf.size() >= limit) {
        buf.erase(buf.begin());
    }
    buf.push_back(std::move(line));
}

void DSState_saveHistory(const DSState *st) {
    auto handle = st->symbolTable.lookupHandle(VAR_HISTFILESIZE);
    unsigned int histFileSize = typeAs<Int_Object>(st->getGlobal(handle->getFieldIndex()))->getValue();
    if(histFileSize > DS_HISTFILESIZE_LIMIT) {
        histFileSize = DS_HISTFILESIZE_LIMIT;
    }
    auto path = histFile(st);

    std::vector<std::string> buf;
    buf.reserve(histFileSize);

    // read previous history file
    if(histFileSize > st->history.size) {
        std::ifstream input(path);
        if(input) {
            for(std::string line; std::getline(input, line);) {
                appendWithFixedSize(buf, histFileSize, std::move(line));
            }
        }
    }

    for(unsigned int i = 0; i < st->history.size; i++) {
        appendWithFixedSize(buf, histFileSize, std::string(st->history.data[i]));
    }

    // update history file
    FILE *fp = fopen(path.c_str(), "w");
    if(fp != nullptr) {
        for(auto &e : buf) {
            fprintf(fp, "%s\n", e.c_str());
        }
        fclose(fp);
    }
}