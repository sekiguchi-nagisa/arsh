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

namespace ydsh {

// ###################
// ##     Shell     ##
// ###################

Shell::Shell() :
        ctx(), parser(), checker(&this->ctx.getPool()), lineNum(1),
        listener(0), proxy(), reportingListener(),
        dumpUntypedAST(false), dumpTypedAST(false), parseOnly(false), traceExit(false) {
    // set error listener
    this->setErrorListener(&this->proxy);
    this->proxy.addListener(this->getDefaultListener());
    this->proxy.addListener(&this->reportingListener);
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
    rootNode.addNode(new BindVarNode("0", this->ctx.getScriptName(), true));
    rootNode.addNode(new BindVarNode("@", this->ctx.getScriptArgs(), true));
    rootNode.addNode(new BindVarNode("?", this->ctx.getExitStatus(), true));

    // register DBus management object
    rootNode.addNode(new BindVarNode("DBus", this->ctx.getDBus()));

    // set alias
    rootNode.addNode(new TypeAliasNode("Int", "Int32"));
    rootNode.addNode(new TypeAliasNode("Uint", "Uint32"));

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

} /* namespace ydsh */
