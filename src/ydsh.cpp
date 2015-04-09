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

#include <iostream>
#include <stdlib.h>

#include <exe/Shell.h>
#include <misc/ArgsParser.h>
#include <core/builtin.cpp>

enum OptionKind {
    DUMP_UAST,
    DUMP_AST,
    DISABLE_ASSERT,
    HELP,
};

int main(int argc, char **argv, char **envp) {
    ydsh::args::ArgsParser parser;

    parser.addOption(
            (unsigned int) DUMP_UAST,
            "--dump-untyped-ast",
            false,
            "dump abstract syntax tree (before type checking)"
    );

    parser.addOption(
            (unsigned int) DUMP_AST,
            "--dump-ast",
            false,
            "dump abstract syntax tree (after type checking)"
    );

    parser.addOption(
            (unsigned int) DISABLE_ASSERT,
            "--disable-assertion",
            false,
            "disable assert statement"
    );

    parser.addOption(
            (unsigned int) HELP,
            "--help",
            false,
            "show this help message"
    );

    std::vector<std::pair<unsigned int, const char *>> cmdLines;

    std::vector<const char *> restArgs;
    try {
        restArgs = parser.parse(argc, argv, cmdLines);
    } catch(const ydsh::args::ParseError &e) {
        std::cerr << e.getMessage() << ": " << e.getSuffix() << std::endl;
        parser.printHelp(std::cerr);
        return 1;
    }

    ydsh::Shell shell(envp);

    for(const std::pair<unsigned int, const char *> &cmdLine : cmdLines) {
        switch((OptionKind)cmdLine.first) {
        case DUMP_UAST:
            shell.setDumpUntypedAST(true);
            break;
        case DUMP_AST:
            shell.setDumpTypedAST(true);
            break;
        case DISABLE_ASSERT:
            shell.setAssertion(false);
            break;
        case HELP:
            parser.printHelp(std::cout);
            return 0;
        }
    }

    if(restArgs.size() > 0) {
        const char *scriptName = restArgs[0];
        FILE *fp = fopen(scriptName, "r");
        if(fp == NULL) {
            fprintf(stderr, "cannot open file: %s\n", scriptName);
            return 1;
        }
        shell.setArguments(restArgs);
        ydsh::ExitStatus status = shell.eval(scriptName, fp);
        fclose(fp);
        return status;
    } else {
        exec_interactive(argv[0], shell);
    }
    return 0;
}
