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
#include <unistd.h>

#include <ydsh/ydsh.h>
#include "misc/ArgsParser.hpp"

#define YDSH_MAJOR_VERSION 0
#define YDSH_MINOR_VERSION 2
#define YDSH_PATCH_VERSION 0

#if 1
#define DEV_STATE "-unstable"
#else
#define DEV_STATE ""
#endif

using namespace ydsh;

enum OptionKind {
    DUMP_UAST,
    DUMP_AST,
    PARSE_ONLY,
    DISABLE_ASSERT,
    PRINT_TOPLEVEL,
    VERSION,
    HELP,
};

void exec_interactive(const char *progName, DSContext *ctx);

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

static void loadRC(DSContext *ctx) {
    std::string path(getenv("HOME"));
    path += "/.ydshrc";
    FILE *fp = fopen(path.c_str(), "rb");
    if(fp == NULL) {
        return; // not read
    }
    DSStatus *s;
    int ret = DSContext_loadAndEval(ctx, path.c_str(), fp, &s);
    int type = DSStatus_getType(s);
    DSStatus_free(&s);
    if(type != DS_STATUS_SUCCESS) {
        DSContext_delete(&ctx);
        exit(ret);
    }
}

int main(int argc, char **argv) {
    args::ArgsParser<OptionKind> parser;

    parser.addOption(
            DUMP_UAST,
            "--dump-untyped-ast",
            false,
            "dump abstract syntax tree (before type checking)"
    );

    parser.addOption(
            DUMP_AST,
            "--dump-ast",
            false,
            "dump abstract syntax tree (after type checking)"
    );

    parser.addOption(
            PARSE_ONLY,
            "--parse-only",
            false,
            "not evaluate, parse only"
    );

    parser.addOption(
            DISABLE_ASSERT,
            "--disable-assertion",
            false,
            "disable assert statement"
    );

    parser.addOption(
            PRINT_TOPLEVEL,
            "--print-toplevel",
            false,
            "print toplevel evaluated value"
    );

    parser.addOption(
            VERSION,
            "--version",
            false,
            "show version and copyright"
    );

    parser.addOption(
            HELP,
            "--help",
            false,
            "show this help message"
    );

    std::vector<std::pair<OptionKind , const char *>> cmdLines;

    std::vector<const char *> restArgs;
    try {
        restArgs = parser.parse(argc, argv, cmdLines);
    } catch(const ydsh::args::ParseError &e) {
        std::cerr << e.message << ": " << e.suffix << std::endl;
        showVersion(std::cerr);
        parser.printHelp(std::cerr);
        return 1;
    }

    DSContext *ctx = DSContext_create();

    for(auto &cmdLine : cmdLines) {
        switch(cmdLine.first) {
        case DUMP_UAST:
            DSContext_setOption(ctx, DS_OPTION_DUMP_UAST);
            break;
        case DUMP_AST:
            DSContext_setOption(ctx, DS_OPTION_DUMP_AST);
            break;
        case PARSE_ONLY:
            DSContext_setOption(ctx, DS_OPTION_PARSE_ONLY);
            break;
        case DISABLE_ASSERT:
            DSContext_unsetOption(ctx, DS_OPTION_ASSERT);
            break;
        case PRINT_TOPLEVEL:
            DSContext_setOption(ctx, DS_OPTION_TOPLEVEL);
            break;
        case VERSION:
            showVersion(std::cout);
            showCopyright(std::cout);
            return 0;
        case HELP:
            showVersion(std::cout);
            parser.printHelp(std::cout);
            return 0;
        }
    }

    // load ydshrc
    loadRC(ctx);

    if(restArgs.size() > 0) {
        const char *scriptName = restArgs[0];
        FILE *fp = fopen(scriptName, "rb");
        if(fp == NULL) {
            fprintf(stderr, "cannot open file: %s\n", scriptName);
            return 1;
        }

        unsigned int size = restArgs.size();
        const char *shellArgs[size + 1];
        for(unsigned int i = 0; i < size; i++) {
            shellArgs[i] = restArgs[i];
        }
        shellArgs[size] = nullptr;

        DSContext_setArguments(ctx, shellArgs);
        int ret = DSContext_loadAndEval(ctx, scriptName, fp, nullptr);
        DSContext_delete(&ctx);
        return ret;
    } else if(isatty(STDIN_FILENO) == 0) {
        FILE *fp = fdopen(STDIN_FILENO, "rb");
        if(fp == NULL) {
            fprintf(stderr, "cannnot open stdin\n");
            return 1;
        }
        int ret = DSContext_loadAndEval(ctx, nullptr, fp, nullptr);
        DSContext_delete(&ctx);
        return ret;
    } else {
        showVersion(std::cout);
        showCopyright(std::cout);

        exec_interactive(argv[0], ctx);
    }
    return 0;
}
