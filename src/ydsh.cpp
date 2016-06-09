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

namespace ydsh {

/**
 * entry point
 */
bool vmEval(RuntimeContext &ctx, Callable &callable);

} // namespace ydsh

using namespace ydsh;

struct DSContext {
    RuntimeContext ctx;
    Parser parser;
    TypeChecker checker;
    unsigned int lineNum;

    // option
    flag32_set_t option;

    // previously computed prompt
    std::string prompt;

    struct {
        /**
         * kind of execution status.
         */
        unsigned int type;

        /**
         * for error location.
         */
        unsigned int lineNum;

        const char *errorKind;
    } execStatus;

    DSContext();
    ~DSContext() = default;

    static const unsigned int originalShellLevel;
};


// #######################
// ##     DSContext     ##
// #######################

DSContext::DSContext() :
        ctx(), parser(), checker(this->ctx.getPool(), this->ctx.getSymbolTable()),
        lineNum(1), option(DS_OPTION_ASSERT), prompt(), execStatus() {
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
}

/**
 * get exit status of recently executed command.(also exit command)
 */
static int getExitStatus(DSContext *dsctx) {
    return dsctx->ctx.getExitStatus();
}

static void resetStatus(DSContext *dsctx) {
    dsctx->execStatus.type = DS_STATUS_SUCCESS;
    dsctx->execStatus.lineNum = 0;
    dsctx->execStatus.errorKind = "";
}

static void updateStatus(DSContext *dsctx, unsigned int type, unsigned int lineNum, const char *errorKind) {
    dsctx->execStatus.type = type;
    dsctx->execStatus.lineNum = lineNum;
    dsctx->execStatus.errorKind = errorKind;
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

static void handleParseError(DSContext *dsctx, const Lexer &lexer, const ParseError &e) {
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

    /**
     * update execution status
     */
    updateStatus(dsctx, DS_STATUS_PARSE_ERROR, errorLineNum, e.getErrorKind());
}

static void handleTypeError(DSContext *dsctx, const Lexer &lexer, const TypeCheckError &e) {
    unsigned int errorLineNum = lexer.getSourceInfoPtr()->getLineNum(e.getStartPos());

    const bool isatty = isSupportedTerminal(STDERR_FILENO);

    /**
     * show type error message
     */
    std::cerr << lexer.getSourceInfoPtr()->getSourceName() << ":" << errorLineNum << ":"
    << color(TermColor::Magenta, isatty) << " [semantic error] " << color(TermColor::Reset, isatty)
    << e.getMessage() << std::endl;
    formatErrorLine(isatty, lexer, e.getToken());

    /**
     * update execution status
     */
    updateStatus(dsctx, DS_STATUS_TYPE_ERROR,
                 lexer.getSourceInfoPtr()->getLineNum(e.getStartPos()), e.getKind());
}

static int eval(DSContext *dsctx, RootNode &rootNode) {
    ByteCodeGenerator codegen(dsctx->ctx.getPool(), hasFlag(dsctx->option, DS_OPTION_ASSERT));
    Callable c = codegen.generateToplevel(rootNode);

    if(hasFlag(dsctx->option, DS_OPTION_DUMP_CODE)) {
        std::cout << "### dump compiled code ###" << std::endl;
        dumpCode(std::cout, dsctx->ctx, c);
    }

    if(!vmEval(dsctx->ctx, c)) {
        unsigned int errorLineNum = 0;
        DSValue thrownObj = dsctx->ctx.getThrownObject();
        if(dynamic_cast<Error_Object *>(thrownObj.get()) != nullptr) {
            Error_Object *obj = typeAs<Error_Object>(thrownObj);
            errorLineNum = getOccuredLineNum(obj->getStackTrace());
        }

        dsctx->ctx.loadThrownObject();
        dsctx->ctx.handleUncaughtException(dsctx->ctx.pop());
        dsctx->checker.recover(false);
        updateStatus(dsctx, DS_STATUS_RUNTIME_ERROR, errorLineNum,
                     dsctx->ctx.getPool().getTypeName(*thrownObj->getType()).c_str());
        return 1;
    }
    return getExitStatus(dsctx);
}

static int eval(DSContext *dsctx, Lexer &lexer) {
    resetStatus(dsctx);

    lexer.setLineNum(dsctx->lineNum);
    RootNode rootNode;

    // parse
    try {
        dsctx->parser.parse(lexer, rootNode);
        dsctx->lineNum = lexer.getLineNum();

        if(hasFlag(dsctx->option, DS_OPTION_DUMP_UAST)) {
            std::cout << "### dump untyped AST ###" << std::endl;
            NodeDumper::dump(std::cout, dsctx->ctx.getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const ParseError &e) {
        handleParseError(dsctx, lexer, e);
        dsctx->lineNum = lexer.getLineNum();
        return 1;
    }

    // type check
    try {
        dsctx->checker.checkTypeRootNode(rootNode);

        if(hasFlag(dsctx->option, DS_OPTION_DUMP_AST)) {
            std::cout << "### dump typed AST ###" << std::endl;
            NodeDumper::dump(std::cout, dsctx->ctx.getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const TypeCheckError &e) {
        handleTypeError(dsctx, lexer, e);
        dsctx->checker.recover();
        return 1;
    }

    if(hasFlag(dsctx->option, DS_OPTION_PARSE_ONLY)) {
        return 0;
    }

    // eval
    return eval(dsctx, rootNode);
}

static void defineBuiltin(RootNode &rootNode, const char *varName, DSValue &&value) {
    rootNode.addNode(new BindVarNode(varName, std::move(value)));
}

static void defineBuiltin(RootNode &rootNode, const char *varName, const DSValue &value) {
    rootNode.addNode(new BindVarNode(varName, value));
}

static void initBuiltinVar(DSContext *dsctx) {
    RootNode rootNode;

    /**
     * management object for D-Bus related function
     * must be DBus_Object
     */
    defineBuiltin(rootNode, "DBus", DSValue(DBus_Object::newDBus_Object(&dsctx->ctx.getPool())));

    struct utsname name;
    if(uname(&name) == -1) {
        perror("cannot get utsname");
        exit(1);
    }

    /**
     * for os type detection.
     * must be String_Object
     */
    defineBuiltin(rootNode, "OSTYPE", DSValue::create<String_Object>(
            dsctx->ctx.getPool().getStringType(), name.sysname));

#define XSTR(V) #V
#define STR(V) XSTR(V)
    /**
     * for version detection
     * must be String_Object
     */
    defineBuiltin(rootNode, "YDSH_VERSION", DSValue::create<String_Object>(
            dsctx->ctx.getPool().getStringType(),
            STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "." STR(X_INFO_PATCH_VERSION)));
#undef XSTR
#undef STR

    /**
     * default variable for read command.
     * must be String_Object
     */
    defineBuiltin(rootNode, "REPLY", dsctx->ctx.getEmptyStrObj());

    std::vector<DSType *> types(2);
    types[0] = &dsctx->ctx.getPool().getStringType();
    types[1] = types[0];

    /**
     * holding read variable.
     * must be Map_Object
     */
    defineBuiltin(rootNode, "reply", DSValue::create<Map_Object>(
            dsctx->ctx.getPool().createReifiedType(dsctx->ctx.getPool().getMapTemplate(), std::move(types))));

    /**
     * contains exit status of most recent executed process. ($?)
     * must be Int_Object
     */
    defineBuiltin(rootNode, "?", DSValue::create<Int_Object>(dsctx->ctx.getPool().getInt32Type(), 0));

    /**
     * process id of root shell. ($$)
     * must be Int_Object
     */
    defineBuiltin(rootNode, "$", DSValue::create<Int_Object>(dsctx->ctx.getPool().getUint32Type(), getpid()));

    /**
     * contains script argument(exclude script name). ($@)
     * must be Array_Object
     */
    defineBuiltin(rootNode, "@", DSValue::create<Array_Object>(dsctx->ctx.getPool().getStringArrayType()));

    /**
     * contains size of argument. ($#)
     * must be Int_Object
     */
    defineBuiltin(rootNode, "#", DSValue::create<Int_Object>(dsctx->ctx.getPool().getInt32Type(), 0));

    /**
     * represent shell or shell script name.
     * must be String_Object
     */
    defineBuiltin(rootNode, "0", DSValue::create<String_Object>(dsctx->ctx.getPool().getStringType(), "ydsh"));

    /**
     * initialize positional parameter
     */
    for(unsigned int i = 0; i < 9; i++) {
        defineBuiltin(rootNode, std::to_string(i + 1).c_str(), dsctx->ctx.getEmptyStrObj());
    }


    // ignore error check (must be always success)
    dsctx->checker.checkTypeRootNode(rootNode);
    eval(dsctx, rootNode);
}

static void loadEmbeddedScript(DSContext *dsctx) {
    Lexer lexer("(embed)", embed_script);
    eval(dsctx, lexer);
    if(dsctx->execStatus.type != DS_STATUS_SUCCESS) {
        fatal("broken embedded script\n");
    }
    dsctx->ctx.getPool().commit();

    // rest some state
    dsctx->lineNum = 1;
    dsctx->ctx.updateExitStatus(0);
}

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

const unsigned int DSContext::originalShellLevel = getShellLevel();

// #####################################
// ##     public api of DSContext     ##
// #####################################

DSContext *DSContext_create() {
    DSContext *ctx = new DSContext();
    initBuiltinVar(ctx);
    loadEmbeddedScript(ctx);
    return ctx;
}

void DSContext_delete(DSContext **ctx) {
    if(ctx != nullptr) {
        delete (*ctx);
        *ctx = nullptr;
    }
}

int DSContext_eval(DSContext *ctx, const char *sourceName, const char *source) {
    Lexer lexer(sourceName == nullptr ? "(stdin)" : sourceName, source);
    return eval(ctx, lexer);
}

int DSContext_loadAndEval(DSContext *ctx, const char *sourceName, FILE *fp) {
    Lexer lexer(sourceName == nullptr ? "(stdin)" : sourceName, fp);
    return eval(ctx, lexer);
}

int DSContext_exec(DSContext *ctx, char *const argv[]) {
    resetStatus(ctx);
    ctx->ctx.execBuiltinCommand(argv);
    return getExitStatus(ctx);
}

void DSContext_setLineNum(DSContext *ctx, unsigned int lineNum) {
    ctx->lineNum = lineNum;
}

unsigned int DSContext_lineNum(DSContext *ctx) {
    return ctx->lineNum;
}

void DSContext_setShellName(DSContext *ctx, const char *shellName) {
    if(shellName != nullptr) {
        ctx->ctx.updateScriptName(shellName);
    }
}

void DSContext_setArguments(DSContext *ctx, char *const args[]) {
    if(args == nullptr) {
        return;
    }

    ctx->ctx.initScriptArg();
    for(unsigned int i = 0; args[i] != nullptr; i++) {
        ctx->ctx.addScriptArg(args[i]);
    }
    ctx->ctx.finalizeScriptArg();
}

static void setOptionImpl(DSContext *ctx, flag32_set_t flagSet, bool set) {
    if(hasFlag(flagSet, DS_OPTION_ASSERT)) {
        ctx->ctx.setAssertion(set);
    }
    if(hasFlag(flagSet, DS_OPTION_TOPLEVEL)) {
        ctx->checker.setToplevelPrinting(set);
    }
    if(hasFlag(flagSet, DS_OPTION_TRACE_EXIT)) {
        ctx->ctx.setTraceExit(set);
    }
}

void DSContext_setOption(DSContext *ctx, unsigned int optionSet) {
    setFlag(ctx->option, optionSet);
    setOptionImpl(ctx, optionSet, true);
}

void DSContext_unsetOption(DSContext *ctx, unsigned int optionSet) {
    unsetFlag(ctx->option, optionSet);
    setOptionImpl(ctx, optionSet, false);
}

const char *DSContext_prompt(DSContext *ctx, unsigned int n) {
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

    unsigned int index = ctx->ctx.getSymbolTable().lookupHandle(psName)->getFieldIndex();
    const DSValue &obj = ctx->ctx.getGlobal(index);

    ctx->ctx.interpretPromptString(typeAs<String_Object>(obj)->getValue(), ctx->prompt);
    return ctx->prompt.c_str();
}

int DSContext_supportDBus() {
    return hasFlag(DSContext_featureBit(), DS_FEATURE_DBUS) ? 1 : 0;
}

unsigned int DSContext_majorVersion() {
    return X_INFO_MAJOR_VERSION;
}

unsigned int DSContext_minorVersion() {
    return X_INFO_MINOR_VERSION;
}

unsigned int DSContext_patchVersion() {
    return X_INFO_PATCH_VERSION;
}

const char *DSContext_version() {
    return "ydsh, version " X_INFO_VERSION
            " (" X_INFO_SYSTEM "), build by " X_INFO_CPP " " X_INFO_CPP_V;
}

const char *DSContext_copyright() {
    return "Copyright (C) 2015-2016 Nagisa Sekiguchi";
}

unsigned int DSContext_featureBit() {
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

unsigned int DSContext_status(DSContext *ctx) {
    return ctx->execStatus.type;
}

unsigned int DSContext_errorLineNum(DSContext *ctx) {
    return ctx->execStatus.lineNum;
}

const char *DSContext_errorKind(DSContext *ctx) {
    return ctx->execStatus.errorKind;
}

void DSContext_addTerminationHook(DSContext *ctx, TerminationHook hook) {
    ctx->ctx.setTerminationHook(hook);
}

void DSContext_complete(DSContext *ctx, const char *buf, size_t cursor, DSCandidates *c) {
    if(c == nullptr) {
        return;
    }

    // init candidates
    c->size = 0;
    c->values = nullptr;

    if(ctx == nullptr || buf == nullptr || cursor == 0) {
        return;
    }

    std::string line(buf, cursor);
    LOG(DUMP_CONSOLE, "line: " << line << ", cursor: " << cursor);

    line += '\n';
    CStrBuffer sbuf = ctx->ctx.completeLine(line);

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