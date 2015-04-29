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

#define YDSH_MAJOR_VERSION 0
#define YDSH_MINOR_VERSION 0
#define YDSH_PATCH_VERSION 4

#if 1
#define DEV_STATE "-unstable"
#else
#define DEV_STATE ""
#endif

enum OptionKind {
    DUMP_UAST,
    DUMP_AST,
    PARSE_ONLY,
    DISABLE_ASSERT,
    VERSION,
    HELP,
};

static const char *getVersion() {
#define XSTR(S) #S
#define STR(S) XSTR(S)
    static const char version[] =
            "ydsh, version " STR(YDSH_MAJOR_VERSION) "."
                    STR(YDSH_MINOR_VERSION) "." STR(YDSH_PATCH_VERSION) DEV_STATE
                    " (" STR(X_INFO_SYSTEM) "), build by " STR(X_INFO_CPP) " " STR(X_INFO_CPP_V);
#undef STR
#undef XSTR
    return version;
}

static const char *getCopyright() {
    static const char copyright[] = "Copyright (c) 2015 Nagisa Sekiguchi";
    return copyright;
}

static void showVersion(std::ostream &stream) {
    stream << getVersion() << std::endl;
}

static void showCopyright(std::ostream &stream) {
    stream << getCopyright() << std::endl;
}

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
            (unsigned int) PARSE_ONLY,
            "--parse-only",
            false,
            "not evalute, parse only"
    );

    parser.addOption(
            (unsigned int) DISABLE_ASSERT,
            "--disable-assertion",
            false,
            "disable assert statement"
    );

    parser.addOption(
            (unsigned int) VERSION,
            "--version",
            false,
            "show version and copyright"
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
        showVersion(std::cerr);
        parser.printHelp(std::cerr);
        return ydsh::core::ARGS_ERROR;
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
        case PARSE_ONLY:
            shell.setParseOnly(true);
            break;
        case DISABLE_ASSERT:
            shell.setAssertion(false);
            break;
        case VERSION:
            showVersion(std::cout);
            showCopyright(std::cout);
            return ydsh::core::SUCCESS;
        case HELP:
            showVersion(std::cout);
            parser.printHelp(std::cout);
            return ydsh::core::SUCCESS;
        }
    }

    if(restArgs.size() > 0) {
        const char *scriptName = restArgs[0];
        FILE *fp = fopen(scriptName, "r");
        if(fp == NULL) {
            fprintf(stderr, "cannot open file: %s\n", scriptName);
            return ydsh::core::IO_ERROR;
        }
        shell.setArguments(restArgs);
        ydsh::ExitStatus status = shell.eval(scriptName, fp);
        fclose(fp);
        return status;
    } else {
        showVersion(std::cout);
        showCopyright(std::cout);

        exec_interactive(argv[0], shell);
    }
    return ydsh::core::SUCCESS;
}
