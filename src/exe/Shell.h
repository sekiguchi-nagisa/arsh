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
#include "../parser/ErrorListener.h"
#include "../core/RuntimeContext.h"
#include "../core/TypePool.h"
#include "../parser/TypeChecker.h"
#include "../core/DSType.h"
#include "../ast/Node.h"
#include "../ast/dump.h"
#include <ydsh/ydsh.h>

namespace ydsh {

using namespace ydsh::ast;
using namespace ydsh::parser;
using namespace ydsh::core;

class Shell : public ExecutionEngine {
private:
    RuntimeContext ctx;
    Parser parser;
    TypeChecker checker;
    unsigned int lineNum;

    /*
     * not delete it.
     */
    const ErrorListener *listener;

    // option
    bool dumpUntypedAST;
    bool dumpTypedAST;
    bool parseOnly;

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

    ExecStatus eval(const char *line); // override
    ExecStatus eval(const char *sourceName, FILE *fp); // overrie
    void setErrorListener(const ErrorListener *listener);
    void setLineNum(unsigned int lineNum); // override
    unsigned int getLineNum();  // override
    void setArguments(const std::vector<const char *> &args);   // override

    void setDumpUntypedAST(bool dump);  // override
    void setDumpTypedAST(bool dump);    // override
    void setParseOnly(bool parseOnly);  // override
    void setAssertion(bool assertion); // override
    void setToplevelprinting(bool print);   // override

    const std::string &getWorkingDir(); // override

    /**
     * get exit status of recently executed command.(also exit command)
     */
    int getExitStatus();

    static std::unique_ptr<Shell> createShell();

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
    void initbuiltinIface();

    static CommonErrorListener clistener;
};

} /* namespace ydsh */

#endif /* EXE_SHELL_H_ */
