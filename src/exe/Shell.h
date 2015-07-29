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

#ifndef EXE_SHELL_H_
#define EXE_SHELL_H_

#include "../parser/Lexer.h"
#include "../parser/Parser.h"
#include "../core/RuntimeContext.h"
#include "../core/TypePool.h"
#include "../parser/TypeChecker.h"
#include "../core/DSType.h"
#include "../ast/Node.h"
#include "../ast/dump.h"
#include "ErrorListener.h"

namespace ydsh {

using namespace ydsh::ast;
using namespace ydsh::parser;
using namespace ydsh::core;

enum class ExecStatus : unsigned int {
    SUCCESS,
    PARSE_ERROR,
    TYPE_ERROR,
    RUNTIME_ERROR,
    ASSERTION_ERROR,
    EXIT,
};

class Shell {
private:
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
    bool dumpUntypedAST;
    bool dumpTypedAST;
    bool parseOnly;
    bool traceExit;

    /**
     * for prompt
     */
    std::string ps1;
    std::string ps2;

protected:
    Shell();

    /**
     * not allow copy constructor
     */
    explicit Shell(const Shell &shell);

public:
    ~Shell() = default;

    /**
     * not allow assignment
     */
    Shell &operator=(const Shell &shell);

    ExecStatus eval(const char *line);

    /**
     * fp must be opened binary mode.
     */
    ExecStatus eval(const char *sourceName, FILE *fp);

    void setErrorListener(ErrorListener * const listener) {
        this->listener = listener;
    }

    ErrorListener * const getErrorListener() const {
        return this->listener;
    }

    ErrorListener * const getDefaultListener() const {
        return &clistener;
    }

    void setLineNum(unsigned int lineNum) {
        this->lineNum = lineNum;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    /**
     * ignore empty string.
     * first element must be source name
     */
    void setArguments(const std::vector<const char *> &argv);

    void setDumpUntypedAST(bool dump) {
        this->dumpUntypedAST = dump;
    }

    void setDumpTypedAST(bool dump) {
        this->dumpTypedAST = dump;
    }

    void setParseOnly(bool parseOnly) {
        this->parseOnly = parseOnly;
    }

    void setAssertion(bool assertion) {
        this->ctx.setAssertion(assertion);
    }

    void setToplevelprinting(bool print) {
        this->ctx.setToplevelPrinting(print);
    }

    void setTraceExit(bool trace) {
        this->traceExit = trace;
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
     * n is 1 or 2(PS1, PS2)
     * return interpreted prompt string. if interpretation is failed, return empty string.
     */
    const char *getInterpretedPrompt(unsigned int n);

    static Shell *createShell();

private:
    /**
     * sourceName is null, if read stdin.
     */
    ExecStatus eval(const char *sourceName, Lexer &lexer);

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

} /* namespace ydsh */

#endif /* EXE_SHELL_H_ */
