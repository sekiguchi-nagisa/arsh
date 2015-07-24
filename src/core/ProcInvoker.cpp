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
#include <sys/types.h>
#include <sys/wait.h>

#include <cstdlib>

#include "ProcInvoker.h"
#include "RuntimeContext.h"
#include "../misc/num.h"
#include "../misc/debug.h"

namespace ydsh {
namespace core {

/**
 * for builtin command error message.
 */
static void builtin_perror(FILE *fp, const char *prefix) {
    fprintf(fp, "%s: %s\n", prefix, strerror(errno));
}

// builtin command definition

static int builtin_help(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised);

static int builtin_cd(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    const char *destDir = getenv("HOME");
    if(bctx.argc > 1) {
        destDir = bctx.argv[1];
    }
    if(chdir(destDir) != 0) {
        std::string msg("-ydsh: cd: ");
        msg += destDir;
        builtin_perror(bctx.fp_stderr, msg.c_str());
        return 1;
    }

    ctx->updateWorkingDir();
    return 0;
}

static int builtin_check_env(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    if(bctx.argc == 1) {
        fprintf(bctx.fp_stderr, "%s: usage: %s [variable ...]\n", bctx.argv[0], bctx.argv[0]);
        return 1;
    }
    for(int i = 1; i < bctx.argc; i++) {
        const char *env = getenv(bctx.argv[i]);
        if(env == nullptr || strlen(env) == 0) {
            return 1;
        }
    }
    return 0;
}

static int builtin_exit(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    int ret = 0;
    if(bctx.argc > 1) {
        const char *num = bctx.argv[1];
        int status;
        long value = convertToInt64(num, status, false);
        if(status == 0) {
            ret = value;
        }
    }
    ctx->exitShell(ret);
    raised = true;
    return ret;
}

static int builtin_echo(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    FILE *fp = bctx.fp_stdout;  // not close it.
    int argc = bctx.argc;
    char **argv = bctx.argv;

    bool newline = true;
    bool interpEscape = false;
    int index = 1;

    // parse option
    for(; index < argc; index++) {
        if(strcmp(argv[index], "-n") == 0) {
            newline = false;
        } else if(strcmp(argv[index], "-e") == 0) {
            interpEscape = true;
        } else {
            break;
        }
    }

    // print argument
    bool firstArg = true;
    for(; index < argc; index++) {
        if(firstArg) {
            firstArg = false;
        } else {
            fputc(' ', fp);
        }
        if(!interpEscape) {
            fputs(argv[index], fp);
            continue;
        }
        char *arg = argv[index];
        for(unsigned int i = 0; arg[i] != '\0'; i++) {
            char ch = arg[i];
            if(ch == '\\' && arg[i + 1] != '\0') {
                switch(arg[++i]) {
                case 'a':
                    ch = '\a';
                    break;
                case 'c':   // stop printing
                    return 0;
                case 'v':
                    ch = '\v';
                    break;
                case 'e':
                    ch = '\033';
                    break;
                default:
                    i--;
                    break;
                }
            }
            fputc(ch, fp);
        }
    }

    if(newline) {
        fputc('\n', fp);
    }
    return 0;
}

static int builtin_true(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    return 0;
}

static int builtin_false(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    return 1;
}

const struct {
    const char *commandName;
    ProcInvoker::builtin_command_t cmd_ptr;
    const char *usage;
    const char *detail;
} builtinCommands[] {
        {"cd", builtin_cd, "[dir]",
                "    Changing the current directory to DIR. The Environment variable\n"
                "    HOME is the default DIR.  A null directory name is the same as\n"
                "    the current directory."},
        {"check_env", builtin_check_env, "[variable ...]",
                "    Check existence of specified environmental variables.\n"
                "    If all of variables are exist and not empty string, exit with 0."},
        {"echo", builtin_echo, "[-ne]",
                "    print argument to standard output and print new line.\n"
                "    Options:\n"
                "        -n    not print new line\n"
                "        -e    interpret some escape sequence\n"
                "                  \\a    bell\n"
                "                  \\c    ignore subsequent string\n"
                "                  \\e    escape\n"
                "                  \\v    vertical tab"},
        {"exit", builtin_exit, "[n]",
                "    Exit the shell with a status of N.  If N is omitted, the exit\n"
                "    status is 0."},
        {"help", builtin_help, "[-s] [pattern ...]",
                "    Display helpful information about builtin commands."},
        {"true", builtin_true, "",
                "    always success (exit status is 0)."},
        {"false", builtin_false, "",
                "    always failure (exit status is 1)."},
};

template<typename T, size_t N>
static size_t sizeOfArray(const T (&array)[N]) {
    return N;
}


static void printAllUsage(FILE *fp) {
    unsigned int size = sizeOfArray(builtinCommands);
    for(unsigned int i = 0; i < size; i++) {
        fprintf(fp, "%s %s\n", builtinCommands[i].commandName, builtinCommands[i].usage);
    }
}

static int builtin_help(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    if(bctx.argc == 1) {
        printAllUsage(bctx.fp_stdout);
        return 0;
    }
    bool isShortHelp = false;
    bool foundValidCommand = false;
    for(int i = 1; i < bctx.argc; i++) {
        const char *arg = bctx.argv[i];
        if(strcmp(arg, "-s") == 0 && bctx.argc == 2) {
            printAllUsage(bctx.fp_stdout);
            foundValidCommand = true;
        }
        else if(strcmp(arg, "-s") == 0 && i == 1) {
            isShortHelp = true;
        }
        else {
            unsigned int size = sizeOfArray(builtinCommands);
            for(unsigned int j = 0; j < size; j++) {
                if(strcmp(arg, builtinCommands[j].commandName) == 0) {
                    foundValidCommand = true;
                    fprintf(bctx.fp_stdout, "%s: %s %s\n", arg, arg, builtinCommands[j].usage);
                    if(!isShortHelp) {
                        fprintf(bctx.fp_stdout, "%s\n", builtinCommands[j].detail);
                    }
                    break;
                }
            }
        }
    }
    if(!foundValidCommand) {
        fprintf(bctx.fp_stderr,
                "-ydsh: help: no help topics match `%s'.  Try `help help'.\n", bctx.argv[bctx.argc - 1]);
        return 1;
    }
    return 0;
}


// #########################
// ##     ProcInvoker     ##
// #########################

ProcInvoker::ProcInvoker(RuntimeContext *ctx) :
        ctx(ctx), builtinMap(), argArray(), redirOptions(), procOffsets(), procCtxs() {
    // register builtin command
    unsigned int size = sizeOfArray(builtinCommands);
    for(unsigned int i = 0; i < size; i++) {
        this->builtinMap.insert(std::make_pair(builtinCommands[i].commandName, builtinCommands[i].cmd_ptr));
    }
}

void ProcInvoker::openProc() {
    unsigned int argOffset = this->argArray.getUsedSize();
    unsigned int redirOffset = this->redirOptions.size();
    this->procOffsets.push_back(std::make_pair(argOffset, redirOffset));
}

void ProcInvoker::closeProc() {
    this->argArray.append(nullptr);
    this->redirOptions.push_back(std::make_pair(RedirectOP::DUMMY, nullptr));
}

void ProcInvoker::addCommandName(const std::string &name) {
    this->argArray.append(name.c_str());
}

void ProcInvoker::addArg(const std::shared_ptr<DSObject> &value, bool skipEmptyString) {
    DSType *valueType = value->getType();
    if(*valueType == *this->ctx->getPool().getStringType()) {
        std::shared_ptr<String_Object> obj = std::dynamic_pointer_cast<String_Object>(value);
        if(skipEmptyString && obj->getValue().empty()) {
            return;
        }
        this->argArray.append(obj->getValue().c_str());
        return;
    }

    if(*valueType == *this->ctx->getPool().getStringArrayType()) {
        Array_Object *arrayObj = TYPE_AS(Array_Object, value);
        for(const std::shared_ptr<DSObject> &element : arrayObj->getValues()) {
            this->argArray.append(std::dynamic_pointer_cast<String_Object>(element)->getValue().c_str());
        }
    } else {
        fatal("illegal command parameter type: %s\n", this->ctx->getPool().getTypeName(*valueType).c_str());
    }
}

void ProcInvoker::addRedirOption(RedirectOP op, const std::shared_ptr<DSObject> &value) {
    DSType *valueType = value->getType();
    if(*valueType == *this->ctx->getPool().getStringType()) {
        this->redirOptions.push_back(
                std::make_pair(op, std::dynamic_pointer_cast<String_Object>(value)->getValue().c_str()));
    } else {
        fatal("illegal command parameter type: %s\n", this->ctx->getPool().getTypeName(*valueType).c_str());
    }
}

static void closeAllPipe(int size, int pipefds[][2]) {
    for(int i = 0; i < size; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }
}

static void redirectToFile(const char *fileName, const char *mode, int targetFD) {
    FILE *fp = fopen(fileName, mode);
    if(fp != NULL) {
        int fd = fileno(fp);
        dup2(fd, targetFD);
        fclose(fp);
    } else {
        fprintf(stderr, "-ydsh: %s: ", fileName);
        perror("");
        exit(1);
    }
}

/**
 * if redirection failed, exit 1
 */
void ProcInvoker::redirect(unsigned int procIndex) {  //FIXME: error reporting
    unsigned int startIndex = this->procOffsets[procIndex].second;
    for(; this->redirOptions[startIndex].first != RedirectOP::DUMMY; startIndex++) {
        const std::pair<RedirectOP, const char *> &pair = this->redirOptions[startIndex];
        switch(pair.first) {
        case IN_2_FILE: {
            redirectToFile(pair.second, "rb", STDIN_FILENO);
            break;
        };
        case OUT_2_FILE: {
            redirectToFile(pair.second, "wb", STDOUT_FILENO);
            break;
        };
        case OUT_2_FILE_APPEND: {
            redirectToFile(pair.second, "ab", STDOUT_FILENO);
            break;
        };
        case ERR_2_FILE: {
            redirectToFile(pair.second, "wb", STDERR_FILENO);
            break;
        };
        case ERR_2_FILE_APPEND: {
            redirectToFile(pair.second, "ab", STDERR_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE: {
            redirectToFile(pair.second, "wb", STDOUT_FILENO);
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE_APPEND: {
            redirectToFile(pair.second, "ab", STDOUT_FILENO);
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT: {
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        };
        case MERGE_OUT_2_ERR: {
            dup2(STDERR_FILENO, STDOUT_FILENO);
            break;
        }
        default:
            fatal("unsupported redir option: %d\n", pair.first);
        }
    }
}

/**
 * open file and get file descriptor.
 * write opend file pointer to openedFps.
 * if cannot open file, return -1.
 */
static bool redirectToFile(const char *fileName, const char *mode, FILE **targetFp, std::vector<FILE *> &openedFps) {
    FILE *fp = fopen(fileName, mode);
    if(fp == nullptr) {
        fprintf(stderr, "-ydsh: %s: ", fileName);
        perror("");
        return false;   //FIXME: error reporting
    }
    openedFps.push_back(fp);
    *targetFp = fp;
    return true;
}

#define REDIRECT_TO(name, mode, targetFD) \
    do { if(!redirectToFile(name, mode, &targetFD, openedFps)) { return false; }} while(false)

bool ProcInvoker::redirectBuiltin(unsigned int procIndex, std::vector<FILE *> &openedFps, BuiltinContext &bctx) {
    unsigned int startIndex = this->procOffsets[procIndex].second;
    for(; this->redirOptions[startIndex].first != RedirectOP::DUMMY; startIndex++) {
        const std::pair<RedirectOP, const char *> &pair = this->redirOptions[startIndex];
        switch(pair.first) {
        case IN_2_FILE: {
            REDIRECT_TO(pair.second, "rb", bctx.fp_stdin);
            break;
        };
        case OUT_2_FILE: {
            REDIRECT_TO(pair.second, "wb", bctx.fp_stdout);
            break;
        };
        case OUT_2_FILE_APPEND: {
            REDIRECT_TO(pair.second, "ab", bctx.fp_stdout);
            break;
        };
        case ERR_2_FILE: {
            REDIRECT_TO(pair.second, "wb", bctx.fp_stderr);
            break;
        };
        case ERR_2_FILE_APPEND: {
            REDIRECT_TO(pair.second, "ab", bctx.fp_stderr);
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE: {
            REDIRECT_TO(pair.second, "wb", bctx.fp_stdout);
            bctx.fp_stderr = bctx.fp_stdout;
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE_APPEND: {
            REDIRECT_TO(pair.second, "ab", bctx.fp_stdout);
            bctx.fp_stderr = bctx.fp_stdout;
            break;
        };
        case MERGE_ERR_2_OUT: {
            bctx.fp_stderr = bctx.fp_stdout;
            break;
        };
        case MERGE_OUT_2_ERR: {
            bctx.fp_stdout = bctx.fp_stderr;
            break;
        }
        default:
            fatal("unsupported redir option: %d\n", pair.first);
        }
    }
    return true;
}

ProcInvoker::builtin_command_t ProcInvoker::lookupBuiltinCommand(const char *commandName) {
    auto iter = this->builtinMap.find(commandName);
    if(iter == this->builtinMap.end()) {
        return nullptr;
    }
    return iter->second;
}

EvalStatus ProcInvoker::invoke() {
    const unsigned int procSize = this->procOffsets.size();

    // check builtin command
    if(procSize == 1) {
        char **argv = this->argArray.getRawData(this->procOffsets[0].first);
        builtin_command_t cmd_ptr = this->lookupBuiltinCommand(argv[0]);
        if(cmd_ptr != nullptr) {
            BuiltinContext bctx = {
                    .argc = 1, .argv = argv,
                    .fp_stdin = stdin, .fp_stdout = stdout, .fp_stderr = stderr
            };
            for(; argv[bctx.argc] != nullptr; bctx.argc++);

            std::vector<FILE *> fps;
            if(!this->redirectBuiltin(0, fps, bctx)) {
                this->ctx->updateExitStatus(1);
                return EvalStatus::SUCCESS;
            }

            // invoke
            bool raised = false;
            this->ctx->updateExitStatus(cmd_ptr(this->ctx, bctx, raised));

            for(FILE *fp : fps) {
                fclose(fp);
            }

            // flush standard stream to prevent buffering.
            fflush(stdin);
            fflush(stdout);
            fflush(stderr);

            return raised ? EvalStatus::THROW : EvalStatus::SUCCESS;
        }
    }

    // create pipe
    pid_t pid[procSize];
    int pipefds[procSize][2];
    for(unsigned int i = 0; i < procSize; i++) {
        if(pipe(pipefds[i]) < 0) {
            perror("pipe creation error");
            exit(1);
        }
    }

    // fork
    unsigned int procIndex;
    for(procIndex = 0; procIndex < procSize && (pid[procIndex] = fork()) > 0; procIndex++) {
        this->procCtxs.push_back(ProcInvoker::ProcContext(pid[procIndex]));
    }

    if(procIndex == procSize) {   // parent process
        closeAllPipe(procSize - 1, pipefds);
        close(pipefds[procSize - 1][WRITE_PIPE]);
        close(pipefds[procSize - 1][READ_PIPE]);

        // wait for exit
        for(unsigned int i = 0; i < procSize; i++) {
            int status;
            waitpid(pid[i], &status, 0);
            if(WIFEXITED(status)) {
                this->procCtxs[i].set(ExitKind::NORMAL, WEXITSTATUS(status));
            }
            if(WIFSIGNALED(status)) {
                this->procCtxs[i].set(ExitKind::INTR, WTERMSIG(status));
            }
        }
        ctx->updateExitStatus(this->procCtxs[procSize - 1].exitStatus);
        return EvalStatus::SUCCESS;
    } else if(pid[procIndex] == 0) { // child process
        if(procIndex == 0) {    // first process
            if(procSize > 1) {
                dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
            }
        }
        if(procIndex > 0 && procIndex < procSize - 1) {   // other process.
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == procSize - 1) { // last proc
            if(procSize > 1) {
                dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            }
        }

        this->redirect(procIndex);

        closeAllPipe(procSize, pipefds);

        char **argv = this->argArray.getRawData(this->procOffsets[procIndex].first);

        // check builtin
        builtin_command_t cmd_ptr = this->lookupBuiltinCommand(argv[0]);
        if(cmd_ptr != nullptr) {
            BuiltinContext bctx = {
                    .argc = 1, .argv = argv,
                    .fp_stdin = stdin, .fp_stdout = stdout, .fp_stderr = stderr
            };
            for(; argv[bctx.argc] != nullptr; bctx.argc++);
            bool raised = false;
            exit(cmd_ptr(this->ctx, bctx, raised));
        }

        execvp(argv[0], argv);
        perror("execution error");
        fprintf(stderr, "executed cmd: %s\n", argv[0]);
        exit(1);
    } else {
        perror("child process error");
        exit(1);
    }
}


} // namespace core
} // namespace ydsh