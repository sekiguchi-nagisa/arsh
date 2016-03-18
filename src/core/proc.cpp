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

#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <cstdlib>
#include <cstdarg>
#include <unordered_map>

#include "logger.h"
#include "proc.h"
#include "context.h"
#include "symbol.h"
#include "../misc/num.h"
#include "../misc/files.h"

extern char **environ;

namespace ydsh {
namespace core {

/**
 * if filePath is null, not execute and set ENOENT.
 * argv is not null.
 * envp may be null.
 * if success, not return.
 */
static void xexecve(const char *filePath, char **argv, char *const *envp) {
    if(filePath == nullptr) {
        errno = ENOENT;
        return;
    }

    // set env
    setenv("_", filePath, 1);
    if(envp == nullptr) {
        envp = environ;
    }

    LOG_L(DUMP_EXEC, [&](std::ostream &stream) {
        stream << "execve(" << filePath << ", [";
        for(unsigned int i = 0; argv[i] != nullptr; i++) {
            if(i > 0) {
                stream << ", ";
            }
            stream << argv[i];
        }
        stream << "])";
    });

    // execute external command
    execve(filePath, argv, envp);
}

/**
 * if errorNum is not 0, include strerror(errorNum)
 */
static void builtin_perror(char *const *argv, int errorNum, const char *fmt, ...) {
    const char *cmdName = argv[0];
    fprintf(stderr, "-ydsh: %s", cmdName);

    if(strcmp(fmt, "") != 0) {
        fputs(": ", stderr);

        va_list arg;
        va_start(arg, fmt);

        vfprintf(stderr, fmt, arg);

        va_end(arg);
    }

    if(errorNum != 0) {
        fprintf(stderr, ": %s", strerror(errorNum));
    }
    fputc('\n', stderr);
}

#define PERROR(argv, fmt, ...) builtin_perror(argv, errno, fmt, ## __VA_ARGS__ )


// builtin command definition
static int builtin___gets(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin___puts(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_cd(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_check_env(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_command(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_complete(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_echo(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_eval(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_exec(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_exit(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_false(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_hash(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_help(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_ps_intrp(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_pwd(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_read(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_test(RuntimeContext *ctx, const int argc, char *const *argv);
static int builtin_true(RuntimeContext *ctx, const int argc, char *const *argv);

const struct {
    const char *commandName;
    builtin_command_t cmd_ptr;
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
        {"cd", builtin_cd, "-LP [dir]",
                "    Changing the current directory to DIR.  The Environment variable\n"
                "    HOME is the default DIR.  A null directory name is the same as\n"
                "    the current directory. If -L is specified, use logical directory \n"
                "    (with symbolic link). If -P is specified, use physical directory \n"
                "    (without symbolic  link). Default is -L."},
        {"check_env", builtin_check_env, "[variable ...]",
                "    Check existence of specified environmental variables.\n"
                "    If all of variables are exist and not empty string, exit with 0."},
        {"command", builtin_command, "[-pVv] command [arg ...]",
                "    Execute COMMAND with ARGS excepting user defined command.\n"
                        "    If -p option is specified, search command from default PATH.\n"
                        "    If -V or -v option are specified, print description of COMMAND.\n"
                        "    -V option shows more detailed information."},
        {"complete", builtin_complete, "[line]",
                "    Show completion candidates."},
        {"echo", builtin_echo, "[-neE] [arg ...]",
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
                "                  \\v    vertical tab\n"
                "                  \\0nnn N is octal number.  NNN can be 0 to 3 number\n"
                "                  \\xnn  N is hex number.  NN can be 1 to 2 number\n"
                "        -E    disable escape sequence interpretation"},
        {"eval", builtin_eval, "[args ...]",
                "    evaluate ARGS as command."},
        {"exec", builtin_exec, "[-c] [-a name] file [args ...]",
                "    Execute FILE and replace this shell with specified program.\n"
                "    If FILE is not specified, the redirections take effect in this shell.\n"
                "    IF FILE execution fail, terminate this shell immediately\n"
                "    Options:\n"
                "        -c    cleaner environmental variable\n"
                "        -a    specify set program name(default is FILE)"},
        {"exit", builtin_exit, "[n]",
                "    Exit the shell with a status of N.  If N is omitted, the exit\n"
                "    status is 0."},
        {"false", builtin_false, "",
                "    Always failure (exit status is 1)."},
        {"hash", builtin_hash, "[-r] [command ...]",
                "    Cache file path of specified commands.  If -r option is supplied,\n"
                "    removes specified command path (if not specified, remove all cache).\n"
                "    If option is not supplied, display all cached path."},
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
                "        \\]    end of unprintable sequence\n"
                "        \\0nnn N is octal number.  NNN can be 0 to 3 number\n"
                "        \\xnn  N is hex number.  NN can be 1 to 2 number"},
        {"pwd", builtin_pwd, "-LP",
                "    Print the current working directory(absolute path).\n"
                "    If -L specified, print logical working directory.\n"
                "    If -P specified, print physical working directory\n"
                "    (without symbolic link). Default is -L."},
        {"read", builtin_read, "[-r] [-p prompt] [-f field separator] [name ...]",
                "    Read from standard input.\n"
                "    Options:\n"
                "        -r    disable backslash escape\n"
                "        -p    specify prompt string\n"
                "        -f    specify field separator (if not, use IFS)"},
        {"test", builtin_test, "[expr]",
                "    Unary or Binary expressions.\n"
                "    If expression is true, return 0\n"
                "    If expression is false, return 1\n"
                "    If operand or operator is invalid, return 2\n"
                "\n"
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
                "    Integer value is signed int 64.\n"
                "\n"
                "    File operators:\n"
                "        -a FILE\n"
                "        -e FILE        check if file exists\n"
                "        -b FILE        check if file is block device\n"
                "        -c FILE        check if file is character device\n"
                "        -d FILE        check if file is a directory\n"
                "        -f FILE        check if file is a regular file\n"
                "        -g FILE        check if file has set-group-id bit\n"
                "        -h FILE\n"
                "        -L FILE        check if file is a symbolic link\n"
                "        -k FILE        check if file has sticky bit\n"
                "        -p FILE        check if file is a named pipe\n"
                "        -r FILE        check if file is readable\n"
                "        -s FILE        check if file is not empty\n"
                "        -S FILE        check if file is a socket\n"
                "        -t FD          check if file descriptor is a terminal\n"
                "        -u FILE        check if file has set-user-id bit\n"
                "        -w FILE        check if file is writable\n"
                "        -x FILE        check if file is executable\n"
                "        -O FILE        check if file is effectively owned by user\n"
                "        -G FILE        check if file is effectively owned by group"},
        {"true", builtin_true, "",
                "    Always success (exit status is 0)."},
};

unsigned int getBuiltinCommandSize() {
    return sizeof(builtinCommands) / sizeof(builtinCommands[0]);
}

/**
 * if index is out of range, return null
 */
const char *getBultinCommandName(unsigned int index) {
    if(index >= getBuiltinCommandSize()) {
        return nullptr;
    }
    return builtinCommands[index].commandName;
}

/**
 * return null, if not found builtin command.
 */
static builtin_command_t lookupBuiltinCommand(const char *commandName) {
    /**
     * builtin command name and pointer.
     */
    static CStringHashMap<builtin_command_t> builtinMap;

    // register builtin command
    if(builtinMap.empty()) {
        for(const auto &e : builtinCommands) {
            builtinMap.insert(std::make_pair(e.commandName, e.cmd_ptr));
        }
    }

    auto iter = builtinMap.find(commandName);
    if(iter == builtinMap.end()) {
        return nullptr;
    }
    return iter->second;
}

static void printAllUsage(FILE *fp) {
    for(const auto &e : builtinCommands) {
        fprintf(fp, "%s %s\n", e.commandName, e.usage);
    }
}

static bool startsWith(const char *prefix, const char *target) {
    const unsigned int prefixSize = strlen(prefix);
    const unsigned int targetSize = strlen(target);
    return prefixSize <= targetSize && strncmp(prefix, target, prefixSize) == 0;
}

/**
 * if not found command, return false.
 */
static bool printUsage(FILE *fp, const char *prefix, bool isShortHelp = true) {
    bool matched = false;
    for(const auto &e : builtinCommands) {
        const char *cmdName = e.commandName;
        if(startsWith(prefix, cmdName)) {
            fprintf(fp, "%s: %s %s\n", cmdName, cmdName, e.usage);
            if(!isShortHelp) {
                fprintf(fp, "%s\n", e.detail);
            }
            matched = true;
        }
    }
    return matched;
}

static int builtin_help(RuntimeContext *, const int argc, char *const *argv) {
    if(argc == 1) {
        printAllUsage(stdout);
        return 0;
    }
    bool isShortHelp = false;
    bool foundValidCommand = false;
    for(int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if(strcmp(arg, "-s") == 0 && argc == 2) {
            printAllUsage(stdout);
            foundValidCommand = true;
        } else if(strcmp(arg, "-s") == 0 && i == 1) {
            isShortHelp = true;
        } else {
            if(printUsage(stdout, arg, isShortHelp)) {
                foundValidCommand = true;
            }
        }
    }
    if(!foundValidCommand) {
        fprintf(stderr, "-ydsh: help: no help topics match `%s'.  Try `help help'.\n", argv[argc - 1]);
        return 1;
    }
    return 0;
}

inline static void showUsage(char *const *argv) {
    printUsage(stderr, argv[0]);
}

static int builtin_cd(RuntimeContext *ctx, const int argc, char *const *argv) {
    bool useOldpwd = false;
    bool useLogical = true;

    int index = 1;
    for(; index < argc; index++) {
        const char *arg = argv[index];
        if(strcmp(arg, "-") == 0) {
            useOldpwd = true;
        } else if(strcmp(arg, "-P") == 0) {
            useLogical = false;
        } else if(strcmp(arg, "-L") == 0) {
            useLogical = true;
        } else {
            break;
        }
    }

    const char *dest = nullptr;
    if(useOldpwd) {
        dest = getenv(ENV_OLDPWD);
    } else if(index < argc) {
        dest = argv[index];
    } else {
        dest = getenv(ENV_HOME);
    }

    if(useOldpwd && dest != nullptr) {
        printf("%s\n", dest);
    }

    if(!ctx->changeWorkingDir(dest, useLogical)) {
        PERROR(argv, "%s", dest);
        return 1;
    }
    return 0;
}

static int builtin_check_env(RuntimeContext *, const int argc, char *const *argv) {
    if(argc == 1) {
        showUsage(argv);
        return 1;
    }
    for(int i = 1; i < argc; i++) {
        const char *env = getenv(argv[i]);
        if(env == nullptr || strlen(env) == 0) {
            return 1;
        }
    }
    return 0;
}

static int builtin_exit(RuntimeContext *ctx, const int argc, char *const *argv) {
    int ret = 0;
    if(argc > 1) {
        const char *num = argv[1];
        int status;
        long value = convertToInt64(num, status);
        if(status == 0) {
            ret = value;
        }
    }
    ctx->exitShell(ret);
    return ret;
}

static int builtin_echo(RuntimeContext *, const int argc, char *const *argv) {
    FILE *fp = stdout;  // not close it.

    bool newline = true;
    bool interpEscape = false;
    int index = 1;

    // parse option
    for(; index < argc; index++) {
        if(strcmp(argv[index], "-n") == 0) {
            newline = false;
        } else if(strcmp(argv[index], "-e") == 0) {
            interpEscape = true;
        } else if(strcmp(argv[index], "-E") == 0) {
            interpEscape = false;
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
                case '0': {
                    int v = 0;
                    for(unsigned int c = 0; c < 3; c++) {
                        if(isOctal(arg[i + 1])) {
                            v *= 8;
                            v += arg[++i] - '0';
                        } else {
                            break;
                        }
                    }
                    ch = (char) v;
                    break;
                }
                case 'x': {
                    if(isHex(arg[i + 1])) {
                        int v = toHex(arg[++i]);
                        if(isHex(arg[i + 1])) {
                            v *= 16;
                            v += toHex(arg[++i]);
                        }
                        ch = (char) v;
                        break;
                    }
                    i--;
                    break;
                }
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

static int builtin_true(RuntimeContext *, const int, char *const *) {
    return 0;
}

static int builtin_false(RuntimeContext *, const int, char *const *) {
    return 1;
}

/**
 * for stdin redirection test
 */
static int builtin___gets(RuntimeContext *, const int, char *const *) {
    unsigned int bufSize = 256;
    char buf[bufSize];
    int readSize;
    while((readSize = fread(buf, sizeof(char), bufSize, stdin)) > 0) {
        fwrite(buf, sizeof(char), readSize, stdout);
    }
    clearerr(stdin);    // clear eof flag
    return 0;
}

/**
 * for stdout/stderr redirection test
 */
static int builtin___puts(RuntimeContext *, const int argc, char *const *argv) {
    for(int index = 1; index < argc; index++) {
        const char *arg = argv[index];
        if(strcmp("-1", arg) == 0 && ++index < argc) {
            fputs(argv[index], stdout);
            fputc('\n', stdout);
            fflush(stdout);
        } else if(strcmp("-2", arg) == 0 && ++index < argc) {
            fputs(argv[index], stderr);
            fputc('\n', stderr);
            fflush(stderr);
        } else {
            return 1;   // need option
        }
    }
    return 0;
}

/**
 * for prompt string debugging
 */
static int builtin_ps_intrp(RuntimeContext *ctx, const int argc, char *const *argv) {
    if(argc != 2) {
        showUsage(argv);
        return 1;
    }
    std::string str;
    ctx->interpretPromptString(argv[1], str);
    fputs(str.c_str(), stdout);
    fputc('\n', stdout);
    return 0;
}

static int builtin_exec(RuntimeContext *ctx, const int argc, char *const *argv) {
    int index = 1;
    bool clearEnv = false;
    const char *progName = nullptr;
    for(; index < argc; index++) {
        const char *arg = argv[index];
        if(arg[0] != '-') {
            break;
        }
        if(strcmp(arg, "-c") == 0) {
            clearEnv = true;
        } else if(strcmp(arg, "-a") == 0 && ++index < argc) {
            progName = argv[index];
        } else {
            showUsage(argv);
            return 1;
        }
    }

    if(index < argc) { // exec
        char **argv2 = const_cast<char **>(argv + index);
        const char *filePath = ctx->getPathCache().searchPath(argv2[0], FilePathCache::DIRECT_SEARCH);
        if(progName != nullptr) {
            argv2[0] = const_cast<char *>(progName);
        }

        char *envp[] = {nullptr};
        xexecve(filePath, argv2, clearEnv ? envp : nullptr);
        PERROR(argv, "%s", argv[index]);
        exit(1);
    }
    return 0;
}

/**
 * write status to status (same of wait's status).
 */
static void forkAndExec(RuntimeContext *ctx, char *const *argv, int &status, bool useDefaultPath = false) {
    // setup self pipe
    int selfpipe[2];
    if(pipe(selfpipe) < 0) {
        perror("pipe creation error");
        exit(1);
    }
    if(fcntl(selfpipe[WRITE_PIPE], F_SETFD, fcntl(selfpipe[WRITE_PIPE], F_GETFD) | FD_CLOEXEC)) {
        perror("fcntl error");
        exit(1);
    }

    const char *filePath = ctx->getPathCache().searchPath(
            argv[0], useDefaultPath ? FilePathCache::USE_DEFAULT_PATH : 0);

    pid_t pid = xfork();
    if(pid == -1) {
        perror("child process error");
        exit(1);
    } else if(pid == 0) {   // child
        xexecve(filePath, const_cast<char **>(argv), nullptr);

        int errnum = errno;
        PERROR(argv, "");
        write(selfpipe[WRITE_PIPE], &errnum, sizeof(int));
        exit(1);
    } else {    // parent process
        close(selfpipe[WRITE_PIPE]);
        int readSize;
        int errnum = 0;
        while((readSize = read(selfpipe[READ_PIPE], &errnum, sizeof(int))) == -1) {
            if(errno != EAGAIN && errno != EINTR) {
                break;
            }
        }
        close(selfpipe[READ_PIPE]);
        if(readSize > 0 && errnum == ENOENT) {  // remove cached path
            ctx->getPathCache().removePath(argv[0]);
        }

        ctx->xwaitpid(pid, status, 0);
    }
}

static int builtin_eval(RuntimeContext *ctx, const int argc, char *const *argv) {
    if(argc <= 1) {
        return 0;
    }

    const char *cmdName = argv[1];
    // user-defined command
    UserDefinedCmdNode *udcNode = ctx->lookupUserDefinedCommand(cmdName);
    if(udcNode != nullptr) {
        pid_t pid = xfork();
        if(pid == -1) {
            perror("child process error");
            exit(1);
        } else if(pid == 0) {   // child
            const unsigned int size = argc;
            DSValue *argv2 = new DSValue[size];
            for(int i = 1; i < argc; i++) {
                argv2[i - 1] = DSValue::create<String_Object>(
                        ctx->getPool().getStringType(), std::string(argv[i])
                );
            }
            argv2[size - 1] = nullptr;

            int r = ctx->execUserDefinedCommand(udcNode, argv2);
            delete[] argv2;
            exit(r);
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
        return 0;
    }

    // builtin command
    int argc2 = argc - 1;
    char *const *argv2 = argv + 1;
    builtin_command_t builtinCmd = lookupBuiltinCommand(cmdName);
    if(builtinCmd != nullptr) {
        return builtinCmd(ctx, argc2, argv2);
    }

    // external command
    int status;
    forkAndExec(ctx, argv2, status);
    if(WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if(WIFSIGNALED(status)) {
        return WTERMSIG(status);
    }
    return 0;
}

static int builtin_pwd(RuntimeContext *, const int argc, char *const *argv) {
    bool useLogical = true;

    int index = 1;
    for(; index < argc; index++) {
        const char *arg = argv[index];
        if(arg[0] != '-') {
            break;
        }
        if(strcmp(arg, "-L") == 0) {
            useLogical = true;
        } else if(strcmp(arg, "-P") == 0) {
            useLogical = false;
        } else {
            builtin_perror(argv, 0, "%s: invalid option", arg);
            showUsage(argv);
            return 1;
        }
    }

    if(useLogical) {
        const char *dir = RuntimeContext::getLogicalWorkingDir();
        if(!S_ISDIR(getStMode(dir))) {
            PERROR(argv, ".");
            return 1;
        }
        fputs(dir, stdout);
    } else {
        size_t size = PATH_MAX;
        char buf[size];
        if(getcwd(buf, size) == nullptr) {
            PERROR(argv, ".");
            return 1;
        }
        fputs(buf, stdout);
    }
    fputc('\n', stdout);
    return 0;
}

static int builtin_command(RuntimeContext *ctx, const int argc, char *const *argv) {
    int index = 1;
    bool useDefaultPath = false;

    /**
     * if 0, ignore
     * if 1, show description
     * if 2, show detailed description
     */
    unsigned char showDesc = 0;

    for(; index < argc; index++) {
        const char *arg = argv[index];
        if(arg[0] != '-') {
            break;
        } else if(strcmp(arg, "-p") == 0) {
            useDefaultPath = true;
        } else if(strcmp(arg, "-v") == 0) {
            showDesc = 1;
        } else if(strcmp(arg, "-V") == 0) {
            showDesc = 2;
        } else {
            builtin_perror(argv, 0, "%s: invalid option", arg);
            showUsage(argv);
            return 1;
        }
    }

    if(index < argc) {
        if(showDesc == 0) { // execute command
            char *const *argv2 = argv + index;
            int argc2 = argc - index;
            auto *cmd = lookupBuiltinCommand(argv[index]);
            if(cmd != nullptr) {
                return cmd(ctx, argc2, argv2);
            } else {
                int status;
                forkAndExec(ctx, argv2, status, useDefaultPath);
                if(WIFEXITED(status)) {
                    return WEXITSTATUS(status);
                }
                if(WIFSIGNALED(status)) {
                    return WTERMSIG(status);
                }
            }
        } else {    // show command description
            unsigned int successCount = 0;
            for(; index < argc; index++) {
                const char *commandName = argv[index];
                // check user defined command
                if(ctx->lookupUserDefinedCommand(commandName) != nullptr) {
                    successCount++;
                    fputs(commandName, stdout);
                    if(showDesc == 2) {
                        fputs(" is an user-defined command", stdout);
                    }
                    fputc('\n', stdout);
                    continue;
                }

                // check builtin command
                if(lookupBuiltinCommand(commandName) != nullptr) {
                    successCount++;
                    fputs(commandName, stdout);
                    if(showDesc == 2) {
                        fputs(" is a shell builtin command", stdout);
                    }
                    fputc('\n', stdout);
                    continue;
                }

                // check external command
                const char *path = ctx->getPathCache().searchPath(commandName, FilePathCache::DIRECT_SEARCH);
                if(path != nullptr) {
                    successCount++;
                    if(showDesc == 1) {
                        printf("%s\n", path);
                    } else if(ctx->getPathCache().isCached(commandName)) {
                        printf("%s is hashed (%s)\n", commandName, path);
                    } else {
                        printf("%s is %s\n", commandName, path);
                    }
                    continue;
                }
                if(showDesc == 2) {
                    PERROR(argv, "%s", commandName);
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

static int builtin_test(RuntimeContext *, const int argc, char *const *argv) {
    static CStringHashMap<BinaryOp> binaryOpMap;
    if(binaryOpMap.empty()) {
        static const struct {
            const char *k;
            BinaryOp op;
        } table[] = {
                {"=", BinaryOp::STR_EQ},
                {"==", BinaryOp::STR_EQ},
                {"!=", BinaryOp::STR_NE},
                {"<", BinaryOp::STR_LT},
                {">", BinaryOp::STR_GT},
                {"-eq", BinaryOp::EQ},
                {"-ne", BinaryOp::NE},
                {"-lt", BinaryOp::LT},
                {"-gt", BinaryOp::GT},
                {"-le", BinaryOp::LE},
                {"-ge", BinaryOp::GE},
        };

        for(const auto &e : table) {
            binaryOpMap.insert(std::make_pair(e.k, e.op));
        }
    }

    bool result = false;
    const int argSize = argc - 1;

    switch(argSize) {
    case 0: {
        result = false;
        break;
    }
    case 1: {
        result = strlen(argv[1]) != 0; // check if string is not empty
        break;
    }
    case 2: {   // unary op
        const char *op = argv[1];
        const char *value = argv[2];
        if(strlen(op) != 2 || op[0] != '-') {
            builtin_perror(argv, 0, "%s: invalid unary operator", op);
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
        case 'a':
        case 'e': {
            result = access(value, F_OK) == 0;  // check if file exists
            break;
        }
        case 'b': {
            result = S_ISBLK(getStMode(value)); // check if file is block device
            break;
        }
        case 'c': {
            result = S_ISCHR(getStMode(value)); // check if file is character device
            break;
        }
        case 'd': {
            result = S_ISDIR(getStMode(value)); // chedck if file is directory
            break;
        }
        case 'f': {
            result = S_ISREG(getStMode(value)); // check if file is regular file.
            break;
        }
        case 'g': {
            result = S_IS_PERM_(getStMode(value), S_ISUID); // check if file has set-uid-bit
            break;
        }
        case 'h':
        case 'L': {
            mode_t mode = 0;
            struct stat st;
            if(lstat(value, &st) == 0) {
                mode = st.st_mode;
            }
            result = S_ISLNK(mode); // check if file is symbolic-link
            break;
        }
        case 'k': {
            result = S_IS_PERM_(getStMode(value), S_ISVTX); // check if file has sticky bit
            break;
        }
        case 'p': {
            result = S_ISFIFO(getStMode(value));    // check if file is a named pipe
            break;
        }
        case 'r': {
            result = access(value, R_OK) == 0;  // check if file is readable
            break;
        }
        case 's': {
            struct stat st;
            result = stat(value, &st) == 0 && st.st_size != 0;  // check if file is not empty
            break;
        }
        case 'S': {
            result = S_ISSOCK(getStMode(value));    // check file is a socket
            break;
        }
        case 't': {
            int s;
            long n = convertToInt64(value, s);
            result = s == 0 && n >= INT32_MIN && n <= INT32_MAX && isatty(n) != 0;  //  check if FD is a terminal
            break;
        }
        case 'u': {
            result = S_IS_PERM_(getStMode(value), S_ISUID); // check file has set-user-id bit
            break;
        }
        case 'w': {
            result = access(value, W_OK) == 0;   // check if file is writable
            break;
        }
        case 'x': {
            result = access(value, X_OK) == 0;  // check if file is executable
            break;
        }
        case 'O': {
            struct stat st;
            result = stat(value, &st) == 0 && st.st_uid == geteuid();   // check if file is effectively owned
            break;
        }
        case 'G': {
            struct stat st;
            result = stat(value, &st) == 0 && st.st_gid == getegid();   // check if file is effectively owned by group
            break;
        }
        default: {
            builtin_perror(argv, 0, "%s: invalid unary operator", op);
            return 2;
        }
        }
        break;
    }
    case 3: {   // binary op
        const char *left = argv[1];
        const char *op = argv[2];
        const char *right = argv[3];

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
            long n1 = convertToInt64(left, s);
            if(s != 0) {
                builtin_perror(argv, 0, "%s: must be integer", left);
                return 2;
            }

            long n2 = convertToInt64(right, s);
            if(s != 0) {
                builtin_perror(argv, 0, "%s: must be integer", right);
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
            builtin_perror(argv, 0, "%s: invalid binary operator", op);
            return 2;
        }
        }
        break;
    }
    default: {
        builtin_perror(argv, 0, "too many arguments");
        return 2;
    }
    }
    return result ? 0 : 1;
}

/**
 * only allow ascii space
 */
static bool isSpace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

static bool isFieldSep(const char *ifs, int ch) {
    for(unsigned int i = 0; ifs[i] != '\0'; i++) {
        if(ifs[i] == ch) {
            return true;
        }
    }
    return false;
}

static int xfgetc(FILE *fp) {
    int ch = 0;
    do {
        ch = fgetc(fp);
    } while(ch == EOF && ferror(fp) != 0 && (errno == EAGAIN || errno == EINTR));
    return ch;
}

static int builtin_read(RuntimeContext *ctx, const int argc, char *const *argv) {  //FIXME: timeout, no echo, UTF-8
    int index = 1;
    const char *prompt = "";
    const char *ifs = nullptr;
    bool backslash = true;

    for(; index < argc; index++) {
        const char *arg = argv[index];
        if(strlen(arg) != 2 || arg[0] != '-') {
            break;
        }
        const char op = arg[1]; // ignore '-'
        switch(op) {
        case 'p': {
            if(index + 1 < argc) {
                prompt = argv[++index];
                break;
            }
            builtin_perror(argv, 0, "%s: option require argument", arg);
            return 2;
        }
        case 'f': {
            if(index + 1 < argc) {
                ifs = argv[++index];
                break;
            }
            builtin_perror(argv, 0, "%s: option require argument", arg);
            return 2;
        }
        case 'r': {
            backslash = false;
            break;
        }
        default: {
            builtin_perror(argv, 0, "%s: invalid option", arg);
            return 2;
        }
        }
    }

    // check ifs
    if(ifs == nullptr) {
        ifs = ctx->getIFS();
    }

    // clear old variable before read
    ctx->setGlobal(ctx->getBuiltinVarIndex(BuiltinVarOffset::REPLY), ctx->getEmptyStrObj());    // clear REPLY
    typeAs<Map_Object>(ctx->getGlobal(
            ctx->getBuiltinVarIndex(BuiltinVarOffset::REPLY_VAR)))->refValueMap().clear();      // clear reply


    const int varSize = argc - index;  // if zero, store line to REPLY
    const unsigned int varIndex = ctx->getBuiltinVarIndex(
                    varSize == 0 ? BuiltinVarOffset::REPLY : BuiltinVarOffset::REPLY_VAR);
    std::string strBuf;

    // show prompt
    if(isatty(fileno(stdin)) != 0) {
        fputs(prompt, stdout);
        fflush(stdout);
    }

    // read line
    unsigned int skipCount = 1;
    int ch;
    for(bool prevIsBackslash = false; (ch = xfgetc(stdin)) != EOF;
            prevIsBackslash = backslash && ch == '\\' && !prevIsBackslash) {
        if(ch == '\n') {
            if(prevIsBackslash) {
                continue;
            } else {
                break;
            }
        } else if(ch == '\\' && !prevIsBackslash && backslash) {
            continue;
        }

        bool fieldSep = isFieldSep(ifs, ch) && !prevIsBackslash;
        if(fieldSep && skipCount > 0) {
            if(isSpace(ch)) {
                continue;
            }
            if(--skipCount == 1) {
                continue;
            }
        }
        skipCount = 0;
        if(fieldSep && index < argc - 1) {
            auto obj = typeAs<Map_Object>(ctx->getGlobal(varIndex));
            auto varObj = DSValue::create<String_Object>(ctx->getPool().getStringType(), argv[index]);
            auto valueObj = DSValue::create<String_Object>(ctx->getPool().getStringType(), std::move(strBuf));
            std::swap(obj->refValueMap()[std::move(varObj)], valueObj);
            strBuf = "";
            index++;
            skipCount = isSpace(ch) ? 2 : 1;
            continue;
        }
        strBuf += ch;
    }

    // remove last spaces
    if(!strBuf.empty()) {
        // check if field separator has spaces
        bool hasSpace = false;
        for(unsigned int i = 0; ifs[i] != '\0'; i++) {
            if((hasSpace = isSpace(ifs[i]))) {
                break;
            }
        }

        if(hasSpace) {
            while(!strBuf.empty() && isSpace(strBuf.back())) {
                strBuf.pop_back();
            }
        }
    }

    if(varSize == 0) {
        ctx->setGlobal(varIndex,
                       DSValue::create<String_Object>(ctx->getPool().getStringType(), std::move(strBuf)));
    }

    // set rest variable
    for(; index < argc; index++) {
        auto obj = typeAs<Map_Object>(ctx->getGlobal(varIndex));
        auto varObj = DSValue::create<String_Object>(ctx->getPool().getStringType(), argv[index]);
        auto valueObj = DSValue::create<String_Object>(ctx->getPool().getStringType(), std::move(strBuf));
        std::swap(obj->refValueMap()[std::move(varObj)], valueObj);
        strBuf = "";
    }

    if(ch == EOF) {
        clearerr(stdin);
    }
    return ch == EOF ? 1 : 0;
}

static int builtin_hash(RuntimeContext *ctx, const int argc, char *const *argv) {
    bool remove = false;

    // check option
    int index = 1;
    for(; index < argc; index++) {
        const char *arg = argv[index];
        if(arg[0] != '-') {
            break;
        }
        if(strcmp(arg, "-r") == 0) {
            remove = true;
        } else {
            builtin_perror(argv, 0, "%s: invalid option", arg);
            return 2;
        }
    }

    const bool hasNames = index < argc;
    if(hasNames) {
        for(; index < argc; index++) {
            const char *name = argv[index];
            if(remove) {
                ctx->getPathCache().removePath(name);
            } else {
                if(ctx->getPathCache().searchPath(name) == nullptr) {
                    builtin_perror(argv, 0, "%s: not found", name);
                    return 1;
                }
            }
        }
    } else {
        if(remove) {    // remove all cache
            ctx->getPathCache().clear();
        } else {    // show all cache
            const auto cend = ctx->getPathCache().cend();
            if(ctx->getPathCache().cbegin() == cend) {
                fputs("hash: file path cache is empty\n", stdout);
                return 0;
            }
            for(auto iter = ctx->getPathCache().cbegin(); iter != cend; ++iter) {
                printf("%s=%s\n", iter->first, iter->second.c_str());
            }
        }
    }
    return 0;
}

// for completor debugging
static int builtin_complete(RuntimeContext *ctx, const int argc, char *const *argv) {
    if(argc != 2) {
        showUsage(argv);
        return 1;
    }

    std::string line(argv[1]);
    line += '\n';
    auto c = ctx->completeLine(line);
    for(const auto &e : c) {
        fputs(e, stdout);
        fputc('\n', stdout);

        free(e);
    }
    return 0;
}

void RuntimeContext::openProc() {
    DSValue value = this->pop();

    // resolve proc kind (external command, builtin command or user-defined command)
    const char *commandName = typeAs<String_Object>(value)->getValue();
    ProcState::ProcKind procKind = ProcState::EXTERNAL;
    void *ptr = nullptr;

    // first, check user-defined command
    {
        UserDefinedCmdNode *udcNode = this->lookupUserDefinedCommand(commandName);
        if(udcNode != nullptr) {
            procKind = ProcState::ProcKind::USER_DEFINED;
            ptr = udcNode;
        }
    }

    // second, check builtin command
    if(ptr == nullptr) {
        builtin_command_t bcmd = lookupBuiltinCommand(commandName);
        if(bcmd != nullptr) {
            procKind = ProcState::ProcKind::BUILTIN;
            ptr = (void *)bcmd;
        }
    }

    // resolve external command path
    if(ptr == nullptr) {
        ptr = (void *)this->getPathCache().searchPath(commandName);
    }

    auto &pipeline = this->activePipeline();
    unsigned int argOffset = pipeline.getArgArray().size();
    unsigned int redirOffset = pipeline.getRedirOptions().size();
    pipeline.getProcStates().push_back(ProcState(argOffset, redirOffset, procKind, ptr));

    pipeline.getArgArray().push_back(std::move(value));
}

void RuntimeContext::closeProc() {
    auto &pipeline = this->activePipeline();
    pipeline.getArgArray().push_back(DSValue());
    pipeline.getRedirOptions().push_back(std::make_pair(RedirectOP::DUMMY, DSValue()));
}

void RuntimeContext::addArg(bool skipEmptyString) {
    DSValue value = this->pop();
    DSType *valueType = value->getType();
    if(*valueType == this->getPool().getStringType()) {
        if(skipEmptyString && typeAs<String_Object>(value)->empty()) {
            return;
        }
        this->activePipeline().getArgArray().push_back(std::move(value));
        return;
    }

    if(*valueType == this->getPool().getStringArrayType()) {
        Array_Object *arrayObj = typeAs<Array_Object>(value);
        for(auto &element : arrayObj->getValues()) {
            if(typeAs<String_Object>(element)->empty()) {
                continue;
            }
            this->activePipeline().getArgArray().push_back(element);
        }
    } else {
        fatal("illegal command parameter type: %s\n", this->getPool().getTypeName(*valueType).c_str());
    }
}

void RuntimeContext::addRedirOption(RedirectOP op) {
    DSValue value = this->pop();
    DSType *valueType = value->getType();
    if(*valueType == this->getPool().getStringType()) {
        this->activePipeline().getRedirOptions().push_back(std::make_pair(op, value));
    } else {
        fatal("illegal command parameter type: %s\n", this->getPool().getTypeName(*valueType).c_str());
    }
}


// ###############################
// ##     PipelineEvaluator     ##
// ###############################

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
bool PipelineEvaluator::redirect(RuntimeContext &ctx, unsigned int procIndex, int errorPipe) {
#define CHECK_ERROR(result) do { occurredError = (result); if(occurredError != 0) { goto ERR; } } while(0)

    int occurredError = 0;

    unsigned int startIndex = this->procStates[procIndex].redirOffset();
    for(; this->redirOptions[startIndex].first != RedirectOP::DUMMY; startIndex++) {
        auto &pair = this->redirOptions[startIndex];
        switch(pair.first) {
        case IN_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "rb", STDIN_FILENO));
            break;
        }
        case OUT_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDOUT_FILENO));
            break;
        }
        case OUT_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDOUT_FILENO));
            break;
        }
        case ERR_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDERR_FILENO));
            break;
        }
        case ERR_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDERR_FILENO));
            break;
        }
        case MERGE_ERR_2_OUT_2_FILE: {
            CHECK_ERROR(redirectToFile(pair.second, "wb", STDOUT_FILENO));
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        }
        case MERGE_ERR_2_OUT_2_FILE_APPEND: {
            CHECK_ERROR(redirectToFile(pair.second, "ab", STDOUT_FILENO));
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        }
        case MERGE_ERR_2_OUT: {
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        }
        case MERGE_OUT_2_ERR: {
            dup2(STDERR_FILENO, STDOUT_FILENO);
            break;
        }
        default:
            fatal("unsupported redir option: %d\n", pair.first);
        }
    }

    ERR:
    if(occurredError != 0) {
        ChildError e;
        e.redirIndex = startIndex;
        e.errorNum = occurredError;

        if(errorPipe == -1) {
            return this->checkChildError(ctx, std::make_pair(0, e)); // return false
        }
        write(errorPipe, &e, sizeof(ChildError));
        exit(0);
    }
    return true;

#undef CHECK_ERROR
}

DSValue *PipelineEvaluator::getARGV(unsigned int procIndex) {
    return this->argArray.data() + this->procStates[procIndex].argOffset();
}

static void saveStdFD(int (&origFds)[3]) {
    origFds[0] = dup(STDIN_FILENO);
    origFds[1] = dup(STDOUT_FILENO);
    origFds[2] = dup(STDERR_FILENO);
}

static void restoreStdFD(int (&origFds)[3]) {
    dup2(origFds[0], STDIN_FILENO);
    dup2(origFds[1], STDOUT_FILENO);
    dup2(origFds[2], STDERR_FILENO);

    for(unsigned int i = 0; i < 3; i++) {
        close(origFds[i]);
    }
}

static void flushStdFD() {
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);
}

EvalStatus PipelineEvaluator::evalPipeline(RuntimeContext &ctx) {
    const unsigned int procSize = this->procStates.size();

    // check builtin command
    if(procSize == 1) {
        if(this->procStates[0].procKind() == ProcState::ProcKind::BUILTIN) {
            builtin_command_t cmd_ptr = this->procStates[0].builtinCmd();
            DSValue *ptr = this->getARGV(0);
            unsigned int argc = 1;
            for(; ptr[argc].get() != nullptr; argc++);
            char *argv[argc + 1];
            for(unsigned int i = 0; i < argc; i++) {
                argv[i] = const_cast<char *>(typeAs<String_Object>(ptr[i])->getValue());
            }
            argv[argc] = nullptr;

            const bool restoreFD = strcmp(argv[0], "exec") != 0;

            int origFDs[3];
            if(restoreFD) {
                saveStdFD(origFDs);
            }

            if(!this->redirect(ctx, 0, -1)) {
                ctx.updateExitStatus(1);
                return EvalStatus::THROW;
            }

            ctx.updateExitStatus(cmd_ptr(&ctx, argc, argv));

            // flush and restore
            flushStdFD();
            if(restoreFD) {
                restoreStdFD(origFDs);
            }

            return EvalStatus::SUCCESS;
        }
    }

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
    pid_t pid;
    std::pair<unsigned int, ChildError> errorPair;
    unsigned int procIndex;
    for(procIndex = 0; procIndex < procSize && (pid = xfork()) > 0; procIndex++) {
        this->procStates[procIndex].setPid(pid);

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

            if(childError.errorNum == ENOENT) { // if file not found, remove path cache
                const char *cmdName = this->getCommandName(procIndex);
                ctx.getPathCache().removePath(cmdName);
            }
            procIndex = procSize;
            break;
        }
    }

    if(procIndex == procSize) {   // parent process
        // close unused pipe
        closeAllPipe(procSize, pipefds);
        closeAllPipe(procSize, selfpipes);

        // wait for exit
        const unsigned int actualProcSize = this->procStates.size();
        for(unsigned int i = 0; i < actualProcSize; i++) {
            int status = 0;
            ctx.xwaitpid(this->procStates[i].pid(), status, 0);
            if(WIFEXITED(status)) {
                this->procStates[i].set(ProcState::NORMAL, WEXITSTATUS(status));
            }
            if(WIFSIGNALED(status)) {
                this->procStates[i].set(ProcState::INTR, WTERMSIG(status));
            }
        }

        ctx.updateExitStatus(this->procStates[actualProcSize - 1].exitStatus());
        return this->checkChildError(ctx, errorPair) ? EvalStatus::SUCCESS : EvalStatus::THROW ;
    } else if(pid == 0) { // child process
        if(procIndex == 0) {    // first process
            if(procSize > 1) {
                dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
            }
        }
        if(procIndex > 0 && procIndex < procSize - 1) {   // other process.
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == procSize - 1) { // last process
            if(procSize > 1) {
                dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            }
        }

        this->redirect(ctx, procIndex, selfpipes[procIndex][WRITE_PIPE]);

        closeAllPipe(procSize, pipefds);

        /**
         * invoke command
         */
        const auto &procState = this->procStates[procIndex];
        DSValue *ptr = this->getARGV(procIndex);
        const auto procKind = procState.procKind();
        if(procKind == ProcState::ProcKind::USER_DEFINED) { // invoke user-defined command
            UserDefinedCmdNode *udcNode = procState.udcNode();
            closeAllPipe(procSize, selfpipes);
            exit(ctx.execUserDefinedCommand(udcNode, ptr));
        } else {
            // create argv
            unsigned int argc = 1;
            for(; ptr[argc]; argc++);
            char *argv[argc + 1];
            for(unsigned int i = 0; i < argc; i++) {
                argv[i] = const_cast<char *>(typeAs<String_Object>(ptr[i])->getValue());
            }
            argv[argc] = nullptr;

            if(procKind == ProcState::ProcKind::BUILTIN) {  // invoke builtin command
                builtin_command_t cmd_ptr = procState.builtinCmd();
                closeAllPipe(procSize, selfpipes);
                exit(cmd_ptr(&ctx, argc, argv));
            } else {    // invoke external command
                xexecve(procState.filePath(), argv, nullptr);

                ChildError e;
                e.errorNum = errno;

                write(selfpipes[procIndex][WRITE_PIPE], &e, sizeof(ChildError));
                exit(1);
            }
        }
    } else {
        perror("child process error");
        exit(1);
    }
}

void RuntimeContext::execBuiltinCommand(char *const argv[]) {
    builtin_command_t cmd_ptr = lookupBuiltinCommand(argv[0]);
    if(cmd_ptr == nullptr) {
        fprintf(stderr, "ydsh: %s: not builtin command\n", argv[0]);
        this->updateExitStatus(1);
        return;
    }

    int argc;
    for(argc = 0; argv[argc] != nullptr; argc++);

    this->updateExitStatus(cmd_ptr(this, argc, argv));
    flushStdFD();
}

const char *PipelineEvaluator::getCommandName(unsigned int procIndex) {
    return typeAs<String_Object>(this->getARGV(procIndex)[0])->getValue();
}

bool PipelineEvaluator::checkChildError(RuntimeContext &ctx, const std::pair<unsigned int, ChildError> &errorPair) {
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
        ctx.throwSystemError(errorPair.second.errorNum, std::move(msg));
        return false;
    }
    return true;
}


} // namespace core
} // namespace ydsh
