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

#include <unistd.h>
#include <execinfo.h>

#include <csignal>
#include <iostream>

#include <ydsh/ydsh.h>
#include "misc/ArgsParser.hpp"
#include "config.h"

using namespace ydsh;

enum OptionKind {
    DUMP_UAST,
    DUMP_AST,
    PARSE_ONLY,
    DISABLE_ASSERT,
    PRINT_TOPLEVEL,
    TRACE_EXIT,
    VERSION,
    HELP,
    COMMAND,
    NORC,
};

void exec_interactive(const char *progName, DSContext *ctx);

/**
 * write version string.
 * not write new line.
 */
static std::ostream &version(std::ostream &stream) {
    return stream << "ydsh, version " X_INFO_VERSION
                             " (" X_INFO_SYSTEM "), build by " X_INFO_CPP " " X_INFO_CPP_V;
}

/**
 * write copyright.
 * not write new line.
 */
static std::ostream &copyright(std::ostream &stream) {
    return stream << "Copyright (C) 2015 Nagisa Sekiguchi";
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
    fclose(fp);
    if(type != DS_STATUS_SUCCESS) {
        DSContext_delete(&ctx);
        exit(ret);
    }
}

static void segvHandler(int num) {
    // write message
    char msg[] = "+++++ catch Segmentation fault +++++\n";
    write(STDERR_FILENO, msg, strlen(msg));

    // get backtrace
    const unsigned int size = 128;
    void *buf[size];

    int retSize = backtrace(buf, size);
    backtrace_symbols_fd(buf, retSize, STDERR_FILENO);
    fsync(STDERR_FILENO);

    abort();
}

int main(int argc, char **argv) {
    // init alternative stack (for signal handler)
    static char altStack[SIGSTKSZ];
    stack_t ss;
    ss.ss_sp = altStack;
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;

    if(sigaltstack(&ss, nullptr) == -1) {
        perror("sigaltstack failed\n");
        exit(1);
    }

    // set signal handler for SIGSEGV
    struct sigaction act;
    act.sa_handler = segvHandler;
    act.sa_flags = SA_ONSTACK;
    sigfillset(&act.sa_mask);

    if(sigaction(SIGSEGV, &act, nullptr) < 0) {
        perror("setup signal handeler failed\n");
        exit(1);
    }


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
            TRACE_EXIT,
            "--trace-exit",
            false,
            "trace execution process to exit command"
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

    parser.addOption(
            COMMAND,
            "-c",
            true,
            "evaluate argument"
    );

    parser.addOption(
            NORC,
            "--norc",
            false,
            "not load ydshrc"
    );

    std::vector<std::pair<OptionKind, const char *>> cmdLines;

    std::vector<const char *> restArgs;
    try {
        restArgs = parser.parse(argc, argv, cmdLines);
    } catch(const ydsh::args::ParseError &e) {
        std::cerr << e.message << ": " << e.suffix << std::endl;
        std::cerr << version << std::endl;
        parser.printHelp(std::cerr);
        return 1;
    }

    DSContext *ctx = DSContext_create();
    const char *evalArg = nullptr;
    bool userc = true;

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
        case TRACE_EXIT:
            DSContext_setOption(ctx, DS_OPTION_TRACE_EXIT);
            break;
        case VERSION:
            std::cout << version << std::endl << copyright << std::endl;
            return 0;
        case HELP:
            std::cout << version << std::endl;
            parser.printHelp(std::cout);
            return 0;
        case COMMAND:
            evalArg = cmdLine.second;
            goto EXEC;
        case NORC:
            userc = false;
            break;
        }
    }

    EXEC:

    // evaluate argument
    if(evalArg != nullptr) {
        DSContext_eval(ctx, evalArg, nullptr);
        int ret = DSContext_getExitStatus(ctx);
        DSContext_delete(&ctx);
        return ret;
    }

    // execute
    if(restArgs.size() > 0) {
        const char *scriptName = restArgs[0];
        FILE *fp = fopen(scriptName, "rb");
        if(fp == NULL) {
            std::cerr << "cannot open file: " << scriptName << std::endl;
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
        int ret = DSContext_loadAndEval(ctx, nullptr, stdin, nullptr);
        DSContext_delete(&ctx);
        return ret;
    } else {    // interactive mode
        if(userc) {
            loadRC(ctx);
        }
        std::cout << version << std::endl << copyright << std::endl;

        exec_interactive(argv[0], ctx);
    }
    return 0;
}
