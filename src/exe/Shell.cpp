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

Shell::Shell() :
        ctx(), parser(), checker(&this->ctx.pool), lineNum(1),
        listener(&clistener), dumpUntypedAST(false), dumpTypedAST(false) {
}

Shell::~Shell() {
}

bool Shell::eval(const char *line) {
    Lexer lexer(line);
    return this->eval("(stdin)", lexer, true);
}

bool Shell::eval(const char *sourceName, FILE *fp) {
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

CommonErrorListener Shell::clistener;

bool Shell::eval(const char *sourceName, Lexer &lexer, bool interactive) {
    lexer.setLineNum(this->lineNum);
    RootNode rootNode;
    try {
        this->parser.parse(lexer, rootNode);

        if(this->dumpUntypedAST) {
            std::cout << "### dump untyped AST ###" << std::endl;
            dumpAST(std::cout, rootNode);
            std::cout << std::endl;
        }
    } catch(const ParseError &e) {
        listener->displayParseError(lexer, sourceName, e);
        return false;
    }

    bool status = true;
    try {
        checker.checkTypeRootNode(rootNode);

        if(this->dumpTypedAST) {
            std::cout << "### dump typed AST ###" << std::endl;
            dumpAST(std::cout, rootNode);
            std::cout << std::endl;
        }

        // eval
        rootNode.eval(ctx, interactive);
    } catch(const TypeCheckError &e) {
        listener->displayTypeError(sourceName, e);
        checker.recover();
        status = false;
    }
    this->lineNum = lexer.getLineNum();

    return status;
}


