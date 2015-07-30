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

#include "Shell.h"
#include "../misc/debug.h"
#include "../misc/num.h"
#include "../misc/files.h"

namespace ydsh {

// ###################
// ##     Shell     ##
// ###################

Shell::Shell() :
        ctx(), parser(), checker(this->ctx.getPool(), this->ctx.getSymbolTable()), lineNum(1),
        listener(0), proxy(), reportingListener(),
        dumpUntypedAST(false), dumpTypedAST(false), parseOnly(false), traceExit(false), ps1(), ps2() {
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

ExecStatus Shell::eval(const char *line) {
    Lexer lexer(line, true);    // zero copy buf.
    return this->eval(0, lexer);
}

ExecStatus Shell::eval(const char *sourceName, FILE *fp) {
    Lexer lexer(fp);
    return this->eval(sourceName, lexer);
}

void Shell::setArguments(const std::vector<const char *> &argv) {
    unsigned int size = argv.size();
    if(size == 0) {
        return;
    }
    this->ctx.updateScriptName(argv[0]);
    for(unsigned int i = 1; i < size; i++) {
        if(strcmp(argv[i], "") != 0) {
            this->ctx.addScriptArg(argv[i]);
        }
    }
}

const char *Shell::getInterpretedPrompt(unsigned int n) {
    static char empty[] = "";

    FieldHandle *handle = nullptr;
    bool usePS1 = true;
    switch(n) {
    case 1:
        handle = this->ctx.getSymbolTable().lookupHandle("PS1");
        break;
    case 2:
        handle = this->ctx.getSymbolTable().lookupHandle("PS2");
        usePS1 = false;
        break;
    default:
        break;
    }

    if(handle == nullptr) {
        return empty;
    }

    unsigned int index = handle->getFieldIndex();
    const std::shared_ptr<DSObject> &obj = this->ctx.getGlobal(index);
    if(dynamic_cast<String_Object *>(obj.get()) == nullptr) {
        return empty;
    }

    this->ctx.interpretPromptString(
            TYPE_AS(String_Object, obj)->getValue().c_str(), usePS1 ? this->ps1 : this->ps2);

    return (usePS1 ? this->ps1 : this->ps2).c_str();
}

ExecStatus Shell::exec(char *const argv[]) {
    if(this->ctx.getProcInvoker().execBuiltinCommand(argv) != EvalStatus::SUCCESS) {
        DSType *thrownType = this->ctx.getThrownObject()->getType();
        if(*thrownType == *this->ctx.getPool().getShellExit()) {
            return ExecStatus::EXIT;
        }
    }
    return ExecStatus::SUCCESS;
}

Shell *Shell::createShell() {
    Shell *shell = new Shell();
    shell->initBuiltinVar();
    shell->initBuiltinIface();

    // reset line number
    shell->lineNum = 1;

    return shell;
}

CommonErrorListener Shell::clistener;

ExecStatus Shell::eval(const char *sourceName, Lexer &lexer) {
    sourceName = this->ctx.registerSourceName(sourceName);
    lexer.setLineNum(this->lineNum);
    RootNode rootNode(sourceName);

    // parse
    try {
        this->parser.parse(lexer, rootNode);
        this->lineNum = lexer.getLineNum();

        if(this->dumpUntypedAST) {
            std::cout << "### dump untyped AST ###" << std::endl;
            dumpAST(std::cout, this->ctx.getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const ParseError &e) {
        this->listener->handleParseError(lexer, sourceName, e);
        this->lineNum = lexer.getLineNum();
        return ExecStatus::PARSE_ERROR;
    }

    // type check
    try {
        this->checker.checkTypeRootNode(rootNode);

        if(this->dumpTypedAST) {
            std::cout << "### dump typed AST ###" << std::endl;
            dumpAST(std::cout, this->ctx.getPool(), rootNode);
            std::cout << std::endl;
        }
    } catch(const TypeCheckError &e) {
        this->listener->handleTypeError(sourceName, e);
        this->checker.recover();
        return ExecStatus::TYPE_ERROR;
    }

    if(this->parseOnly) {
        return ExecStatus::SUCCESS;
    }

    // eval
    if(rootNode.eval(this->ctx) != EvalStatus::SUCCESS) {
        this->listener->handleRuntimeError(this->ctx.getPool(), this->ctx.getThrownObject());

        DSType *thrownType = this->ctx.getThrownObject()->getType();
        if(this->ctx.getPool().getInternalStatus()->isSameOrBaseTypeOf(thrownType)) {
            if(*thrownType == *this->ctx.getPool().getShellExit()) {
                if(this->traceExit) {
                    this->ctx.loadThrownObject();
                    TYPE_AS(Error_Object, this->ctx.pop())->printStackTrace(this->ctx);
                }
                return ExecStatus::EXIT;
            }
            if(*thrownType == *this->ctx.getPool().getAssertFail()) {
                this->ctx.loadThrownObject();
                TYPE_AS(Error_Object, this->ctx.pop())->printStackTrace(this->ctx);
                return ExecStatus::ASSERTION_ERROR;
            }
        }
        this->ctx.reportError();
        this->checker.recover(false);
        return ExecStatus::RUNTIME_ERROR;
    }
    return ExecStatus::SUCCESS;
}

static VarDeclNode *newStringVar(const char *name, const char *value, bool readOnly = false) {
    return new VarDeclNode(0, std::string(name), new StringValueNode(std::string(value)), readOnly);
}

void Shell::initBuiltinVar() {
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

void Shell::initBuiltinIface() {
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
    
    ExecStatus s = this->eval(builtinIface);
    if(s != ExecStatus::SUCCESS) {
        fatal("broken builtin iface\n");
    }
    this->ctx.getPool().commit();
}

const unsigned int Shell::originalShellLevel = getShellLevel();

unsigned int Shell::getShellLevel() {
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


} /* namespace ydsh */
