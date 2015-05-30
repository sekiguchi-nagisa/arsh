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

#include "Shell.h"

namespace ydsh {

Shell::Shell() :
        ctx(), parser(), checker(&this->ctx.getPool()), lineNum(1),
        listener(&clistener), dumpUntypedAST(false),
        dumpTypedAST(false), parseOnly(false) {
    this->initBuiltinVar();
}

ShellStatus Shell::eval(const char *line) {
    Lexer lexer(line);
    return this->eval(0, lexer);
}

ShellStatus Shell::eval(const char *sourceName, FILE *fp) {
    Lexer lexer(fp);
    return this->eval(sourceName, lexer);
}

void Shell::setErrorListener(const ErrorListener *listener) {
    this->listener = listener;
}

void Shell::setLineNum(unsigned int lineNum) {
    this->lineNum = lineNum;
}

unsigned int Shell::getLineNum() {
    return this->lineNum;
}

void Shell::setArguments(const std::vector<const char *> &args) {
    unsigned int size = args.size();
    this->ctx.setScriptName(args[0]);
    for(unsigned int i = 1; i < size; i++) {
        this->ctx.addScriptArg(args[i]);
    }
}

void Shell::setDumpUntypedAST(bool dump) {
    this->dumpUntypedAST = dump;
}

void Shell::setDumpTypedAST(bool dump) {
    this->dumpTypedAST = dump;
}

void Shell::setParseOnly(bool parseOnly) {
    this->parseOnly = parseOnly;
}

void Shell::setAssertion(bool assertion) {
    this->ctx.setAssertion(assertion);
}

void Shell::setToplevelprinting(bool print) {
    this->ctx.setToplevelPrinting(print);
}

const std::string &Shell::getWorkingDir() {
    return this->ctx.getWorkingDir();
}

int Shell::getExitStatus() {
    return this->ctx.getExitStatus()->getValue();
}

CommonErrorListener Shell::clistener;

ShellStatus Shell::eval(const char *sourceName, Lexer &lexer) {
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
        this->listener->displayParseError(lexer, sourceName, e);
        this->lineNum = lexer.getLineNum();
        return ShellStatus::PARSE_ERROR;
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
        this->listener->displayTypeError(sourceName, e);
        this->checker.recover();
        return ShellStatus::TYPE_ERROR;
    }

    if(this->parseOnly) {
        return ShellStatus::SUCCESS;
    }
    
    // eval
    switch(rootNode.eval(this->ctx)) {
    case EvalStatus::SUCCESS:
        return ShellStatus::SUCCESS;
    case EvalStatus::ASSERT_FAIL:
        return ShellStatus::ASSERTION_ERROR;
    case EvalStatus::EXIT:
        return ShellStatus::EXIT;
    default:
        this->checker.recover();
        this->ctx.reportError();
        return ShellStatus::RUNTIME_ERROR;
    }
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
    rootNode.addNode(new BindVarNode("DBus",this->ctx.getDBus()));

    // set alias
    rootNode.addNode(new TypeAliasNode("Int", "Int32"));
    rootNode.addNode(new TypeAliasNode("Uint", "Uint32"));

    // ignore error check (must be always success)
    this->checker.checkTypeRootNode(rootNode);
    rootNode.eval(this->ctx);
}

} /* namespace ydsh */
