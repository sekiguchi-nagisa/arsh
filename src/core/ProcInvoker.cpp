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
#include <sys/stat.h>
#include <fcntl.h>

#include <cstdlib>

#include "../config.h"
#include "ProcInvoker.h"
#include "RuntimeContext.h"
#include "../misc/num.h"
#include "../misc/debug.h"

extern char **environ;

namespace ydsh {
namespace core {

/**
 * if cannot resolve path, return empty string and set errno.
 */
static std::string resolveFilePath(const char *fileName) {
    // get path
    const char *path = getenv("PATH");
    if(path == nullptr) {
        path = "/bin:/usr/bin:/usr/local/bin";
    }

    // resolve path
    std::string resolvedPath;
    for(unsigned int i = 0; !resolvedPath.empty() || path[i] != '\0'; i++) {
        char ch = path[i];
        bool stop = false;

        if(ch == '\0') {
            stop = true;
        } else if(ch != ':') {
            resolvedPath += ch;
            continue;
        }
        if(resolvedPath.empty()) {
            continue;
        }

        if(resolvedPath[resolvedPath.size() - 1] != '/') {
            resolvedPath += '/';
        }
        resolvedPath += fileName;

        if(resolvedPath[0] == '~') {
            resolvedPath = RuntimeContext::expandTilde(resolvedPath.c_str());
        }

        struct stat st;
        if(stat(resolvedPath.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) == S_IXUSR) {
            return resolvedPath;
        }
        resolvedPath.clear();

        if(stop) {
            break;
        }
    }

    // not found
    errno = ENOENT;
    return resolvedPath;
}

/**
 * first element of argv is file name.
 * last element of argv is null.
 * last element of envp is null.
 * if envp is null, inherit current env.
 * if progName is null, equivalent to argv[0].
 *
 * if execution success, not return.
 */
static void builtin_execvpe(char **argv, char *const *envp, const char *progName) {
    const char *fileName = argv[0];
    if(envp == nullptr) {
        envp = environ;
    }
    if(progName != nullptr) {
        argv[0] = const_cast<char *>(progName);
    }

    std::string path;
    if(strchr(fileName, '/') == nullptr) {  // resolve file path
        path = resolveFilePath(fileName);
        if(!path.empty()) {
            fileName = path.c_str();
        }
    }

#ifdef USE_DUMP_EXEC
    if(getenv("YDSH_DUMP_EXEC") != nullptr) {
        std::cerr << getpid() << " execve(" << fileName << ", [";
        for(unsigned int i = 0; argv[i] != nullptr; i++) {
            if(i > 0) {
                std::cerr << ", ";
            }
            std::cerr << argv[i];
        }
        std::cerr << "]";

        if(strcmp(getenv("YDSH_DUMP_EXEC"), "env") == 0) {
            std::cerr << ", [" << (envp == nullptr ? "null" : "");
            for(unsigned int i = 0; envp != nullptr && envp[i] != nullptr; i++) {
                if(i > 0) {
                    std::cerr << ", ";
                }
                std::cerr << envp[i];
            }
            std::cerr << "]";
        }
        std::cerr << ")" << std::endl;
    }
#endif
    execve(fileName, argv, envp);
}

/**
 * for builtin command error message.
 */
static void builtin_perror(FILE *fp, const char *prefix) {
    fprintf(fp, "%s: %s\n", prefix, strerror(errno));
}

// builtin command definition

static int builtin_help(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised);

static int builtin_cd(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    bool OLDPWD_only = true;
    const char *destDir = getenv("HOME");

    if(bctx.argc > 1) {
        destDir = bctx.argv[1];
    }
    if(destDir != nullptr && strlen(destDir) != 0) {
        OLDPWD_only = false;
        if(chdir(destDir) != 0) {
            std::string msg("-ydsh: cd: ");
            msg += destDir;
            builtin_perror(bctx.fp_stderr, msg.c_str());
            return 1;
        }
    }

    ctx->updateWorkingDir(OLDPWD_only);
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
    char *const *argv = bctx.argv;

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
        const char *arg = argv[index];
        for(unsigned int i = 0; arg[i] != '\0'; i++) {
            char ch = arg[i];
            if(ch == '\\' && arg[i + 1] != '\0') {
                switch(arg[++i]) {
                case '\\':
                    ch = '\\';
                    break;
                case 'a':
                    ch = '\a';
                    break;
                case 'b':
                    ch = '\b';
                    break;
                case 'c':   // stop printing
                    return 0;
                case 'e':
                    ch = '\033';
                    break;
                case 'f':
                    ch = '\f';
                    break;
                case 'n':
                    ch = '\n';
                    break;
                case 'r':
                    ch = '\r';
                    break;
                case 't':
                    ch = '\t';
                    break;
                case 'v':
                    ch = '\v';
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

/**
 * for stdin redirection test
 */
static int builtin___gets(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    unsigned int bufSize = 256;
    char buf[bufSize];
    int readSize;
    while((readSize = fread(buf, sizeof(char), bufSize, bctx.fp_stdin)) > 0) {
        fwrite(buf, sizeof(char), readSize, bctx.fp_stdout);
    }
    return 0;
}

/**
 * for stdout/stderr redirection test
 */
static int builtin___puts(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    for(int index = 1; index < bctx.argc; index++) {
        const char *arg = bctx.argv[index];
        if(strcmp("-1", arg) == 0 && ++index < bctx.argc) {
            fputs(bctx.argv[index], bctx.fp_stdout);
            fputc('\n', bctx.fp_stdout);
        } else if(strcmp("-2", arg) == 0 && ++index < bctx.argc) {
            fputs(bctx.argv[index], bctx.fp_stderr);
            fputc('\n', bctx.fp_stderr);
        } else {
            return 1;   // need option
        }
    }
    return 0;
}

/**
 * for prompt string debugging
 */
static int builtin_ps_intrp(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    if(bctx.argc != 2) {
        fprintf(bctx.fp_stderr, "%s: usage: %s [prompt string]\n", bctx.argv[0], bctx.argv[0]);
        return 1;
    }
    std::string str;
    ctx->interpretPromptString(bctx.argv[1], str);
    fputs(str.c_str(), bctx.fp_stdout);
    fputc('\n', bctx.fp_stdout);
    return 0;
}

static int builtin_exec(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    int index = 1;
    bool clearEnv = false;
    const char *progName = nullptr;
    for(; index < bctx.argc; index++) {
        const char *arg = bctx.argv[index];
        if(arg[0] != '-') {
            break;
        }
        if(strcmp(arg, "-c") == 0) {
            clearEnv = true;
        } else if(strcmp(arg, "-a") == 0 && ++index < bctx.argc) {
            progName = bctx.argv[index];
        } else {
            fprintf(bctx.fp_stderr, "%s: usage: %s [-c] [-a name] file [args ...]\n", bctx.argv[0], bctx.argv[0]);
            return 1;
        }
    }

    char *envp[] = {nullptr};
    if(index < bctx.argc) { // exec
        builtin_execvpe(const_cast<char **>(bctx.argv + index), clearEnv ? envp : nullptr, progName);
        fprintf(bctx.fp_stderr, "-ydsh: exec: %s: %s\n", bctx.argv[index], strerror(errno));
        return 1;
    }
    return 0;
}

static int builtin_eval(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    if(bctx.argc > 1) {
        execvp(bctx.argv[1], bctx.argv + 1);
        fprintf(bctx.fp_stderr, "-ydsh: eval: %s: %s\n", bctx.argv[1], strerror(errno));
        return 1;
    }
    return 0;
}

static int builtin_pwd(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised) {
    //TODO: support LP option

    size_t size = PATH_MAX;
    char buf[size];
    if(getcwd(buf, size) == nullptr) {
        builtin_perror(bctx.fp_stderr, "-ydsh: pwd: .");
        return 1;
    }

    fputs(buf, bctx.fp_stdout);
    fputc('\n', bctx.fp_stdout);
    return 0;
}

const struct {
    const char *commandName;
    ProcInvoker::builtin_command_t cmd_ptr;
    const char *usage;
    const char *detail;
} builtinCommands[] {
        {"__gets", builtin___gets, "",
                "    Read standard input and write to standard output."},
        {"__puts", builtin___puts, "[-1] [arg1] [-2] [arg2]",
                "    Print specified argument to standard output/error and print new line.\n"
                "    Options:\n"
                "        -1    print to standard output\n"
                "        -2    print to standard error"},
        {"cd", builtin_cd, "[dir]",
                "    Changing the current directory to DIR.  The Environment variable\n"
                "    HOME is the default DIR.  A null directory name is the same as\n"
                "    the current directory."},
        {"check_env", builtin_check_env, "[variable ...]",
                "    Check existence of specified environmental variables.\n"
                "    If all of variables are exist and not empty string, exit with 0."},
        {"echo", builtin_echo, "[-ne]",
                "    Print argument to standard output and print new line.\n"
                "    Options:\n"
                "        -n    not print new line\n"
                "        -e    interpret some escape sequence\n"
                "                  \\\\    backslash\n"
                "                  \\a    bell\n"
                "                  \\b    backspace\n"
                "                  \\c    ignore subsequent string\n"
                "                  \\e    escape sequence\n"
                "                  \\f    form feed\n"
                "                  \\n    newline\n"
                "                  \\r    carriage return\n"
                "                  \\t    horizontal tab\n"
                "                  \\v    vertical tab"},
        {"eval", builtin_eval, "[args ...]",
                "    evaluate ARGS as command."},
        {"exec", builtin_exec, "[-c] [-a name] file [args ...]",
                "    Execute FILE and replace this shell with specified program.\n"
                "    If FILE is not specified, the redirections take effect in this shell.\n"
                "    Options:\n"
                "        -c    cleaner environmental variable\n"
                "        -a    specify set program name(default is FILE)"},
        {"exit", builtin_exit, "[n]",
                "    Exit the shell with a status of N.  If N is omitted, the exit\n"
                "    status is 0."},
        {"false", builtin_false, "",
                "    Always failure (exit status is 1)."},
        {"help", builtin_help, "[-s] [pattern ...]",
                "    Display helpful information about builtin commands."},
        {"ps_intrp", builtin_ps_intrp, "[prompt string]",
                "    Interpret prompt string.\n"
                "    Escape Sequence:\n"
                "        \\a    bell\n"
                "        \\d    date\n"
                "        \\e    escape sequence\n"
                "        \\h    host name\n"
                "        \\H    fully qualified host name\n"
                "        \\n    newline\n"
                "        \\r    carriage return\n"
                "        \\s    shell name ($0)\n"
                "        \\t    24 hour notation (HH:MM:SS)\n"
                "        \\T    12 hour notation (HH:MM:SS)\n"
                "        \\@    12 hour notation with AM/PM\n"
                "        \\u    user name\n"
                "        \\v    version\n"
                "        \\V    version with patch level\n"
                "        \\w    current directory\n"
                "        \\W    base name of current directory\n"
                "        \\$    # if uid is 0, otherwise $\n"
                "        \\\\    backslash\n"
                "        \\[    begin of unprintable sequence\n"
                "        \\]    end of unprintable sequence"},
        {"pwd", builtin_pwd, "",
                "    Print the current working directiry(absolute path)."},
        {"true", builtin_true, "",
                "    Always success (exit status is 0)."},
};

template<typename T, size_t N>
static constexpr size_t sizeOfArray(const T (&array)[N]) {
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
        } else if(strcmp(arg, "-s") == 0 && i == 1) {
            isShortHelp = true;
        } else {
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
    unsigned int argOffset = this->argArray.size();
    unsigned int redirOffset = this->redirOptions.size();
    this->procOffsets.push_back(std::make_pair(argOffset, redirOffset));
}

void ProcInvoker::closeProc() {
    this->argArray.push_back(nullptr);
    this->redirOptions.push_back(std::make_pair(RedirectOP::DUMMY, nullptr));
}

void ProcInvoker::addCommandName(std::shared_ptr<DSObject> &&value) {
    this->argArray.push_back(std::move(value));
}

void ProcInvoker::addArg(std::shared_ptr<DSObject> &&value, bool skipEmptyString) {
    DSType *valueType = value->getType();
    if(*valueType == *this->ctx->getPool().getStringType()) {
        std::shared_ptr<String_Object> obj = std::dynamic_pointer_cast<String_Object>(value);
        if(skipEmptyString && obj->empty()) {
            return;
        }
        this->argArray.push_back(std::move(obj));
        return;
    }

    if(*valueType == *this->ctx->getPool().getStringArrayType()) {
        Array_Object *arrayObj = TYPE_AS(Array_Object, value);
        for(const std::shared_ptr<DSObject> &element : arrayObj->getValues()) {
            this->argArray.push_back(element);
        }
    } else {
        fatal("illegal command parameter type: %s\n", this->ctx->getPool().getTypeName(*valueType).c_str());
    }
}

void ProcInvoker::addRedirOption(RedirectOP op, std::shared_ptr<DSObject> &&value) {
    DSType *valueType = value->getType();
    if(*valueType == *this->ctx->getPool().getStringType()) {
        this->redirOptions.push_back(
                std::make_pair(op, std::dynamic_pointer_cast<String_Object>(value)));
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

/**
 * if failed, return non-zero value(errno)
 */
static int redirectToFile(const std::shared_ptr<String_Object> &fileName, const char *mode, int targetFD) {
    FILE *fp = fopen(fileName->getValue(), mode);
    if(fp == NULL) {
        return errno;
    }
    int fd = fileno(fp);
    dup2(fd, targetFD);
    fclose(fp);
    return 0;
}

/**
 * do redirection and report error.
 * if errorPipe is -1, report error and return false.
 * if errorPipe is not -1, report error and exit 1
 */
bool ProcInvoker::redirect(unsigned int procIndex, int errorPipe) {
#define CHECK_ERROR(result) do { occuredError = (result); if(occuredError != 0) { goto ERR; } } while(0)

    int occuredError = 0;

    unsigned int startIndex = this->procOffsets[procIndex].second;
    for(; this->redirOptions[startIndex].first != RedirectOP::DUMMY; startIndex++) {
        auto &pair = this->redirOptions[startIndex];
        switch(pair.first) {
        case IN_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "rb", STDIN_FILENO));
            break;
        };
        case OUT_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDOUT_FILENO));
            break;
        };
        case OUT_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDOUT_FILENO));
            break;
        };
        case ERR_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDERR_FILENO));
            break;
        };
        case ERR_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDERR_FILENO));
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDOUT_FILENO));
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDOUT_FILENO));
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
        };
        default:
            fatal("unsupported redir option: %d\n", pair.first);
        }
    }

    ERR:
    if(occuredError != 0) {
        ChildError e;
        e.redirIndex = startIndex;
        e.errorNum = occuredError;

        if(errorPipe == -1) {
            return this->checkChildError(std::make_pair(0, e)); // return false
        }
        write(errorPipe, &e, sizeof(ChildError));
        exit(0);
    }
    return true;

#undef CHECK_ERROR
}

/**
 * open file and get file descriptor.
 * write opend file pointer to openedFps.
 * if cannot open file, return errno.
 */
static int redirectToFile(const std::shared_ptr<String_Object> &fileName,
                          const char *mode, FILE **targetFp, std::vector<FILE *> &openedFps) {
    FILE *fp = fopen(fileName->getValue(), mode);
    if(fp == nullptr) {
        return errno;
    }
    openedFps.push_back(fp);
    *targetFp = fp;
    return 0;
}

bool ProcInvoker::redirectBuiltin(std::vector<FILE *> &openedFps, BuiltinContext &bctx) {
#define REDIRECT_TO(name, mode, targetFD) \
    do { occuredError = redirectToFile(name, mode, &targetFD, openedFps); if(occuredError != 0) { goto ERR; } } while(false)

    int occuredError = 0;
    unsigned int startIndex = this->procOffsets[0].second;  // procIndex must be 0
    for(; this->redirOptions[startIndex].first != RedirectOP::DUMMY; startIndex++) {
        auto &pair = this->redirOptions[startIndex];
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
        };
        default:
            fatal("unsupported redir option: %d\n", pair.first);
        }
    }

    ERR:
    if(occuredError != 0) {
        ChildError e;

        e.redirIndex = startIndex;
        e.errorNum = occuredError;
        return this->checkChildError(std::make_pair(0, e)); // return false
    }

    return true;

#undef REDIRECT_TO
}

ProcInvoker::builtin_command_t ProcInvoker::lookupBuiltinCommand(const char *commandName) {
    auto iter = this->builtinMap.find(commandName);
    if(iter == this->builtinMap.end()) {
        return nullptr;
    }
    return iter->second;
}

std::shared_ptr<DSObject> *ProcInvoker::getARGV(unsigned int procIndex) {
    return this->argArray.data() + this->procOffsets[procIndex].first;
}

EvalStatus ProcInvoker::invoke() {
    const unsigned int procSize = this->procOffsets.size();

    // check builtin command
    if(procSize == 1) {
        builtin_command_t cmd_ptr = this->lookupBuiltinCommand(this->getCommandName(0));
        if(cmd_ptr != nullptr && cmd_ptr != builtin_eval) {
            std::shared_ptr<DSObject> *ptr = this->getARGV(0);
            unsigned int argc = 1;
            for(; ptr[argc] != nullptr; argc++);
            char *argv[argc + 1];
            for(unsigned int i = 0; i < argc; i++) {
                argv[i] = const_cast<char *>(TYPE_AS(String_Object, ptr[i])->getValue());
            }
            argv[argc] = nullptr;

            BuiltinContext bctx(argc, argv);
            bool raised = false;

            // for builtin-exec
            if(strcmp(bctx.argv[0], "exec") == 0) {
                if(!this->redirect(0, -1)) {
                    this->ctx->updateExitStatus(1);
                    return EvalStatus::THROW;
                }
                this->ctx->updateExitStatus(cmd_ptr(this->ctx, bctx, raised));
                return EvalStatus::SUCCESS;
            }

            // for other builtin command
            std::vector<FILE *> fps;
            if(!this->redirectBuiltin(fps, bctx)) {
                this->ctx->updateExitStatus(1);
                raised = true;
            } else {
                this->ctx->updateExitStatus(cmd_ptr(this->ctx, bctx, raised));
            }

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

    pid_t pid[procSize];
    int pipefds[procSize][2];
    int selfpipes[procSize][2];
    for(unsigned int i = 0; i < procSize; i++) {
        if(pipe(pipefds[i]) < 0) {  // create pipe
            perror("pipe creation error");
            exit(1);
        }
        if(pipe(selfpipes[i]) < 0) {    // create self-pipe for error reporting
            perror("pipe creation error");
            exit(1);
        }
        if(fcntl(selfpipes[i][WRITE_PIPE], F_SETFD, fcntl(selfpipes[i][WRITE_PIPE], F_GETFD) | FD_CLOEXEC)) {
            perror("fcntl error");
            exit(1);
        }
    }

    // fork
    std::pair<unsigned int, ChildError> errorPair;
    unsigned int procIndex;
    for(procIndex = 0; procIndex < procSize && (pid[procIndex] = fork()) > 0; procIndex++) {
        this->procCtxs.push_back(ProcInvoker::ProcContext(pid[procIndex]));

        // check error via self-pipe
        int readSize;
        ChildError childError;
        close(selfpipes[procIndex][WRITE_PIPE]);
        while((readSize = read(selfpipes[procIndex][READ_PIPE], &childError, sizeof(childError))) == -1) {
            if(errno != EAGAIN && errno != EINTR) {
                break;
            }
        }
        if(readSize > 0 && !childError) {   // if error happened, stop forking.
            errorPair.first = procIndex;
            errorPair.second = childError;

            procIndex = procSize;
            break;
        }
    }

    if(procIndex == procSize) {   // parent process
        // close unused pipe
        closeAllPipe(procSize, pipefds);
        closeAllPipe(procSize, selfpipes);

        // wait for exit
        const unsigned int actualProcSize = this->procCtxs.size();
        for(unsigned int i = 0; i < actualProcSize; i++) {
            int status;
            waitpid(pid[i], &status, 0);
            if(WIFEXITED(status)) {
                this->procCtxs[i].set(ExitKind::NORMAL, WEXITSTATUS(status));
            }
            if(WIFSIGNALED(status)) {
                this->procCtxs[i].set(ExitKind::INTR, WTERMSIG(status));
            }
        }

        this->ctx->updateExitStatus(this->procCtxs[actualProcSize - 1].exitStatus);
        return this->checkChildError(errorPair) ? EvalStatus::SUCCESS : EvalStatus::THROW ;
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

        this->redirect(procIndex, selfpipes[procIndex][WRITE_PIPE]);

        closeAllPipe(procSize, pipefds);

        // create argv
        std::shared_ptr<DSObject> *ptr = this->getARGV(procIndex);
        unsigned int argc = 1;
        for(; ptr[argc] != nullptr; argc++);
        char *argv[argc + 1];
        for(unsigned int i = 0; i < argc; i++) {
            argv[i] = const_cast<char *>(TYPE_AS(String_Object, ptr[i])->getValue());
        }
        argv[argc] = nullptr;

        // check builtin
        builtin_command_t cmd_ptr = this->lookupBuiltinCommand(argv[0]);
        if(cmd_ptr != nullptr) {
            closeAllPipe(procSize, selfpipes);

            BuiltinContext bctx(argc, argv);
            bool raised = false;
            exit(cmd_ptr(this->ctx, bctx, raised));
        }

        builtin_execvpe(argv, nullptr, nullptr);

        ChildError e;
        e.errorNum = errno;

        write(selfpipes[procIndex][WRITE_PIPE], &e, sizeof(ChildError));
        exit(1);
    } else {
        perror("child process error");
        exit(1);
    }
}

EvalStatus ProcInvoker::execBuiltinCommand(char *const argv[]) {
    builtin_command_t cmd_ptr = this->lookupBuiltinCommand(argv[0]);
    if(cmd_ptr == nullptr) {
        fprintf(stderr, "ydsh: %s: not builtin command\n", argv[0]);
        this->ctx->updateExitStatus(1);
        return EvalStatus::SUCCESS;
    }

    BuiltinContext bctx(argv);
    bool raised = false;
    this->ctx->updateExitStatus(cmd_ptr(this->ctx, bctx, raised));


    // flush standard stream to prevent buffering.
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);

    return raised ? EvalStatus::THROW : EvalStatus::SUCCESS;
}

const char *ProcInvoker::getCommandName(unsigned int procIndex) {
    return TYPE_AS(String_Object, this->getARGV(procIndex)[0])->getValue();
}

bool ProcInvoker::checkChildError(const std::pair<unsigned int, ChildError> &errorPair) {
    if(!errorPair.second) {
        auto &pair = this->redirOptions[errorPair.second.redirIndex];

        std::string msg;
        if(pair.first == RedirectOP::DUMMY) {  // execution error
            msg += "execution error: ";
            msg += this->getCommandName(errorPair.first);
            msg += ": ";
        } else {    // redirection error
            msg += "io redirection error: ";
            if(pair.second != nullptr && pair.second->size() != 0) {
                msg += pair.second->getValue();
                msg += ": ";
            }
        }
        msg += strerror(errorPair.second.errorNum);
        this->ctx->throwError(this->ctx->getPool().getErrorType(), std::move(msg));
        return false;
    }
    return true;
}


} // namespace core
} // namespace ydsh