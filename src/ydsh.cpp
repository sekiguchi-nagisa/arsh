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

#include <unistd.h>
#include <sys/utsname.h>

#include <ydsh/ydsh.h>
#include <config.h>
#include <embed.h>

#include "node_dumper.h"
#include "lexer.h"
#include "parser.h"
#include "type_checker.h"
#include "context.h"
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

static const unsigned int originalShellLevel = getShellLevel();


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

static void formatErrorLine(bool isatty, const Lexer &lexer, const Token &errorToken) {
    Token lineToken = lexer.getLineToken(errorToken, true);

    // print error line
    std::cerr << color(TermColor::Cyan, isatty) << lexer.toTokenText(lineToken)
    << color(TermColor::Reset, isatty) << std::endl;

    // print line marker
    std::cerr << color(TermColor::Green, isatty) << lexer.formatLineMarker(lineToken, errorToken)
    << color(TermColor::Reset, isatty) << std::endl;
}

static void handleParseError(const Lexer &lexer, const ParseError &e, DSError *dsError) {
    /**
     * show parse error message
     */
    unsigned int errorLineNum = lexer.getSourceInfoPtr()->getLineNum(e.getErrorToken().pos);
    if(e.getTokenKind() == EOS) {
        errorLineNum--;
    }

    const bool isatty = isSupportedTerminal(STDERR_FILENO);

    std::cerr << lexer.getSourceInfoPtr()->getSourceName() << ":" << errorLineNum << ":"
    << color(TermColor::Magenta, isatty) << " [syntax error] " << color(TermColor::Reset, isatty)
    << e.getMessage() << std::endl;
    formatErrorLine(isatty, lexer, e.getErrorToken());

    setErrorInfo(dsError, DS_ERROR_KIND_PARSE_ERROR, errorLineNum, e.getErrorKind());
}

static void handleTypeError(const Lexer &lexer, const TypeCheckError &e, DSError *dsError) {
    unsigned int errorLineNum = lexer.getSourceInfoPtr()->getLineNum(e.getStartPos());

    const bool isatty = isSupportedTerminal(STDERR_FILENO);

    /**
     * show type error message
     */
    std::cerr << lexer.getSourceInfoPtr()->getSourceName() << ":" << errorLineNum << ":"
    << color(TermColor::Magenta, isatty) << " [semantic error] " << color(TermColor::Reset, isatty)
    << e.getMessage() << std::endl;
    formatErrorLine(isatty, lexer, e.getToken());

    setErrorInfo(dsError, DS_ERROR_KIND_TYPE_ERROR,
                 lexer.getSourceInfoPtr()->getLineNum(e.getStartPos()), e.getKind());
}

static int eval(DSState *state, RootNode &rootNode, DSError *dsError) {
    ByteCodeGenerator codegen(state->getPool(), hasFlag(state->getOption(), DS_OPTION_ASSERT));
    CompiledCode c = codegen.generateToplevel(rootNode);

    if(hasFlag(state->getOption(), DS_OPTION_DUMP_CODE)) {
        std::cout << "### dump compiled code ###" << std::endl;
        dumpCode(std::cout, *state, c);
    }

    if(!vmEval(*state, c)) {
        unsigned int errorLineNum = 0;
        DSValue thrownObj = state->getThrownObject();
        if(dynamic_cast<Error_Object *>(thrownObj.get()) != nullptr) {
            Error_Object *obj = typeAs<Error_Object>(thrownObj);
            errorLineNum = getOccuredLineNum(obj->getStackTrace());
        }

        state->loadThrownObject();
        state->handleUncaughtException(state->pop());
        state->recover(false);
        setErrorInfo(dsError, DS_ERROR_KIND_RUNTIME_ERROR, errorLineNum,
                     state->getPool().getTypeName(*thrownObj->getType()).c_str());
        return 1;
    }
    return state->getExitStatus();
}

static int eval(DSState *state, Lexer &lexer, DSError *dsError) {
    setErrorInfo(dsError, DS_ERROR_KIND_SUCCESS, 0, nullptr);
    lexer.setLineNum(state->getLineNum());
    RootNode rootNode;

    // parse
    try {
        Parser().parse(lexer, rootNode);
        state->setLineNum(lexer.getLineNum());

        if(hasFlag(state->getOption(), DS_OPTION_DUMP_UAST)) {
            std::cout << "### dump untyped AST ###" << std::endl;
            NodeDumper::dump(std::cout, state->getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const ParseError &e) {
        handleParseError(lexer, e, dsError);
        state->setLineNum(lexer.getLineNum());
        return 1;
    }

    // type check
    try {
        TypeChecker checker(state->getPool(), state->getSymbolTable(),
                            hasFlag(state->getOption(), DS_OPTION_TOPLEVEL));
        checker.checkTypeRootNode(rootNode);

        if(hasFlag(state->getOption(), DS_OPTION_DUMP_AST)) {
            std::cout << "### dump typed AST ###" << std::endl;
            NodeDumper::dump(std::cout, state->getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const TypeCheckError &e) {
        handleTypeError(lexer, e, dsError);
        state->recover();
        return 1;
    }

    if(hasFlag(state->getOption(), DS_OPTION_PARSE_ONLY)) {
        return 0;
    }

    // eval
    return eval(state, rootNode, dsError);
}

static void bindVariable(DSState *state, const char *varName, DSValue &&value) {
    auto handle = state->getSymbolTable().registerHandle(varName, *value.get()->getType(), true);
    assert(handle != nullptr);
    state->setGlobal(handle->getFieldIndex(), std::move(value));
}

static void bindVariable(DSState *state, const char *varName, const DSValue &value) {
    auto handle = state->getSymbolTable().registerHandle(varName, *value.get()->getType(), true);
    assert(handle != nullptr);
    state->setGlobal(handle->getFieldIndex(), value);
}

static void initBuiltinVar(DSState *state) {
    /**
     * management object for D-Bus related function
     * must be DBus_Object
     */
    bindVariable(state, "DBus", newDBusObject(state->getPool()));

    struct utsname name;
    if(uname(&name) == -1) {
        perror("cannot get utsname");
        exit(1);
    }

    /**
     * for os type detection.
     * must be String_Object
     */
    bindVariable(state, "OSTYPE", DSValue::create<String_Object>(
            state->getPool().getStringType(), name.sysname));

#define XSTR(V) #V
#define STR(V) XSTR(V)
    /**
     * for version detection
     * must be String_Object
     */
    bindVariable(state, "YDSH_VERSION", DSValue::create<String_Object>(
            state->getPool().getStringType(),
            STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "." STR(X_INFO_PATCH_VERSION)));
#undef XSTR
#undef STR

    /**
     * default variable for read command.
     * must be String_Object
     */
    bindVariable(state, "REPLY", state->getEmptyStrObj());

    std::vector<DSType *> types(2);
    types[0] = &state->getPool().getStringType();
    types[1] = types[0];

    /**
     * holding read variable.
     * must be Map_Object
     */
    bindVariable(state, "reply", DSValue::create<Map_Object>(
            state->getPool().createReifiedType(state->getPool().getMapTemplate(), std::move(types))));

    /**
     * process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PID", DSValue::create<Int_Object>(state->getPool().getUint32Type(), getpid()));

    /**
     * parent process id of current process.
     * must be Int_Object
     */
    bindVariable(state, "PPID", DSValue::create<Int_Object>(state->getPool().getUint32Type(), getppid()));

    /**
     * contains exit status of most recent executed process. ($?)
     * must be Int_Object
     */
    bindVariable(state, "?", DSValue::create<Int_Object>(state->getPool().getInt32Type(), 0));

    /**
     * process id of root shell. ($$)
     * must be Int_Object
     */
    bindVariable(state, "$", DSValue::create<Int_Object>(state->getPool().getUint32Type(), getpid()));

    /**
     * contains script argument(exclude script name). ($@)
     * must be Array_Object
     */
    bindVariable(state, "@", DSValue::create<Array_Object>(state->getPool().getStringArrayType()));

    /**
     * contains size of argument. ($#)
     * must be Int_Object
     */
    bindVariable(state, "#", DSValue::create<Int_Object>(state->getPool().getInt32Type(), 0));

    /**
     * represent shell or shell script name.
     * must be String_Object
     */
    bindVariable(state, "0", DSValue::create<String_Object>(state->getPool().getStringType(), "ydsh"));

    /**
     * initialize positional parameter
     */
    for(unsigned int i = 0; i < 9; i++) {
        bindVariable(state, std::to_string(i + 1).c_str(), state->getEmptyStrObj());
    }
}

static void loadEmbeddedScript(DSState *state) {
    Lexer lexer("(embed)", embed_script);
    int ret = eval(state, lexer, nullptr);
    if(ret != 0) {
        fatal("broken embedded script\n");
    }
    state->getPool().commit();

    // rest some state
    state->setLineNum(1);
    state->updateExitStatus(0);
}

// ###################################
// ##     public api of DSState     ##
// ###################################

DSState *DSState_create() {
    DSState *ctx = new DSState();

    // set locale
    setlocale(LC_ALL, "");
    setlocale(LC_MESSAGES, "C");

    // update shell level
    setenv(ENV_SHLVL, std::to_string(originalShellLevel + 1).c_str(), 1);

    // set some env
    if(getenv(ENV_HOME) == nullptr) {
        struct passwd *pw = getpwuid(getuid());
        if(pw == nullptr) {
            perror("getpwuid failed\n");
            exit(1);
        }
        setenv(ENV_HOME, pw->pw_dir, 1);
    }

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
    st->setLineNum(lineNum);
}

unsigned int DSState_lineNum(DSState *st) {
    return st->getLineNum();
}

void DSState_setShellName(DSState *st, const char *shellName) {
    if(shellName != nullptr) {
        st->updateScriptName(shellName);
    }
}

void DSState_setArguments(DSState *st, char *const *args) {
    if(args == nullptr) {
        return;
    }

    st->initScriptArg();
    for(unsigned int i = 0; args[i] != nullptr; i++) {
        st->addScriptArg(args[i]);
    }
    st->finalizeScriptArg();
}

void DSState_setOption(DSState *st, unsigned int optionSet) {
    flag32_set_t origOption = st->getOption();
    setFlag(origOption, optionSet);
    st->setOption(origOption);
}

void DSState_unsetOption(DSState *st, unsigned int optionSet) {
    flag32_set_t origOption = st->getOption();
    unsetFlag(origOption, optionSet);
    st->setOption(origOption);
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
    Lexer lexer(sourceName == nullptr ? "(stdin)" : sourceName, source);
    return eval(st, lexer, e);
}

int DSState_loadAndEval(DSState *st, const char *sourceName, FILE *fp, DSError *e) {
    Lexer lexer(sourceName == nullptr ? "(stdin)" : sourceName, fp);
    return eval(st, lexer, e);
}

int DSState_exec(DSState *st, char *const *argv) {
    st->execBuiltinCommand(argv);
    return st->getExitStatus();
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

    unsigned int index = st->getSymbolTable().lookupHandle(psName)->getFieldIndex();
    const DSValue &obj = st->getGlobal(index);

    st->interpretPromptString(typeAs<String_Object>(obj)->getValue(), st->refPrompt());
    return st->refPrompt().c_str();
}

int DSState_supportDBus() {
    return hasFlag(DSState_featureBit(), DS_FEATURE_DBUS) ? 1 : 0;
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
    return "Copyright (C) 2015-2016 Nagisa Sekiguchi";
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
    st->setTerminationHook(hook);
}

void DSState_complete(DSState *st, const char *buf, size_t cursor, DSCandidates *c) {
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
    CStrBuffer sbuf = st->completeLine(line);

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