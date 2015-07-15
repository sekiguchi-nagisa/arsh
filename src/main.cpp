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
#include "misc/argv.hpp"
#include "config.h"

using namespace ydsh;

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

#define EACH_OPT(OP) \
    OP(DUMP_UAST,      "--dump-untyped-ast",  0, "dump abstract syntax tree (before type checking)") \
    OP(DUMP_AST,       "--dump-ast",          0, "dump abstract syntax tree (after type checking)") \
    OP(PARSE_ONLY,     "--parse-only",        0, "not evaluate, parse only") \
    OP(DISABLE_ASSERT, "--disable-assertion", 0, "disable assert statement") \
    OP(PRINT_TOPLEVEL, "--print-toplevel",    0, "print toplevel evaluated value") \
    OP(TRACE_EXIT,     "--trace-exit",        0, "trace execution process to exit command") \
    OP(VERSION,        "--version",           0, "show version and copyright") \
    OP(HELP,           "--help",              0, "show this help message") \
    OP(COMMAND,        "-c",                  argv::REQUIRE_ARG | argv::IGNORE_REST, "evaluate argument") \
    OP(NORC,           "--norc",              0, "not load ydshrc")

enum OptionKind {
#define GEN_ENUM(E, S, F, D) E,
    EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
};

static const argv::Option<OptionKind> options[] = {
#define GEN_OPT(E, S, F, D) {E, S, F, D},
        EACH_OPT(GEN_OPT)
#undef GEN_OPT
};

#undef EACH_OPT

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


    std::vector<std::pair<OptionKind, const char *>> cmdLines;
    std::vector<const char *> restArgs;
    try {
        restArgs = argv::parseArgv(argc, argv, options, cmdLines);
    } catch(const argv::ParseError &e) {
        std::cerr << e.getMessage() << std::endl;
        std::cerr << version << std::endl;
        std::cerr << options << std::endl;
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
            std::cerr << options << std::endl;
            return 0;
        case COMMAND:
            evalArg = cmdLine.second;
            break;
        case NORC:
            userc = false;
            break;
        }
    }

    // set rest argument
    unsigned int size = restArgs.size();
    const char *shellArgs[size + 1];
    for(unsigned int i = 0; i < size; i++) {
        shellArgs[i] = restArgs[i];
    }
    shellArgs[size] = nullptr;

    // evaluate argument
    if(evalArg != nullptr) {
        DSContext_setArguments(ctx, shellArgs);
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

        DSContext_setArguments(ctx, shellArgs);
        int ret = DSContext_loadAndEval(ctx, scriptName, fp, nullptr);
        DSContext_delete(&ctx);
        return ret;
    } else if(isatty(STDIN_FILENO) == 0) {
        int ret = DSContext_loadAndEval(ctx, nullptr, stdin, nullptr);
        DSContext_delete(&ctx);
        return ret;
    } else {    // interactive mode
        std::cout << version << std::endl << copyright << std::endl;
        if(userc) {
            loadRC(ctx);
        }

        exec_interactive(argv[0], ctx);
    }
    return 0;
}
