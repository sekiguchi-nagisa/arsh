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

namespace ydsh {

Shell::Shell(char **envp) :
        ctx(envp), parser(), checker(&this->ctx.pool), lineNum(1),
        listener(&clistener), dumpUntypedAST(false), dumpTypedAST(false) {
    this->initBuiltinVar();
}

Shell::~Shell() {
}

ExitStatus Shell::eval(const char *line) {
    Lexer<LexerDef, TokenKind> lexer(line);
    return this->eval("(stdin)", lexer, true);
}

ExitStatus Shell::eval(const char *sourceName, FILE *fp) {
    Lexer<LexerDef, TokenKind> lexer(fp);
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
    this->ctx.scriptName->value = args[0];
    for(unsigned int i = 1; i < size; i++) {
        this->ctx.scriptArgs->values.push_back(
                std::make_shared<String_Object>( this->ctx.pool.getStringType(), std::string(args[i])));
    }
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

ExitStatus Shell::eval(const char *sourceName, Lexer<LexerDef, TokenKind> &lexer, bool interactive) {
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
        this->ctx.reportError();
        return RUNTIME_ERROR;
    }
}

void Shell::initBuiltinVar() {
    RootNode rootNode;
    // register boolean
    rootNode.addNode(new BindVarNode("TRUE", this->ctx.trueObj));
    rootNode.addNode(new BindVarNode("True", this->ctx.trueObj));
    rootNode.addNode(new BindVarNode("true", this->ctx.trueObj));
    rootNode.addNode(new BindVarNode("FALSE", this->ctx.falseObj));
    rootNode.addNode(new BindVarNode("False", this->ctx.falseObj));
    rootNode.addNode(new BindVarNode("false", this->ctx.falseObj));

    // register special char
    rootNode.addNode(new BindVarNode("0", this->ctx.scriptName));
    rootNode.addNode(new BindVarNode("@", this->ctx.scriptArgs));

    // ignore error check (must be always success)
    this->checker.checkTypeRootNode(rootNode);
    rootNode.eval(this->ctx);
}

} /* namespace ydsh */
