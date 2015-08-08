/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#include <ydsh/ydsh.h>
#include "config.h"

#include "ast/dump.h"
#include "parser/Lexer.h"
#include "parser/Parser.h"
#include "parser/TypeChecker.h"
#include "core/RuntimeContext.h"
#include "core/ErrorListener.h"
#include "misc/debug.h"
#include "misc/num.h"
#include "misc/files.h"


using namespace ydsh;
using namespace ydsh::ast;
using namespace ydsh::parser;
using namespace ydsh::core;

struct DSContext {
    RuntimeContext ctx;
    Parser parser;
    TypeChecker checker;
    unsigned int lineNum;

    /*
     * not delete it.
     */
    ErrorListener *listener;

    ProxyErrorListener proxy;
    ReportingListener reportingListener;

    // option
    flag32_set_t option;

    /**
     * for prompt
     */
    std::string ps1;
    std::string ps2;

    DSContext();
    ~DSContext() = default;

    void setErrorListener(ErrorListener * const listener) {
        this->listener = listener;
    }

    ErrorListener * const getErrorListener() const {
        return this->listener;
    }

    ErrorListener * const getDefaultListener() const {
        return &clistener;
    }

    /**
     * get exit status of recently executed command.(also exit command)
     */
    int getExitStatus() {
        return this->ctx.getExitStatus()->getValue();
    }

    const ReportingListener &getReportingListener() {
        return this->reportingListener;
    }

    /**
     * sourceName is null, if read stdin.
     */
    unsigned int eval(const char *sourceName, Lexer &lexer);

    /**
     * call only once.
     */
    void initBuiltinVar();

    /**
     * call only once
     */
    void initBuiltinIface();

    static CommonErrorListener clistener;

    static const unsigned int originalShellLevel;

    /**
     * if environmental variable SHLVL dose not exist, set 0.
     */
    static unsigned int getShellLevel();
};

struct DSStatus {
    /**
     * kind of execution status.
     */
    unsigned int type;

    /**
     * for error location.
     */
    unsigned int lineNum;

    const char *errorKind;

    DSStatus(unsigned int type, unsigned int lineNum, const char *errorKind) :
            type(type), lineNum(lineNum), errorKind(errorKind) { }

    ~DSStatus() = default;
};


// #######################
// ##     DSContext     ##
// #######################

DSContext::DSContext() :
        ctx(), parser(), checker(this->ctx.getPool(), this->ctx.getSymbolTable()), lineNum(1),
        listener(0), proxy(), reportingListener(), option(0), ps1(), ps2() {
    // set error listener
    this->setErrorListener(&this->proxy);
    this->proxy.addListener(this->getDefaultListener());
    this->proxy.addListener(&this->reportingListener);

    // update shell level
    setenv("SHLVL", std::to_string(originalShellLevel + 1).c_str(), 1);

    // set some env
    std::string wd = getCurrentWorkingDir();
    setenv("OLDPWD", wd.c_str(), 0);
    setenv("PWD", wd.c_str(), 0);
}

CommonErrorListener DSContext::clistener;

unsigned int DSContext::eval(const char *sourceName, Lexer &lexer) {
    sourceName = this->ctx.registerSourceName(sourceName);
    lexer.setLineNum(this->lineNum);
    RootNode rootNode(sourceName);

    // parse
    try {
        this->parser.parse(lexer, rootNode);
        this->lineNum = lexer.getLineNum();

        if(hasFlag(this->option, DS_OPTION_DUMP_UAST)) {
            std::cout << "### dump untyped AST ###" << std::endl;
            dumpAST(std::cout, this->ctx.getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const ParseError &e) {
        this->listener->handleParseError(lexer, sourceName, e);
        this->lineNum = lexer.getLineNum();
        return DS_STATUS_PARSE_ERROR;
    }

    // type check
    try {
        this->checker.checkTypeRootNode(rootNode);

        if(hasFlag(this->option, DS_OPTION_DUMP_AST)) {
            std::cout << "### dump typed AST ###" << std::endl;
            dumpAST(std::cout, this->ctx.getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const TypeCheckError &e) {
        this->listener->handleTypeError(sourceName, e);
        this->checker.recover();
        return DS_STATUS_TYPE_ERROR;
    }

    if(hasFlag(this->option, DS_OPTION_PARSE_ONLY)) {
        return DS_STATUS_SUCCESS;
    }

    // eval
    if(rootNode.eval(this->ctx) != EvalStatus::SUCCESS) {
        this->listener->handleRuntimeError(this->ctx.getPool(), this->ctx.getThrownObject());

        DSType *thrownType = this->ctx.getThrownObject()->getType();
        if(this->ctx.getPool().getInternalStatus()->isSameOrBaseTypeOf(thrownType)) {
            if(*thrownType == *this->ctx.getPool().getShellExit()) {
                if(hasFlag(this->option, DS_OPTION_TRACE_EXIT)) {
                    this->ctx.loadThrownObject();
                    TYPE_AS(Error_Object, this->ctx.pop())->printStackTrace(this->ctx);
                }
                return DS_STATUS_EXIT;
            }
            if(*thrownType == *this->ctx.getPool().getAssertFail()) {
                this->ctx.loadThrownObject();
                TYPE_AS(Error_Object, this->ctx.pop())->printStackTrace(this->ctx);
                return DS_STATUS_ASSERTION_ERROR;
            }
        }
        this->ctx.reportError();
        this->checker.recover(false);
        return DS_STATUS_RUNTIME_ERROR;
    }
    return DS_STATUS_SUCCESS;
}

static VarDeclNode *newStringVar(const char *name, const char *value, bool readOnly = false) {
    return new VarDeclNode(0, std::string(name), new StringValueNode(std::string(value)), readOnly);
}

void DSContext::initBuiltinVar() {
    RootNode rootNode;
    // register boolean
    rootNode.addNode(new BindVarNode("TRUE", this->ctx.getTrueObj()));
    rootNode.addNode(new BindVarNode("True", this->ctx.getTrueObj()));
    rootNode.addNode(new BindVarNode("true", this->ctx.getTrueObj()));
    rootNode.addNode(new BindVarNode("FALSE", this->ctx.getFalseObj()));
    rootNode.addNode(new BindVarNode("False", this->ctx.getFalseObj()));
    rootNode.addNode(new BindVarNode("false", this->ctx.getFalseObj()));

    // register special char
    rootNode.addNode(new BindVarNode("0", this->ctx.getScriptName()));
    rootNode.addNode(new BindVarNode("@", this->ctx.getScriptArgs()));
    rootNode.addNode(new BindVarNode("?", this->ctx.getExitStatus()));

    // register DBus management object
    rootNode.addNode(new BindVarNode("DBus", this->ctx.getDBus()));

    // env
    rootNode.addNode(new ImportEnvNode(0, std::string("OLDPWD")));
    rootNode.addNode(new ImportEnvNode(0, std::string("PWD")));

    // set alias
    rootNode.addNode(new TypeAliasNode("Int", "Int32"));
    rootNode.addNode(new TypeAliasNode("Uint", "Uint32"));

    // define writable variable
    rootNode.addNode(newStringVar("PS1", "\\s-\\v\\$ "));
    rootNode.addNode(newStringVar("PS2", "> "));

    // ignore error check (must be always success)
    this->checker.checkTypeRootNode(rootNode);
    rootNode.eval(this->ctx);
}

void DSContext::initBuiltinIface() {
    static const char builtinIface[] = ""
            "interface org.freedesktop.DBus.Peer {\n"
            "    function Ping()\n"
            "    function GetMachineId() : String\n"
            "}\n"
            "type-alias Peer org.freedesktop.DBus.Peer\n"
            "\n"
            "interface org.freedesktop.DBus.Introspectable {\n"
            "    function Introspect() : String\n"
            "}\n"
            "type-alias Introspectable org.freedesktop.DBus.Introspectable\n"
            "\n"
            "interface org.freedesktop.DBus.Properties {\n"
            "    function Get($iface : String, $property : String) : Variant\n"
            "    function Set($iface : String, $property : String, $value : Variant)\n"
            "    function GetAll($iface : String) : Map<String, Variant>\n"
            "    function PropertiesChanged($hd : Func<Void, [String, Map<String, Variant>, Array<String>]>)\n"
            "}\n"
            "type-alias Properties org.freedesktop.DBus.Properties\n"
            "\n"
            "type-alias ObjectAttr Map<String, Map<String, Variant>>\n"
            "interface org.freedesktop.DBus.ObjectManager {\n"
            "    function GetManagedObjects() : Map<ObjectPath, ObjectAttr>\n"
            "    function InterfacesAdded($hd : Func<Void, [ObjectPath, ObjectAttr]>)\n"
            "    function InterfacesRemoved($hd : Func<Void, [ObjectPath, Array<String>]>)\n"
            "}\n"
            "type-alias ObjectManager org.freedesktop.DBus.ObjectManager\n";

    Lexer lexer(builtinIface);
    unsigned int s = this->eval(0, lexer);
    if(s != DS_STATUS_SUCCESS) {
        fatal("broken builtin iface\n");
    }
    this->ctx.getPool().commit();
}

const unsigned int DSContext::originalShellLevel = getShellLevel();

unsigned int DSContext::getShellLevel() {
    char *shlvl = getenv("SHLVL");
    unsigned int level = 0;
    if(shlvl != nullptr) {
        int status;
        long value = ydsh::misc::convertToInt64(shlvl, status, false);
        if(status != 0) {
            level = 0;
        } else {
            level = value;
        }
    }
    return level;
}

// #####################################
// ##     public api of DSContext     ##
// #####################################

DSContext *DSContext_create() {
    DSContext *ctx = new DSContext();
    ctx->initBuiltinVar();
    ctx->initBuiltinIface();

    // reset line number
    ctx->lineNum = 1;

    return ctx;
}

void DSContext_delete(DSContext **ctx) {
    if(ctx != nullptr) {
        delete (*ctx);
        *ctx = nullptr;
    }
}

static int createStatus(unsigned int type, DSContext *ctx, DSStatus **status) {
    int ret = 1;
    unsigned int lineNum = ctx->getReportingListener().getLineNum();
    const char *errorKind = ctx->getReportingListener().getMessageKind();

    if(type == DS_STATUS_SUCCESS || type == DS_STATUS_EXIT) {
        ret = ctx->getExitStatus();
    }

    if(status != nullptr) {
        *status = new DSStatus(type, lineNum, errorKind);
    }
    return ret;
}

int DSContext_eval(DSContext *ctx, const char *source, DSStatus **status) {
    Lexer lexer(source);
    unsigned int s = ctx->eval(0, lexer);
    return createStatus(s, ctx, status);
}

int DSContext_loadAndEval(DSContext *ctx, const char *sourceName, FILE *fp, DSStatus **status) {
    Lexer lexer(fp);
    unsigned int s = ctx->eval(sourceName, lexer);
    return createStatus(s, ctx, status);
}

int DSContext_exec(DSContext *ctx, char *const argv[], DSStatus **status) {
    unsigned int s = DS_STATUS_SUCCESS;
    if(ctx->ctx.getProcInvoker().execBuiltinCommand(argv) != EvalStatus::SUCCESS) {
        DSType *thrownType = ctx->ctx.getThrownObject()->getType();
        if(*thrownType == *ctx->ctx.getPool().getShellExit()) {
            s = DS_STATUS_EXIT;
        }
    }
    return createStatus(s, ctx, status);
}

void DSContext_setLineNum(DSContext *ctx, unsigned int lineNum) {
    ctx->lineNum = lineNum;
}

unsigned int DSContext_getLineNum(DSContext *ctx) {
    return ctx->lineNum;
}

void DSContext_setArguments(DSContext *ctx, char *const argv[]) {
    for(unsigned int i = 0; argv[i] != nullptr; i++) {
        if(i == 0) {
            ctx->ctx.updateScriptName(argv[0]);
        } else if(strcmp(argv[i], "") != 0) {
            ctx->ctx.addScriptArg(argv[i]);
        }
    }
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

const char *DSContext_getPrompt(DSContext *ctx, unsigned int n) {
    static char empty[] = "";

    FieldHandle *handle = nullptr;
    bool usePS1 = true;
    switch(n) {
    case 1:
        handle = ctx->ctx.getSymbolTable().lookupHandle("PS1");
        break;
    case 2:
        handle = ctx->ctx.getSymbolTable().lookupHandle("PS2");
        usePS1 = false;
        break;
    default:
        break;
    }

    if(handle == nullptr) {
        return empty;
    }

    unsigned int index = handle->getFieldIndex();
    const std::shared_ptr<DSObject> &obj = ctx->ctx.getGlobal(index);
    if(dynamic_cast<String_Object *>(obj.get()) == nullptr) {
        return empty;
    }

    ctx->ctx.interpretPromptString(
            TYPE_AS(String_Object, obj)->getValue().c_str(), usePS1 ? ctx->ps1 : ctx->ps2);

    return (usePS1 ? ctx->ps1 : ctx->ps2).c_str();
}

int DSContext_supportDBus() {
#ifdef USE_DBUS
    return 1;
#else
    return 0;
#endif
}

unsigned int DSContext_getMajorVersion() {
    return X_INFO_MAJOR_VERSION;
}

unsigned int DSContext_getMinorVersion() {
    return X_INFO_MINOR_VERSION;
}

unsigned int DSContext_getPatchVersion() {
    return X_INFO_PATCH_VERSION;
}

const char *DSContext_getVersion() {
    return "ydsh, version " X_INFO_VERSION
            " (" X_INFO_SYSTEM "), build by " X_INFO_CPP " " X_INFO_CPP_V;
}

const char *DSContext_getCopyright() {
    return "Copyright (C) 2015 Nagisa Sekiguchi";
}


// ######################
// ##     DSStatus     ##
// ######################

void DSStatus_free(DSStatus **status) {
    if(status != nullptr) {
        delete (*status);
        *status = nullptr;
    }
}

unsigned int DSStatus_getType(DSStatus *status) {
    return status->type;
}

unsigned int DSStatus_getErrorLineNum(DSStatus *status) {
    return status->lineNum;
}

const char *DSStatus_getErrorKind(DSStatus *status) {
    return status->errorKind;
}