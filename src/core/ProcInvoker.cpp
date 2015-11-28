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

#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <cstdlib>
#include <cstdarg>
#include <unordered_map>

#include "logger.h"
#include "ProcInvoker.h"
#include "RuntimeContext.h"
#include "symbol.h"
#include "../misc/num.h"

extern char **environ;

namespace ydsh {
namespace core {

/**
 * if cannot resolve path, return empty string and set errno.
 */
static std::string resolveFilePath(const char *fileName, bool useDefaultPath) {
    // get path
    const char *path = getenv("PATH");
    if(path == nullptr || useDefaultPath) {
        path = VAR_DEFAULT_PATH;
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
            resolvedPath = expandTilde(resolvedPath.c_str());
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
 * first element of argv is executing file name.
 * last element of argv is null.
 * last element of envp is null.
 * if envp is null, inherit current env.
 * if progName is null, equivalent to argv[0].
 *
 * if execution success, not return.
 */
static void builtin_execvpe(char **argv, char *const *envp, const char *progName, bool useDefaultPath = false) {
    const char *fileName = argv[0];
    if(progName != nullptr) {
        argv[0] = const_cast<char *>(progName);
    }

    // resolve file path
    std::string path;
    if(strchr(fileName, '/') == nullptr) {
        path = resolveFilePath(fileName, useDefaultPath);
        if(!path.empty()) {
            fileName = path.c_str();
        }
    }

    // set env
    setenv("_", fileName, 1);
    if(envp == nullptr) {
        envp = environ;
    }


    LOG_L(DUMP_EXEC, [&](std::ostream &stream) {
        stream << "execve(" << fileName << ", [";
        for(unsigned int i = 0; argv[i] != nullptr; i++) {
            if(i > 0) {
                stream << ", ";
            }
            stream << argv[i];
        }
        stream << "]" << ")";
    });

    // reset signal setting
    struct sigaction ignore_act;
    ignore_act.sa_handler = SIG_DFL;
    ignore_act.sa_flags = 0;
    sigemptyset(&ignore_act.sa_mask);

    sigaction(SIGINT, &ignore_act, NULL);
    sigaction(SIGQUIT, &ignore_act, NULL);
    sigaction(SIGSTOP, &ignore_act, NULL);
    sigaction(SIGCONT, &ignore_act, NULL);
    sigaction(SIGTSTP, &ignore_act, NULL);

    // exev
    execve(fileName, argv, envp);
}

/**
 * if errorNum is not 0, include strerror(errorNum)
 */
static void builtin_perror(const BuiltinContext &bctx, int errorNum, const char *fmt, ...) {
    FILE *fp = bctx.fp_stderr;

    fprintf(fp, "-ydsh: %s", bctx.argv[0]);

    if(strcmp(fmt, "") != 0) {
        fputs(": ", fp);

        va_list arg;
        va_start(arg, fmt);

        vfprintf(fp, fmt, arg);

        va_end(arg);
    }

    if(errorNum != 0) {
        fprintf(fp, ": %s", strerror(errorNum));
    }
    fputc('\n', fp);
}

#define PERROR(bctx, fmt, ...) builtin_perror(bctx, errno, fmt, ## __VA_ARGS__ )


// builtin command definition
static int builtin___gets(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin___puts(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_cd(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_check_env(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_command(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_echo(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_eval(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_exec(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_exit(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_false(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_help(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_ps_intrp(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_pwd(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_test(RuntimeContext *ctx, const BuiltinContext &bctx);
static int builtin_true(RuntimeContext *ctx, const BuiltinContext &bctx);

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
        {"command", builtin_command, "[-pVv] command [arg ...]",
                "    Execute COMMAND with ARGS excepting user defined command.\n"
                        "    If -p option is specified, search command from default PATH.\n"
                        "    If -V or -v option are specifed, print description of COMMAND.\n"
                        "    -V option shows more detailed information."},
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
                "        \\s    base name of $0\n"
                "        \\t    24 hour notation (HH:MM:SS)\n"
                "        \\T    12 hour notation (HH:MM:SS)\n"
                "        \\@    12 hour notation with AM/PM\n"
                "        \\u    user name\n"
                "        \\v    version\n"
                "        \\V    version with patch level\n"
                "        \\w    current directory\n"
                "        \\W    base name of current directory($HOME is replaced by tilde)\n"
                "        \\$    # if uid is 0, otherwise $\n"
                "        \\\\    backslash\n"
                "        \\[    begin of unprintable sequence\n"
                "        \\]    end of unprintable sequence"},
        {"pwd", builtin_pwd, "",
                "    Print the current working directiry(absolute path)."},
        {"true", builtin_true, "",
                "    Always success (exit status is 0)."},
        {"test", builtin_test, "[expr]",
                "    Unary or Binary expressions.\n"
                "    If expression is true, return 0\n"
                "    If expression is false, return 1\n"
                "    If operand or operator is invalid, return 2\n"
                "    String operators:\n"
                "        -z STRING      check if string is empty\n"
                "        -n STRING\n"
                "        STRING         check if string is not empty\n"
                "        STRING1 = STRING2\n"
                "        STRING1 == STRING2\n"
                "                       check if strings are equal\n"
                "        STRING1 != STRING2\n"
                "                       check if strings are not equal\n"
                "        STRING1 < STRING2\n"
                "                       check if STRING1 is less than STRING2 with dictionary order\n"
                "        STRING1 > STRING2\n"
                "                       check if STRING2 is greater than STRING2 with dictionary order\n"
                "    Integer operators:\n"
                "        INT1 -eq INT2  check if integers are equal\n"
                "        INT1 -ne INT2  check if integers are not equal\n"
                "        INT1 -lt INT2  check if INT1 is less than INT2\n"
                "        INT1 -gt INT2  check if INT1 is greater than INT2\n"
                "        INT1 -le INT2  check if INT1 is less than or equal to INT2\n"
                "        INT1 -ge INT2  check if INT1 is greater than or equal to INT2\n"
                "\n"
                "    Integer value is signed int 64."},
};

template<typename T, size_t N>
static constexpr size_t sizeOfArray(const T (&)[N]) {
    return N;
}


static void printAllUsage(FILE *fp) {
    unsigned int size = sizeOfArray(builtinCommands);
    for(unsigned int i = 0; i < size; i++) {
        fprintf(fp, "%s %s\n", builtinCommands[i].commandName, builtinCommands[i].usage);
    }
}

/**
 * if not found command, return false.
 */
static bool printUsage(FILE *fp, const char *commandName, bool isShortHelp = true) {
    unsigned int size = sizeOfArray(builtinCommands);
    for(unsigned int i = 0; i < size; i++) {
        if(strcmp(commandName, builtinCommands[i].commandName) == 0) {
            fprintf(fp, "%s: %s %s\n", commandName, commandName, builtinCommands[i].usage);
            if(!isShortHelp) {
                fprintf(fp, "%s\n", builtinCommands[i].detail);
            }
            return true;
        }
    }
    return false;
}

static int builtin_help(RuntimeContext *, const BuiltinContext &bctx) {
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
            if(printUsage(bctx.fp_stdout, arg, isShortHelp)) {
                foundValidCommand = true;
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

inline static void showUsage(const BuiltinContext &bctx) {
    printUsage(bctx.fp_stderr, bctx.argv[0]);
}

static int builtin_cd(RuntimeContext *ctx, const BuiltinContext &bctx) {
    bool OLDPWD_only = true;
    const char *destDir = getenv("HOME");

    if(bctx.argc > 1) {
        destDir = bctx.argv[1];
    }
    if(destDir != nullptr && strlen(destDir) != 0) {
        OLDPWD_only = false;
        if(chdir(destDir) != 0) {
            PERROR(bctx, "%s", destDir);
            return 1;
        }
    }

    ctx->updateWorkingDir(OLDPWD_only);
    return 0;
}

static int builtin_check_env(RuntimeContext *, const BuiltinContext &bctx) {
    if(bctx.argc == 1) {
        showUsage(bctx);
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

static int builtin_exit(RuntimeContext *ctx, const BuiltinContext &bctx) {
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
    return ret;
}

static int builtin_echo(RuntimeContext *, const BuiltinContext &bctx) {
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

static int builtin_true(RuntimeContext *, const BuiltinContext &) {
    return 0;
}

static int builtin_false(RuntimeContext *, const BuiltinContext &) {
    return 1;
}

/**
 * for stdin redirection test
 */
static int builtin___gets(RuntimeContext *, const BuiltinContext &bctx) {
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
static int builtin___puts(RuntimeContext *, const BuiltinContext &bctx) {
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
static int builtin_ps_intrp(RuntimeContext *ctx, const BuiltinContext &bctx) {
    if(bctx.argc != 2) {
        showUsage(bctx);
        return 1;
    }
    std::string str;
    ctx->interpretPromptString(bctx.argv[1], str);
    fputs(str.c_str(), bctx.fp_stdout);
    fputc('\n', bctx.fp_stdout);
    return 0;
}

static int builtin_exec(RuntimeContext *, const BuiltinContext &bctx) {
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
            showUsage(bctx);
            return 1;
        }
    }

    char *envp[] = {nullptr};
    if(index < bctx.argc) { // exec
        const char *old = getenv("_");  // save current _
        builtin_execvpe(const_cast<char **>(bctx.argv + index), clearEnv ? envp : nullptr, progName);
        PERROR(bctx, "%s", bctx.argv[index]);
        if(old != nullptr) {    // restore
            setenv("_", old, 1);
        }
        return 1;
    }
    return 0;
}

static int builtin_eval(RuntimeContext *ctx, const BuiltinContext &bctx) {
    if(bctx.argc > 1) {
        pid_t pid = xfork();
        if(pid == -1) {
            perror("child process error");
            exit(1);
        } else if(pid == 0) {   // child
            // replace standard stream to bctx
            dup2(fileno(bctx.fp_stdin), STDIN_FILENO);
            dup2(fileno(bctx.fp_stdout), STDOUT_FILENO);
            dup2(fileno(bctx.fp_stderr), STDERR_FILENO);

            // exec user-defined command
            UserDefinedCmdNode *udcNode = ctx->lookupUserDefinedCommand(bctx.argv[1]);
            if(udcNode != nullptr) {
                // prepare arguments
                const unsigned int size = bctx.argc;
                DSValue *argv = new DSValue[size];
                for(int i = 1; i < bctx.argc; i++) {
                    argv[i - 1] = DSValue::create<String_Object>(
                            ctx->getPool().getStringType(), std::string(bctx.argv[i])
                    );
                }
                argv[size - 1] = nullptr;

                int r = ctx->execUserDefinedCommand(udcNode, argv);
                delete[] argv;
                exit(r);
            }

            // exec external command
            builtin_execvpe(const_cast<char **>(bctx.argv + 1), nullptr, nullptr, false);
            BuiltinContext nbctx(1, bctx);
            PERROR(nbctx, "");
            exit(1);
        } else {    // parent process
            int status;
            ctx->xwaitpid(pid, status, 0);
            if(WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            if(WIFSIGNALED(status)) {
                return WTERMSIG(status);
            }
        }
    }
    return 0;
}

static int builtin_pwd(RuntimeContext *, const BuiltinContext &bctx) {
    //TODO: support LP option

    size_t size = PATH_MAX;
    char buf[size];
    if(getcwd(buf, size) == nullptr) {
        PERROR(bctx, ".");
        return 1;
    }

    fputs(buf, bctx.fp_stdout);
    fputc('\n', bctx.fp_stdout);
    return 0;
}

static int builtin_command(RuntimeContext *ctx, const BuiltinContext &bctx) {
    int index = 1;
    bool useDefaultPath = false;

    /**
     * if 0, ignore
     * if 1, show description
     * if 2, show detailed description
     */
    unsigned char showDesc = 0;

    for(; index < bctx.argc; index++) {
        const char *arg = bctx.argv[index];
        if(arg[0] != '-') {
            break;
        } else if(strcmp(arg, "-p") == 0) {
            useDefaultPath = true;
        } else if(strcmp(arg, "-v") == 0) {
            showDesc = 1;
        } else if(strcmp(arg, "-V") == 0) {
            showDesc = 2;
        } else {
            builtin_perror(bctx, 0, "%s: invalid option", arg);
            showUsage(bctx);
            return 1;
        }
    }

    if(index < bctx.argc) {
        auto &invoker = ctx->getProcInvoker();
        if(showDesc == 0) { // execute command
            BuiltinContext nbctx(index, bctx);
            auto *cmd = invoker.lookupBuiltinCommand(bctx.argv[index]);
            if(cmd != nullptr) {
                return cmd(ctx, nbctx);
            } else {
                int status;
                ctx->getProcInvoker().forkAndExec(nbctx, status, useDefaultPath);
                if(WIFEXITED(status)) {
                    return WEXITSTATUS(status);
                }
                if(WIFSIGNALED(status)) {
                    return WTERMSIG(status);
                }
            }
        } else {    // show command description
            unsigned int successCount = 0;
            for(; index < bctx.argc; index++) {
                const char *commandName = bctx.argv[index];
                // check user defined command
                if(ctx->lookupUserDefinedCommand(commandName) != nullptr) {
                    successCount++;
                    fputs(commandName, bctx.fp_stdout);
                    if(showDesc == 2) {
                        fputs(" is an user-defined command", bctx.fp_stdout);
                    }
                    fputc('\n', bctx.fp_stdout);
                    continue;
                }

                // check builtin command
                if(invoker.lookupBuiltinCommand(commandName) != nullptr) {
                    successCount++;
                    fputs(commandName, bctx.fp_stdout);
                    if(showDesc == 2) {
                        fputs(" is a shell builtin command", bctx.fp_stdout);
                    }
                    fputc('\n', bctx.fp_stdout);
                    continue;
                }

                // check external command
                std::string path(resolveFilePath(commandName, false));
                if(!path.empty()) {
                    successCount++;
                    if(showDesc == 1) {
                        fprintf(bctx.fp_stdout, "%s\n", path.c_str());
                    } else {
                        fprintf(bctx.fp_stdout, "%s is %s\n", commandName, path.c_str());
                    }
                    continue;
                }
                if(showDesc == 2) {
                    PERROR(bctx, "%s", commandName);
                }
            }
            return successCount > 0 ? 0 : 1;
        }
    }
    return 0;
}

enum class BinaryOp : unsigned int {
    INVALID,
    STR_EQ,
    STR_NE,
    STR_LT,
    STR_GT,
    EQ,
    NE,
    LT,
    GT,
    LE,
    GE,
};

static int builtin_test(RuntimeContext *, const BuiltinContext &bctx) {
    static std::unordered_map<std::string, BinaryOp> binaryOpMap;
#define ADD_OP(s, op) binaryOpMap.insert(std::make_pair(s, op))
    if(binaryOpMap.empty()) {   // initialize binary operator map
        ADD_OP("=", BinaryOp::STR_EQ);
        ADD_OP("==", BinaryOp::STR_EQ);
        ADD_OP("!=", BinaryOp::STR_NE);
        ADD_OP("<", BinaryOp::STR_LT);
        ADD_OP(">", BinaryOp::STR_GT);
        ADD_OP("-eq", BinaryOp::EQ);
        ADD_OP("-ne", BinaryOp::NE);
        ADD_OP("-lt", BinaryOp::LT);
        ADD_OP("-gt", BinaryOp::GT);
        ADD_OP("-le", BinaryOp::LE);
        ADD_OP("-ge", BinaryOp::GE);
    }
#undef ADD_OP



    bool result = false;
    const int argSize = bctx.argc - 1;

    switch(argSize) {
    case 0: {
        result = false;
        break;
    }
    case 1: {
        result = strlen(bctx.argv[1]) != 0; // check if string is not empty
        break;
    }
    case 2: {   // unary op
        const char *op = bctx.argv[1];
        const char *value = bctx.argv[2];
        if(strlen(op) != 2 || op[0] != '-') {
            builtin_perror(bctx, 0, "%s: invalid unary operator", op);
            return 2;
        }

        const char opKind = op[1];  // ignore -
        switch(opKind) {
        case 'z': { // check if string is empty
            result = strlen(value) == 0;
            break;
        }
        case 'n': { // check if string not empty
            result = strlen(value) != 0;
            break;
        }
        default: {
            builtin_perror(bctx, 0, "%s: invalid unary operator", op);
            return 2;
        }
        }
        break;
    }
    case 3: {   // binary op
        const char *left = bctx.argv[1];
        const char *op = bctx.argv[2];
        const char *right = bctx.argv[3];

        auto iter = binaryOpMap.find(op);
        const BinaryOp opKind = iter != binaryOpMap.end() ? iter->second : BinaryOp::INVALID;
        switch(opKind) {
        case BinaryOp::STR_EQ: {
            result = strcmp(left, right) == 0;
            break;
        }
        case BinaryOp::STR_NE: {
            result = strcmp(left, right) != 0;
            break;
        }
        case BinaryOp::STR_LT: {
            result = strcmp(left, right) < 0;
            break;
        }
        case BinaryOp::STR_GT: {
            result = strcmp(left, right) > 0;
            break;
        }
        case BinaryOp::EQ:
        case BinaryOp::NE:
        case BinaryOp::LT:
        case BinaryOp::GT:
        case BinaryOp::LE:
        case BinaryOp::GE: {
            int s = 0;
            long n1 = convertToInt64(left, s, false);
            if(s != 0) {
                builtin_perror(bctx, 0, "%s: must be integer", left);
                return 2;
            }

            long n2 = convertToInt64(right, s, false);
            if(s != 0) {
                builtin_perror(bctx, 0, "%s: must be integer", right);
                return 2;
            }
            if(opKind == BinaryOp::EQ) {
                result = n1 == n2;
            } else if(opKind == BinaryOp::NE) {
                result = n1 != n2;
            } else if(opKind == BinaryOp::LT) {
                result = n1 < n2;
            } else if(opKind == BinaryOp::GT) {
                result = n1 > n2;
             } else if(opKind == BinaryOp::LE) {
                result = n1 <= n2;
            } else if(opKind == BinaryOp::GE) {
                result = n1 >= n2;
            }
            break;
        }
        case BinaryOp::INVALID: {
            builtin_perror(bctx, 0, "%s: invalid binary operator", op);
            return 2;
        }
        }
        break;
    }
    default: {
        builtin_perror(bctx, 0, "too many arguments");
        return 2;
    }
    }
    return result ? 0 : 1;
}


// ############################
// ##     BuiltinContext     ##
// ############################

BuiltinContext::BuiltinContext(int argc, char **argv, int stdin_fd, int stdout_fd, int stderr_fd) :
        argc(argc), argv(argv),
        fp_stdin(stdin_fd == STDIN_FILENO ? stdin : fdopen(stdin_fd, "r")),
        fp_stdout(stdout_fd == STDOUT_FILENO ? stdout : fdopen(stdout_fd, "w")),
        fp_stderr(stderr_fd == STDERR_FILENO ? stderr : fdopen(stderr_fd, "w")) { }

BuiltinContext::BuiltinContext(char *const *argv, int stdin_fd, int stdout_fd, int stderr_fd) :
        BuiltinContext(1, const_cast<char **>(argv), stdin_fd, stdout_fd, stderr_fd) {
    for(; this->argv[this->argc] != nullptr; this->argc++);
}

BuiltinContext::BuiltinContext(int offset, const BuiltinContext &bctx) :
        argc(bctx.argc - offset), argv(bctx.argv + offset),
        fp_stdin(bctx.fp_stdin), fp_stdout(bctx.fp_stdout), fp_stderr(bctx.fp_stderr) {
    assert(offset < bctx.argc);
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
    this->argArray.push_back(DSValue());
    this->redirOptions.push_back(std::make_pair(RedirectOP::DUMMY, DSValue()));
}

void ProcInvoker::addCommandName(DSValue &&value) {
    this->argArray.push_back(std::move(value));
}

void ProcInvoker::addArg(DSValue &&value, bool skipEmptyString) {
    DSType *valueType = value->getType();
    if(*valueType == this->ctx->getPool().getStringType()) {
        if(skipEmptyString && typeAs<String_Object>(value)->empty()) {
            return;
        }
        this->argArray.push_back(std::move(value));
        return;
    }

    if(*valueType == this->ctx->getPool().getStringArrayType()) {
        Array_Object *arrayObj = typeAs<Array_Object>(value);
        for(auto &element : arrayObj->getValues()) {
            if(typeAs<String_Object>(element)->empty()) {
                continue;
            }
            this->argArray.push_back(element);
        }
    } else {
        fatal("illegal command parameter type: %s\n", this->ctx->getPool().getTypeName(*valueType).c_str());
    }
}

void ProcInvoker::addRedirOption(RedirectOP op, DSValue &&value) {
    DSType *valueType = value->getType();
    if(*valueType == this->ctx->getPool().getStringType()) {
        this->redirOptions.push_back(std::make_pair(op, value));
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
static int redirectToFile(const DSValue &fileName, const char *mode, int targetFD) {
    FILE *fp = fopen(typeAs<String_Object>(fileName)->getValue(), mode);
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
bool ProcInvoker::redirect(unsigned int procIndex, int errorPipe, int stdin_fd, int stdout_fd, int stderr_fd) {
#define CHECK_ERROR(result) do { occuredError = (result); if(occuredError != 0) { goto ERR; } } while(0)

    int occuredError = 0;

    unsigned int startIndex = this->procOffsets[procIndex].second;
    for(; this->redirOptions[startIndex].first != RedirectOP::DUMMY; startIndex++) {
        auto &pair = this->redirOptions[startIndex];
        switch(pair.first) {
        case IN_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "rb", stdin_fd));
            break;
        }
        case OUT_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", stdout_fd));
            break;
        }
        case OUT_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", stdout_fd));
            break;
        }
        case ERR_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", stderr_fd));
            break;
        }
        case ERR_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", stderr_fd));
            break;
        }
        case MERGE_ERR_2_OUT_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", stdout_fd));
            dup2(stdout_fd, stderr_fd);
            break;
        }
        case MERGE_ERR_2_OUT_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", stdout_fd));
            dup2(stdout_fd, stderr_fd);
            break;
        }
        case MERGE_ERR_2_OUT: {
            dup2(stdout_fd, stderr_fd);
            break;
        }
        case MERGE_OUT_2_ERR: {
            dup2(stderr_fd, stdout_fd);
            break;
        }
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

ProcInvoker::builtin_command_t ProcInvoker::lookupBuiltinCommand(const char *commandName) {
    auto iter = this->builtinMap.find(commandName);
    if(iter == this->builtinMap.end()) {
        return nullptr;
    }
    return iter->second;
}

DSValue *ProcInvoker::getARGV(unsigned int procIndex) {
    return this->argArray.data() + this->procOffsets[procIndex].first;
}

EvalStatus ProcInvoker::invoke() {
    const unsigned int procSize = this->procOffsets.size();

    // check builtin command
    if(procSize == 1) {
        builtin_command_t cmd_ptr = this->lookupBuiltinCommand(this->getCommandName(0));
        if(cmd_ptr != nullptr &&
                this->ctx->lookupUserDefinedCommand(this->getCommandName(0)) == nullptr) {
            DSValue *ptr = this->getARGV(0);
            unsigned int argc = 1;
            for(; ptr[argc].get() != nullptr; argc++);
            char *argv[argc + 1];
            for(unsigned int i = 0; i < argc; i++) {
                argv[i] = const_cast<char *>(typeAs<String_Object>(ptr[i])->getValue());
            }
            argv[argc] = nullptr;
            
            bool dupFD = strcmp(argv[0], "exec") != 0
                         && this->redirOptions[this->procOffsets[0].second].first != RedirectOP::DUMMY;
            int stdin_fd = dupFD ? dup(STDIN_FILENO) : STDIN_FILENO;
            int stdout_fd = dupFD ? dup(STDOUT_FILENO) : STDOUT_FILENO;
            int stderr_fd = dupFD ? dup(STDERR_FILENO) : STDERR_FILENO;

            if(!this->redirect(0, -1, stdin_fd, stdout_fd, stderr_fd)) {
                this->ctx->updateExitStatus(1);
                return EvalStatus::THROW;
            }

            BuiltinContext bctx(argc, argv, stdin_fd, stdout_fd, stderr_fd);
            this->ctx->updateExitStatus(cmd_ptr(this->ctx, bctx));

            if(dupFD) {
                fclose(bctx.fp_stdin);
                fclose(bctx.fp_stdout);
                fclose(bctx.fp_stderr);
            } else {    // flush standard stream
                fflush(bctx.fp_stdin);
                fflush(bctx.fp_stdout);
                fflush(bctx.fp_stderr);
            }

            return EvalStatus::SUCCESS;
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
    for(procIndex = 0; procIndex < procSize && (pid[procIndex] = xfork()) > 0; procIndex++) {
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
            int status = 0;
            this->ctx->xwaitpid(pid[i], status, 0);
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

        this->redirect(procIndex, selfpipes[procIndex][WRITE_PIPE], STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);

        closeAllPipe(procSize, pipefds);

        // create argv
        DSValue *ptr = this->getARGV(procIndex);

        // check user defined command
        UserDefinedCmdNode *udcNode = this->ctx->lookupUserDefinedCommand(this->getCommandName(procIndex));
        if(udcNode != nullptr) {
            closeAllPipe(procSize, selfpipes);
            exit(this->ctx->execUserDefinedCommand(udcNode, ptr));
        }

        unsigned int argc = 1;
        for(; ptr[argc]; argc++);
        char *argv[argc + 1];
        for(unsigned int i = 0; i < argc; i++) {
            argv[i] = const_cast<char *>(typeAs<String_Object>(ptr[i])->getValue());
        }
        argv[argc] = nullptr;

        // check builtin command
        builtin_command_t cmd_ptr = this->lookupBuiltinCommand(argv[0]);
        if(cmd_ptr != nullptr) {
            closeAllPipe(procSize, selfpipes);

            BuiltinContext bctx(argc, argv, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
            exit(cmd_ptr(this->ctx, bctx));
        }

        // exec external command
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

void ProcInvoker::execBuiltinCommand(char *const argv[]) {
    builtin_command_t cmd_ptr = this->lookupBuiltinCommand(argv[0]);
    if(cmd_ptr == nullptr) {
        fprintf(stderr, "ydsh: %s: not builtin command\n", argv[0]);
        this->ctx->updateExitStatus(1);
        return;
    }

    BuiltinContext bctx(argv, dup(STDIN_FILENO), dup(STDOUT_FILENO), dup(STDERR_FILENO));
    this->ctx->updateExitStatus(cmd_ptr(this->ctx, bctx));

    fclose(bctx.fp_stdin);
    fclose(bctx.fp_stdout);
    fclose(bctx.fp_stderr);
}

void ProcInvoker::forkAndExec(const BuiltinContext &bctx, int &status, bool useDefaultPath) {
    pid_t pid = xfork();
    if(pid == -1) {
        perror("child process error");
        exit(1);
    } else if(pid == 0) {   // child
        // replace standard stream to bctx
        dup2(fileno(bctx.fp_stdin), STDIN_FILENO);
        dup2(fileno(bctx.fp_stdout), STDOUT_FILENO);
        dup2(fileno(bctx.fp_stderr), STDERR_FILENO);

        builtin_execvpe(const_cast<char **>(bctx.argv), nullptr, nullptr, useDefaultPath);
        PERROR(bctx, "");
        exit(1);
    } else {    // parent process
        this->ctx->xwaitpid(pid, status, 0);
    }
}

const char *ProcInvoker::getCommandName(unsigned int procIndex) {
    return typeAs<String_Object>(this->getARGV(procIndex)[0])->getValue();
}

bool ProcInvoker::checkChildError(const std::pair<unsigned int, ChildError> &errorPair) {
    if(!errorPair.second) {
        auto &pair = this->redirOptions[errorPair.second.redirIndex];

        std::string msg;
        if(pair.first == RedirectOP::DUMMY) {  // execution error
            msg += "execution error: ";
            msg += this->getCommandName(errorPair.first);
        } else {    // redirection error
            msg += "io redirection error: ";
            if(pair.second && typeAs<String_Object>(pair.second)->size() != 0) {
                msg += typeAs<String_Object>(pair.second)->getValue();
            }
        }
        this->ctx->throwSystemError(errorPair.second.errorNum, std::move(msg));
        return false;
    }
    return true;
}


} // namespace core
} // namespace ydsh