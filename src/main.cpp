/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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
#include "misc/argv.hpp"

using namespace ydsh;

int exec_interactive(DSContext *ctx);

static void loadRC(DSContext *ctx, const char *rcfile) {
    std::string path;
    if(rcfile != nullptr) {
        path += rcfile;
    } else {
        path += getenv("HOME");
        path += "/.ydshrc";
    }

    FILE *fp = fopen(path.c_str(), "rb");
    if(fp == NULL) {
        return; // not read
    }
    int ret = DSContext_loadAndEval(ctx, path.c_str(), fp);
    int status = DSContext_status(ctx);
    fclose(fp);
    if(status != DS_STATUS_SUCCESS) {
        DSContext_delete(&ctx);
        exit(ret);
    }

    // reset line num
    DSContext_setLineNum(ctx, 1);
}

static const char *statusLogPath = nullptr;

template <typename F, F func, typename ...T>
static int invoke(DSContext **ctx, T&& ... args) {
    int ret = func(*ctx, std::forward<T>(args)...);
    if(statusLogPath != nullptr) {
        FILE *fp = fopen(statusLogPath, "w");
        if(fp != nullptr) {
            fprintf(fp, "type=%d lineNum=%d kind=%s\n",
                    DSContext_status(*ctx), DSContext_errorLineNum(*ctx), DSContext_errorKind(*ctx));
            fclose(fp);
        }
    }
    DSContext_delete(ctx);
    return ret;
}

#define INVOKE(F) invoke<decltype(DSContext_ ## F), DSContext_ ## F>

static void showFeature(std::ostream &stream) {
    const struct {
        const unsigned int featureFlag;
        const char *name;
    } set[] {
            {DS_FEATURE_LOGGING, "USE_LOGGING"},
            {DS_FEATURE_DBUS, "USE_DBUS"},
            {DS_FEATURE_SAFE_CAST, "USE_SAFE_CAST"},
    };

    const unsigned int featureBit = DSContext_featureBit();
    for(unsigned int i = 0; i < (sizeof(set) / sizeof(set[0])); i++) {
        if(ydsh::misc::hasFlag(featureBit, set[i].featureFlag)) {
            stream << set[i].name << std::endl;
        }
    }
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
    OP(COMMAND,        "-c",                  argv::HAS_ARG | argv::IGNORE_REST, "evaluate argument") \
    OP(NORC,           "--norc",              0, "not load rc file (only available interactive mode)") \
    OP(EXEC,           "-e",                  argv::HAS_ARG | argv::IGNORE_REST, "execute builtin command (ignore some option)") \
    OP(STATUS_LOG,     "--status-log",        argv::HAS_ARG, "write execution status to specified file (ignored in interactive mode)") \
    OP(FEATURE,        "--feature",           0, "show available features") \
    OP(RC_FILE,        "--rcfile",            argv::HAS_ARG, "load specified rc file (only available interactive mode)") \
    OP(QUIET,          "--quiet",             0, "suppress startup message (only available interactive mode)") \
    OP(SET_ARGS,       "-s",                  argv::IGNORE_REST, "set arguments and read command from standard input") \
    OP(INTERACTIVE,    "-i",                  0, "run interactive mode")

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

enum class InvocationKind {
    FROM_FILE,
    FROM_STDIN,
    FROM_STRING,
    BUILTIN,
};

int main(int argc, char **argv) {
    argv::CmdLines<OptionKind> cmdLines;
    int restIndex = argc;
    try {
        restIndex = argv::parseArgv(argc, argv, options, cmdLines);
    } catch(const argv::ParseError &e) {
        std::cerr << e.getMessage() << std::endl;
        std::cerr << DSContext_version() << std::endl;
        std::cerr << options << std::endl;
        return 1;
    }

    DSContext *ctx = DSContext_create();
    InvocationKind invocationKind = InvocationKind::FROM_FILE;
    const char *evalText = nullptr;
    bool userc = true;
    const char *rcfile = nullptr;
    bool quiet = false;
    bool forceInteractive = false;

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
            std::cout << DSContext_version() << std::endl;
            std::cout << DSContext_copyright() << std::endl;
            return 0;
        case HELP:
            std::cout << DSContext_version() << std::endl;
            std::cout << options << std::endl;
            return 0;
        case COMMAND:
            invocationKind = InvocationKind::FROM_STRING;
            evalText = cmdLine.second;
            break;
        case NORC:
            userc = false;
            break;
        case EXEC:
            invocationKind = InvocationKind::BUILTIN;
            restIndex--;
            break;
        case STATUS_LOG:
            statusLogPath = cmdLine.second;
            break;
        case FEATURE:
            showFeature(std::cout);
            return 0;
        case RC_FILE:
            rcfile = cmdLine.second;
            break;
        case QUIET:
            quiet = true;
            break;
        case SET_ARGS:
            invocationKind = InvocationKind::FROM_STDIN;
            break;
        case INTERACTIVE:
            forceInteractive = true;
            break;
        }
    }

    // set rest argument
    const int size = argc - restIndex;
    char *shellArgs[size + 1];
    memcpy(shellArgs, argv + restIndex, sizeof(char *) * size);
    shellArgs[size] = nullptr;

    if(invocationKind == InvocationKind::FROM_FILE && size == 0) {
        invocationKind = InvocationKind::FROM_STDIN;
    }

    // execute
    switch(invocationKind) {
    case InvocationKind::FROM_FILE: {
        const char *scriptName = shellArgs[0];
        FILE *fp = fopen(scriptName, "rb");
        if(fp == NULL) {
            fprintf(stderr, "ydsh: %s: %s\n", scriptName, strerror(errno));
            return 1;
        }

        DSContext_setShellName(ctx, scriptName);
        DSContext_setArguments(ctx, shellArgs + 1);
        return INVOKE(loadAndEval)(&ctx, scriptName, fp);
    }
    case InvocationKind::FROM_STDIN: {
        DSContext_setArguments(ctx, shellArgs);

        if(isatty(STDIN_FILENO) == 0 && !forceInteractive) {  // pipe line mode
            return INVOKE(loadAndEval)(&ctx, nullptr, stdin);
        } else {    // interactive mode
            if(!quiet) {
                std::cout << DSContext_version() << std::endl;
                std::cout << DSContext_copyright() << std::endl;
            }
            if(userc) {
                loadRC(ctx, rcfile);
            }

            return exec_interactive(ctx);
        }
    }
    case InvocationKind::FROM_STRING: {
        DSContext_setShellName(ctx, shellArgs[0]);
        DSContext_setArguments(ctx, size == 0 ? nullptr : shellArgs + 1);
        return INVOKE(eval)(&ctx, "(string)", evalText);
    }
    case InvocationKind::BUILTIN: {
        return INVOKE(exec)(&ctx, (char **)shellArgs);
    }
    }
}
