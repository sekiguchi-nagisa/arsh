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

#include <parser/Lexer.h>
#include <parser/Parser.h>
#include <parser/ErrorListener.h>
#include <core/RuntimeContext.h>
#include <core/TypePool.h>
#include <parser/TypeChecker.h>
#include <core/DSType.h>
#include <ast/Node.h>
#include <ast/dump.h>

namespace ydsh {

using namespace ydsh::ast;
using namespace ydsh::parser;
using namespace ydsh::core;

typedef enum {
    SUCCESS,
    PARSE_ERROR,
    TYPE_ERROR,
    RUNTIME_ERROR,
} ExitStatus;

class Shell {
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

public:
    /**
     * envp is the pointer of environment variable.
     */
    Shell(char **envp);
    ~Shell();

    ExitStatus eval(const char *line);
    ExitStatus eval(const char *sourceName, FILE *fp);
    void setErrorListener(const ErrorListener *listener);
    void setLineNum(unsigned int lineNum);
    unsigned int getLineNum();
    void setArguments(const std::vector<const char *> &args);

    void setDumpUntypedAST(bool dump);
    void setDumpTypedAST(bool dump);
    void setParseOnly(bool parseOnly);
    void setAssertion(bool assertion);

private:
    /**
     * sourceName is null, if read stdin.
     */
    ExitStatus eval(const char *sourceName, Lexer<LexerDef, TokenKind> &lexer, bool interactive = false);

    /**
     * call only once.
     */
    void initBuiltinVar();

    static CommonErrorListener clistener;
};

void exec_interactive(const char *progName, Shell &shell);

} /* namespace ydsh */

#endif /* EXE_SHELL_H_ */
