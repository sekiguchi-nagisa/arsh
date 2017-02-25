/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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
#include "misc/size.hpp"

using namespace ydsh;

int exec_interactive(DSState *dsState);

static void loadRC(DSState *state, const char *rcfile) {
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
    DSError e;
    int ret = DSState_loadAndEval(state, path.c_str(), fp, &e);
    int kind = e.kind;
    DSError_release(&e);
    fclose(fp);
    if(kind != DS_ERROR_KIND_SUCCESS) {
        exit(ret);
    }

    // reset line num
    DSState_setLineNum(state, 1);
}

static const char *statusLogPath = nullptr;

template <typename F, F func, typename ...T>
static int invoke(DSState **state, T&& ... args) {
    DSError error;
    int ret = func(*state, std::forward<T>(args)..., &error);
    if(statusLogPath != nullptr) {
        FILE *fp = fopen(statusLogPath, "w");
        if(fp != nullptr) {
            fprintf(fp, "kind=%d lineNum=%d name=%s\n",
                    error.kind, error.lineNum, error.name != nullptr ? error.name : "");
            fclose(fp);
        }
    }
    DSError_release(&error);
    return ret;
}

#define INVOKE(F) invoke<decltype(&DSState_ ## F), DSState_ ## F>

static void terminationHook(unsigned int kind, unsigned int errorLineNum) {
    if(statusLogPath != nullptr) {
        FILE *fp = fopen(statusLogPath, "w");
        if(fp != nullptr) {
            fprintf(fp, "kind=%d lineNum=%d name=\n", kind, errorLineNum);
            fclose(fp);
        }
    }
}

static void showFeature(std::ostream &stream) {
    const char *featureNames[] = {
            "USE_LOGGING",
            "USE_DBUS",
            "USE_SAFE_CAST",
            "USE_FIXED_TIME"
    };

    const unsigned int featureBit = DSState_featureBit();
    for(unsigned int i = 0; i < arraySize(featureNames); i++) {
        if(hasFlag(featureBit, static_cast<unsigned int>(1 << i))) {
            stream << featureNames[i] << std::endl;
        }
    }
}

#define EACH_OPT(OP) \
    OP(DUMP_UAST,      "--dump-untyped-ast",  0, "dump abstract syntax tree (before type checking)") \
    OP(DUMP_AST,       "--dump-ast",          0, "dump abstract syntax tree (after type checking)") \
    OP(DUMP_CODE,      "--dump-code",         0, "dump compiled code") \
    OP(PARSE_ONLY,     "--parse-only",        0, "not evaluate, parse only") \
    OP(DISABLE_ASSERT, "--disable-assertion", 0, "disable assert statement") \
    OP(PRINT_TOPLEVEL, "--print-toplevel",    0, "print top level evaluated value") \
    OP(TRACE_EXIT,     "--trace-exit",        0, "trace execution process to exit command") \
    OP(VERSION,        "--version",           0, "show version and copyright") \
    OP(HELP,           "--help",              0, "show this help message") \
    OP(COMMAND,        "-c",                  argv::HAS_ARG | argv::IGNORE_REST, "evaluate argument") \
    OP(NORC,           "--norc",              0, "not load rc file (only available interactive mode)") \
    OP(EXEC,           "-e",                  argv::HAS_ARG | argv::IGNORE_REST, "execute builtin command (ignore some option)") \
    OP(STATUS_LOG,     "--status-log",        argv::HAS_ARG, "write execution status to specified file (ignored in interactive mode or -e)") \
    OP(FEATURE,        "--feature",           0, "show available features") \
    OP(RC_FILE,        "--rcfile",            argv::HAS_ARG, "load specified rc file (only available interactive mode)") \
    OP(QUIET,          "--quiet",             0, "suppress startup message (only available interactive mode)") \
    OP(SET_ARGS,       "-s",                  argv::IGNORE_REST, "set arguments and read command from standard input") \
    OP(INTERACTIVE,    "-i",                  0, "run interactive mode") \
    OP(PARSE_ONLY2,    "-n",                  0, "equivalent to `--parse-only' option")

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

static const char *version() {
    return DSState_version(nullptr, 0);
}

int main(int argc, char **argv) {
    argv::CmdLines<OptionKind> cmdLines;
    int restIndex;
    try {
        restIndex = argv::parseArgv(argc, argv, options, cmdLines);
    } catch(const argv::ParseError &e) {
        std::cerr << e.getMessage() << std::endl;
        std::cerr << version() << std::endl;
        std::cerr << options << std::endl;
        exit(1);
    }

    DSState *state = DSState_create();
    DSState_addTerminationHook(state, terminationHook);
    InvocationKind invocationKind = InvocationKind::FROM_FILE;
    const char *evalText = nullptr;
    bool userc = true;
    const char *rcfile = nullptr;
    bool quiet = false;
    bool forceInteractive = false;

    for(auto &cmdLine : cmdLines) {
        switch(cmdLine.first) {
        case DUMP_UAST:
            DSState_setOption(state, DS_OPTION_DUMP_UAST);
            break;
        case DUMP_AST:
            DSState_setOption(state, DS_OPTION_DUMP_AST);
            break;
        case DUMP_CODE:
            DSState_setOption(state, DS_OPTION_DUMP_CODE);
            break;
        case PARSE_ONLY:
        case PARSE_ONLY2:
            DSState_setOption(state, DS_OPTION_PARSE_ONLY);
            break;
        case DISABLE_ASSERT:
            DSState_unsetOption(state, DS_OPTION_ASSERT);
            break;
        case PRINT_TOPLEVEL:
            DSState_setOption(state, DS_OPTION_TOPLEVEL);
            break;
        case TRACE_EXIT:
            DSState_setOption(state, DS_OPTION_TRACE_EXIT);
            break;
        case VERSION:
            std::cout << version() << std::endl;
            std::cout << DSState_copyright() << std::endl;
            exit(0);
        case HELP:
            std::cout << version() << std::endl;
            std::cout << options << std::endl;
            exit(0);
        case COMMAND:
            invocationKind = InvocationKind::FROM_STRING;
            evalText = cmdLine.second;
            break;
        case NORC:
            userc = false;
            break;
        case EXEC:
            invocationKind = InvocationKind::BUILTIN;
            statusLogPath = nullptr;
            restIndex--;
            break;
        case STATUS_LOG:
            statusLogPath = cmdLine.second;
            break;
        case FEATURE:
            showFeature(std::cout);
            exit(0);
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

    if(invocationKind == InvocationKind::FROM_FILE && (size == 0 || strcmp(shellArgs[0], "-") == 0)) {
        invocationKind = InvocationKind::FROM_STDIN;
    }

    // execute
    switch(invocationKind) {
    case InvocationKind::FROM_FILE: {
        const char *scriptName = shellArgs[0];
        FILE *fp = fopen(scriptName, "rb");
        if(fp == NULL) {
            fprintf(stderr, "ydsh: %s: %s\n", scriptName, strerror(errno));
            exit(1);
        }

        DSState_setShellName(state, scriptName);
        DSState_setArguments(state, shellArgs + 1);
        DSState_setScriptDir(state, scriptName);
        exit(INVOKE(loadAndEval)(&state, scriptName, fp));
    }
    case InvocationKind::FROM_STDIN: {
        DSState_setArguments(state, shellArgs);

        if(isatty(STDIN_FILENO) == 0 && !forceInteractive) {  // pipe line mode
            exit(INVOKE(loadAndEval)(&state, nullptr, stdin));
        } else {    // interactive mode
            if(!quiet) {
                std::cout << version() << std::endl;
                std::cout << DSState_copyright() << std::endl;
            }
            if(userc) {
                loadRC(state, rcfile);
            }

            // ignore termination hook
            DSState_addTerminationHook(state, nullptr);

            exit(exec_interactive(state));
        }
    }
    case InvocationKind::FROM_STRING: {
        DSState_setShellName(state, shellArgs[0]);
        DSState_setArguments(state, size == 0 ? nullptr : shellArgs + 1);
        exit(INVOKE(eval)(&state, "(string)", evalText));
    }
    case InvocationKind::BUILTIN: {
        exit(DSState_exec(state, shellArgs));
    }
    }
}
