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
#include <clocale>
#include <csignal>

#include <unistd.h>
#include <execinfo.h>

#include <ydsh/ydsh.h>
#include "config.h"
#include "embed.h"

#include "ast/node_dumper.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "parser/type_checker.h"
#include "core/context.h"
#include "core/symbol.h"
#include "misc/num.h"
#include "misc/term.h"


using namespace ydsh;
using namespace ydsh::ast;
using namespace ydsh::parser;
using namespace ydsh::core;

struct DSContext {
    RuntimeContext ctx;
    Parser parser;
    TypeChecker checker;
    unsigned int lineNum;

    // option
    flag32_set_t option;

    /**
     * for prompt
     */
    std::string ps1;
    std::string ps2;

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

    /**
     * get exit status of recently executed command.(also exit command)
     */
    int getExitStatus() {
        return typeAs<Int_Object>(this->ctx.getExitStatus())->getValue();
    }

    void resetStatus() {
        this->execStatus.type = DS_STATUS_SUCCESS;
        this->execStatus.lineNum = 0;
        this->execStatus.errorKind = "";
    }

    void updateStatus(unsigned int type, unsigned int lineNum, const char *errorKind) {
        this->execStatus.type = type;
        this->execStatus.lineNum = lineNum;
        this->execStatus.errorKind = errorKind;
    }

    void handleParseError(const Lexer &lexer, const ParseError &e);
    void handleTypeError(const Lexer &lexer, const TypeCheckError &e);

    int eval(Lexer &lexer);

    /**
     * call only once.
     */
    void initBuiltinVar();

    /**
     * call only once
     */
    void loadEmbeddedScript();

    static const unsigned int originalShellLevel;

    /**
     * if environmental variable SHLVL dose not exist, set 0.
     */
    static unsigned int getShellLevel();
};


// #######################
// ##     DSContext     ##
// #######################

DSContext::DSContext() :
        ctx(), parser(), checker(this->ctx.getPool(), this->ctx.getSymbolTable()),
        lineNum(1), option(0), ps1(), ps2(), execStatus() {
    // set locale
    setlocale(LC_ALL, "");
    setlocale(LC_MESSAGES, "C");

    // update shell level
    setenv("SHLVL", std::to_string(originalShellLevel + 1).c_str(), 1);

    // set some env
    if(getenv("HOME") == nullptr) {
        struct passwd *pw = getpwuid(getuid());
        if(pw == nullptr) {
            perror("getpwuid failed\n");
            fatal();
        }
        setenv("HOME", pw->pw_dir, 1);
    }
}

static std::ostream &formatErrorLine(std::ostream &stream, const Lexer &lexer, const Token &errorToken) {
    Token lineToken = lexer.getLineToken(errorToken, true);
    stream << misc::TermColor::Cyan << lexer.toTokenText(lineToken) << misc::reset << std::endl;
    stream << misc::TermColor::Green << lexer.formatLineMarker(lineToken, errorToken) << misc::reset;
    return stream;
}

void DSContext::handleParseError(const Lexer &lexer, const ParseError &e) {
    /**
     * show parse error message
     */
    unsigned int errorLineNum = lexer.getSourceInfoPtr()->getLineNum(e.getErrorToken().pos);
    if(e.getTokenKind() == EOS) {
        errorLineNum--;
    }

    std::cerr << lexer.getSourceInfoPtr()->getSourceName() << ":" << errorLineNum << ":"
    << misc::TermColor::Magenta << " [syntax error] " << misc::reset << e << std::endl;
    formatErrorLine(std::cerr, lexer, e.getErrorToken()) << std::endl;

    /**
     * update execution status
     */
    this->updateStatus(DS_STATUS_PARSE_ERROR, errorLineNum, e.getErrorKind());
}

void DSContext::handleTypeError(const Lexer &lexer, const TypeCheckError &e) {
    /**
     * show type error message
     */
    std::cerr << lexer.getSourceInfoPtr()->getSourceName() << ":"
    << lexer.getSourceInfoPtr()->getLineNum(e.getStartPos()) << ":"
    << misc::TermColor::Magenta << " [semantic error] " << misc::reset
    << e.getMessage() << std::endl;
    formatErrorLine(std::cerr, lexer, e.getToken()) << std::endl;

    /**
     * update execution status
     */
    this->updateStatus(DS_STATUS_TYPE_ERROR,
                       lexer.getSourceInfoPtr()->getLineNum(e.getStartPos()), e.getKind());
}

int DSContext::eval(Lexer &lexer) {
    this->resetStatus();

    lexer.setLineNum(this->lineNum);
    RootNode rootNode;

    // parse
    try {
        this->parser.parse(lexer, rootNode);
        this->lineNum = lexer.getLineNum();

        if(hasFlag(this->option, DS_OPTION_DUMP_UAST)) {
            std::cout << "### dump untyped AST ###" << std::endl;
            NodeDumper::dump(std::cout, this->ctx.getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const ParseError &e) {
        this->handleParseError(lexer, e);
        this->lineNum = lexer.getLineNum();
        return 1;
    }

    // type check
    try {
        this->checker.checkTypeRootNode(rootNode);

        if(hasFlag(this->option, DS_OPTION_DUMP_AST)) {
            std::cout << "### dump typed AST ###" << std::endl;
            NodeDumper::dump(std::cout, this->ctx.getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const TypeCheckError &e) {
        this->handleTypeError(lexer, e);
        this->checker.recover();
        return 1;
    }

    if(hasFlag(this->option, DS_OPTION_PARSE_ONLY)) {
        return 0;
    }

    // eval
    EvalStatus s;
    try {
        s = rootNode.eval(this->ctx);
    } catch(const InternalError &e) {
        s = EvalStatus::THROW;
    }

    if(s != EvalStatus::SUCCESS) {
        unsigned int errorLineNum = 0;
        DSValue thrownObj = this->ctx.getThrownObject();
        if(dynamic_cast<Error_Object *>(thrownObj.get()) != nullptr) {
            Error_Object *obj = typeAs<Error_Object>(thrownObj);
            errorLineNum = getOccuredLineNum(obj->getStackTrace());
        }

        DSType &thrownType = *thrownObj->getType();
        if(this->ctx.getPool().getInternalStatus().isSameOrBaseTypeOf(thrownType)) {
            if(thrownType == this->ctx.getPool().getShellExit()) {
                if(hasFlag(this->option, DS_OPTION_TRACE_EXIT)) {
                    this->ctx.loadThrownObject();
                    typeAs<Error_Object>(this->ctx.pop())->printStackTrace(this->ctx);
                }
                this->updateStatus(DS_STATUS_EXIT, errorLineNum, "");
                return this->getExitStatus();
            }
            if(thrownType == this->ctx.getPool().getAssertFail()) {
                this->ctx.loadThrownObject();
                typeAs<Error_Object>(this->ctx.pop())->printStackTrace(this->ctx);
                this->updateStatus(DS_STATUS_ASSERTION_ERROR, errorLineNum, "");
                return 1;
            }
        }
        this->ctx.reportError();
        this->checker.recover(false);
        this->updateStatus(DS_STATUS_RUNTIME_ERROR, errorLineNum,
                           this->ctx.getPool().getTypeName(*thrownObj->getType()).c_str());
        return 1;
    }
    return this->getExitStatus();
}

static void defineBuiltin(RootNode &rootNode, const char *varName, DSValue &&value) {
    rootNode.addNode(new BindVarNode(varName, std::move(value)));
}

static void defineBuiltin(RootNode &rootNode, const char *varName, const DSValue &value) {
    rootNode.addNode(new BindVarNode(varName, value));
}

void DSContext::initBuiltinVar() {
    RootNode rootNode;

    /**
     * management object for D-Bus related function
     * must be DBus_Object
     */
    defineBuiltin(rootNode, "DBus", DSValue(DBus_Object::newDBus_Object(&this->ctx.getPool())));

    /**
     * default variable for read command.
     * must be String_Object
     */
    defineBuiltin(rootNode, "REPLY", this->ctx.getEmptyStrObj());

    std::vector<DSType *> types(2);
    types[0] = &this->ctx.getPool().getStringType();
    types[1] = types[0];

    /**
     * holding read variable.
     * must be Map_Object
     */
    defineBuiltin(rootNode, "reply", DSValue::create<Map_Object>(
            this->ctx.getPool().createReifiedType(this->ctx.getPool().getMapTemplate(), std::move(types))));

    /**
     * contains script argument(exclude script name). ($@)
     * must be Array_Object
     */
    defineBuiltin(rootNode, "@", DSValue::create<Array_Object>(this->ctx.getPool().getStringArrayType()));

    /**
     * contains size of argument. ($#)
     * must be Int_Object
     */
    defineBuiltin(rootNode, "#", DSValue::create<Int_Object>(this->ctx.getPool().getInt32Type(), 0));

    /**
     * contains exit status of most recent executed process. ($?)
     * must be Int_Object
     */
    defineBuiltin(rootNode, "?", DSValue::create<Int_Object>(this->ctx.getPool().getInt32Type(), 0));

    /**
     * represent shell or shell script name.
     * must be String_Object
     */
    defineBuiltin(rootNode, "0", DSValue::create<String_Object>(this->ctx.getPool().getStringType(), "ydsh"));

    /**
     * initialize positional parameter
     */
    for(unsigned int i = 0; i < 9; i++) {
        defineBuiltin(rootNode, std::to_string(i + 1).c_str(), this->ctx.getEmptyStrObj());
    }


    // ignore error check (must be always success)
    this->checker.checkTypeRootNode(rootNode);
    rootNode.eval(this->ctx);
}

void DSContext::loadEmbeddedScript() {
    Lexer lexer("(embed)", embed_script);
    this->eval(lexer);
    if(this->execStatus.type != DS_STATUS_SUCCESS) {
        fatal("broken embedded script\n");
    }
    this->ctx.getPool().commit();

    // rest some state
    this->lineNum = 1;
    this->ctx.updateExitStatus(0);
}

const unsigned int DSContext::originalShellLevel = getShellLevel();

unsigned int DSContext::getShellLevel() {
    char *shlvl = getenv("SHLVL");
    unsigned int level = 0;
    if(shlvl != nullptr) {
        int status;
        long value = ydsh::misc::convertToInt64(shlvl, status);
        if(status != 0) {
            level = 0;
        } else {
            level = value;
        }
    }
    return level;
}

static void segvHandler(int, siginfo_t *info, void *) {
    // write message
    long addr = (long)info->si_addr;
    if(addr > 0) { // stack over flow
        char msg[] = "+++++ detect Stack Overflow +++++\n";
        write(STDERR_FILENO, msg, strlen(msg));
    } else {
        char msg[] = "+++++ detect Segmentation fault +++++\n";
        write(STDERR_FILENO, msg, strlen(msg));
    }

    // get backtrace
    const unsigned int size = 128;
    void *buf[size];

    int retSize = backtrace(buf, size);
    backtrace_symbols_fd(buf, retSize, STDERR_FILENO);
    fsync(STDERR_FILENO);

    abort();
}

static void installSEGVHandler() {
    // init alternative stack (for signal handler)
    static char altStack[SIGSTKSZ];
    stack_t ss;
    ss.ss_sp = altStack;
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;

    if(sigaltstack(&ss, nullptr) == -1) {
        perror("sigaltstack failed\n");
        exit(1);
    }

    // set signal handler for SIGSEGV
    struct sigaction act;
    act.sa_sigaction = segvHandler;
    act.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigfillset(&act.sa_mask);

    if(sigaction(SIGSEGV, &act, nullptr) < 0) {
        perror("setup signal handler failed\n");
        exit(1);
    }
}

// #####################################
// ##     public api of DSContext     ##
// #####################################

DSContext *DSContext_create() {
    installSEGVHandler();

    DSContext *ctx = new DSContext();
    ctx->initBuiltinVar();
    ctx->loadEmbeddedScript();
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
    return ctx->eval(lexer);
}

int DSContext_loadAndEval(DSContext *ctx, const char *sourceName, FILE *fp) {
    Lexer lexer(sourceName == nullptr ? "(stdin)" : sourceName, fp);
    return ctx->eval(lexer);
}

int DSContext_exec(DSContext *ctx, char *const argv[]) {
    ctx->resetStatus();

    EvalStatus es = EvalStatus::SUCCESS;
    try {
        ctx->ctx.getProcInvoker().execBuiltinCommand(argv);
    } catch(const InternalError &e) {
        es = EvalStatus::THROW;
    }

    if(es != EvalStatus::SUCCESS) {
        DSType *thrownType = ctx->ctx.getThrownObject()->getType();
        if(*thrownType == ctx->ctx.getPool().getShellExit()) {
            ctx->execStatus.type = DS_STATUS_EXIT;
        }
    }
    return ctx->getExitStatus();
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
    ctx->ctx.finalizeScritArg();
}

static void setOptionImpl(DSContext *ctx, flag32_set_t flagSet, bool set) {
    if(hasFlag(flagSet, DS_OPTION_ASSERT)) {
        ctx->ctx.setAssertion(set);
    }
    if(hasFlag(flagSet, DS_OPTION_TOPLEVEL)) {
        ctx->ctx.setToplevelPrinting(set);
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
    FieldHandle *handle = nullptr;
    bool usePS1 = true;
    switch(n) {
    case 1:
        handle = ctx->ctx.getSymbolTable().lookupHandle(VAR_PS1);
        break;
    case 2:
        handle = ctx->ctx.getSymbolTable().lookupHandle(VAR_PS2);
        usePS1 = false;
        break;
    default:
        break;
    }

    if(handle == nullptr) {
        return "";
    }

    unsigned int index = handle->getFieldIndex();
    const DSValue &obj = ctx->ctx.getGlobal(index);
    if(dynamic_cast<String_Object *>(obj.get()) == nullptr) {
        return "";
    }

    ctx->ctx.interpretPromptString(
            typeAs<String_Object>(obj)->getValue(), usePS1 ? ctx->ps1 : ctx->ps2);

    return (usePS1 ? ctx->ps1 : ctx->ps2).c_str();
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
