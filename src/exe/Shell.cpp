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

#include <exe/Shell.h>

#include <iostream>

namespace ydsh {

Shell::Shell(char **envp) :
        ctx(envp), parser(), checker(&this->ctx.pool), lineNum(1),
        listener(&clistener), dumpUntypedAST(false), dumpTypedAST(false) {
}

Shell::~Shell() {
}

ExitStatus Shell::eval(const char *line) {
    Lexer lexer(line);
    return this->eval("(stdin)", lexer, true);
}

ExitStatus Shell::eval(const char *sourceName, FILE *fp) {
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

void Shell::setDumpUntypedAST(bool dump) {
    this->dumpUntypedAST = dump;
}

void Shell::setDumpTypedAST(bool dump) {
    this->dumpTypedAST = dump;
}

void Shell::setAssertion(bool assertion) {
    this->ctx.assertion = assertion;
}

CommonErrorListener Shell::clistener;

ExitStatus Shell::eval(const char *sourceName, Lexer &lexer, bool interactive) {
    lexer.setLineNum(this->lineNum);
    RootNode rootNode;

    // parse
    try {
        this->parser.parse(lexer, rootNode);
        this->lineNum = lexer.getLineNum();

        if(this->dumpUntypedAST) {
            std::cout << "### dump untyped AST ###" << std::endl;
            dumpAST(std::cout, this->ctx.pool, rootNode);
            std::cout << std::endl;
        }
    } catch(const ParseError &e) {
        this->listener->displayParseError(lexer, sourceName, e);
        this->lineNum = lexer.getLineNum();
        return PARSE_ERROR;
    }

    // type check
    try {
        this->checker.checkTypeRootNode(rootNode);

        if(this->dumpTypedAST) {
            std::cout << "### dump typed AST ###" << std::endl;
            dumpAST(std::cout, this->ctx.pool, rootNode);
            std::cout << std::endl;
        }
    } catch(const TypeCheckError &e) {
        this->listener->displayTypeError(sourceName, e);
        this->checker.recover();
        return TYPE_ERROR;
    }

    // eval
    this->ctx.repl = interactive;
    EvalStatus status = rootNode.eval(this->ctx);
    if(status == EVAL_SUCCESS) {
        return SUCCESS;
    } else {
        this->checker.recover();
        std::cerr << "[runtime error] " << ctx.thrownObject->toString() << std::endl;
        return RUNTIME_ERROR;
    }
}

} /* namespace ydsh */
