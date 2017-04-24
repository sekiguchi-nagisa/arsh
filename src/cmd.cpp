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

#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>

#include <cstdlib>
#include <cstdarg>
#include <unordered_map>
#include <ydsh/ydsh.h>

#include "logger.h"
#include "cmd.h"
#include "core.h"
#include "symbol.h"
#include "object.h"
#include "misc/num.h"
#include "misc/files.h"
#include "misc/size.hpp"

namespace ydsh {

void xexecve(const char *filePath, char **argv, char *const *envp);

// builtin command definition
static int builtin___gets(DSState &state, Array_Object &argvObj);
static int builtin___puts(DSState &state, Array_Object &argvObj);
static int builtin_cd(DSState &state, Array_Object &argvObj);
static int builtin_check_env(DSState &state, Array_Object &argvObj);
static int builtin_command(DSState &state, Array_Object &argvObj);
static int builtin_complete(DSState &state, Array_Object &argvObj);
static int builtin_echo(DSState &state, Array_Object &argvObj);
static int builtin_eval(DSState &state, Array_Object &argvObj);
static int builtin_exec(DSState &state, Array_Object &argvObj);
static int builtin_exit(DSState &state, Array_Object &argvObj);
static int builtin_false(DSState &state, Array_Object &argvObj);
static int builtin_hash(DSState &state, Array_Object &argvObj);
static int builtin_help(DSState &state, Array_Object &argvObj);
static int builtin_history(DSState &state, Array_Object &argvObj);
static int builtin_ps_intrp(DSState &state, Array_Object &argvObj);
static int builtin_pwd(DSState &state, Array_Object &argvObj);
static int builtin_read(DSState &state, Array_Object &argvObj);
static int builtin_test(DSState &state, Array_Object &argvObj);
static int builtin_true(DSState &state, Array_Object &argvObj);

const struct {
    const char *commandName;
    builtin_command_t cmd_ptr;
    const char *usage;
    const char *detail;
} builtinCommands[] {
        {":", builtin_true, "",
                "    Null command.  Always success (exit status is 0)."},
        {"__gets", builtin___gets, "",
                "    Read standard input and write to standard output."},
        {"__puts", builtin___puts, "[-1] [arg1] [-2] [arg2]",
                "    Print specified argument to standard output/error and print new line.\n"
                "    Options:\n"
                "        -1    print to standard output\n"
                "        -2    print to standard error"},
        {"cd", builtin_cd, "[-LP] [dir]",
                "    Changing the current directory to DIR.  The Environment variable\n"
                "    HOME is the default DIR.  A null directory name is the same as\n"
                "    the current directory. If -L is specified, use logical directory \n"
                "    (with symbolic link). If -P is specified, use physical directory \n"
                "    (without symbolic link). Default is -L."},
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
                "                  \\E    escape sequence\n"
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
        {"history", builtin_history, "[-c] [-d offset] or history [-rw] [file]",
                "    Display or manipulate history list.\n"
                "    Options:\n"
                "        -c        clear the history list\n"
                "        -d offset delete the history entry at OFFSET\n"
                "\n"
                "        -r        read the history list from history file\n"
                "        -w        write the history list to history file"},
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
        {"pwd", builtin_pwd, "[-LP]",
                "    Print the current working directory(absolute path).\n"
                "    If -L specified, print logical working directory.\n"
                "    If -P specified, print physical working directory\n"
                "    (without symbolic link). Default is -L."},
        {"read", builtin_read, "[-r] [-p prompt] [-f field separator] [-u fd] [-t timeout] [name ...]",
                "    Read from standard input.\n"
                "    Options:\n"
                "        -r         disable backslash escape\n"
                "        -p         specify prompt string\n"
                "        -f         specify field separator (if not, use IFS)\n"
                "        -s         disable echo back\n"
                "        -u         specify file descriptor\n"
                "        -t timeout set timeout second (only available if input fd is a tty)"},
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
    return arraySize(builtinCommands);
}

const char *getBuiltinCommandName(unsigned int index) {
    assert(index < getBuiltinCommandSize());
    return builtinCommands[index].commandName;
}

/**
 * return null, if not found builtin command.
 */
builtin_command_t lookupBuiltinCommand(const char *commandName) {
    /**
     * builtin command name and index.
     */
    static CStringHashMap<unsigned int> builtinMap;

    if(builtinMap.empty()) {
        for(unsigned int i = 0; i < arraySize(builtinCommands); i++) {
            builtinMap.insert(std::make_pair(builtinCommands[i].commandName, i));
        }
    }

    auto iter = builtinMap.find(commandName);
    if(iter == builtinMap.end()) {
        return nullptr;
    }
    return builtinCommands[iter->second].cmd_ptr;
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

static int builtin_help(DSState &, Array_Object &argvObj) {
    const unsigned int size = argvObj.getValues().size();

    if(size == 1) {
        printAllUsage(stdout);
        return 0;
    }
    bool isShortHelp = false;
    bool foundValidCommand = false;
    for(unsigned int i = 1; i < size; i++) {
        const char *arg = str(argvObj.getValues()[i]);
        if(strcmp(arg, "-s") == 0 && size == 2) {
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
        ERROR(argvObj, "no help topics match `%s'.  Try `help help'.", str(argvObj.getValues()[size - 1]));
        return 1;
    }
    return 0;
}

static void showUsage(const Array_Object &obj) {
    printUsage(stderr, str(obj.getValues()[0]));
}

static int invalidOptionError(const Array_Object &obj, const GetOptState &s) {
    ERROR(obj, "-%c: invalid option", s.optOpt);
    showUsage(obj);
    return 2;
}

static int invalidOptionError(const Array_Object &obj, const char *opt) {
    ERROR(obj, "%s: invalid option", opt);
    showUsage(obj);
    return 2;
}

static int builtin_cd(DSState &state, Array_Object &argvObj) {
    GetOptState optState;
    bool useLogical = true;
    for(int opt; (opt = optState(argvObj, "PL")) != -1;) {
        switch(opt) {
        case 'P':
            useLogical = false;
            break;
        case 'L':
            useLogical = true;
            break;
        default:
            return invalidOptionError(argvObj, optState);
        }
    }

    unsigned int index = optState.index;
    const char *dest = nullptr;
    bool useOldpwd = false;
    if(index < argvObj.getValues().size()) {
        dest = str(argvObj.getValues()[index]);
        if(strcmp(dest, "-") == 0) {
            dest = getenv(ENV_OLDPWD);
            useOldpwd = true;
        }
    } else {
        dest = getenv(ENV_HOME);
    }

    if(useOldpwd && dest != nullptr) {
        printf("%s\n", dest);
    }

    if(!changeWorkingDir(state, dest, useLogical)) {
        PERROR(argvObj, "%s", dest);
        return 1;
    }
    return 0;
}

static int builtin_check_env(DSState &, Array_Object &argvObj) {
    const unsigned int size = argvObj.getValues().size();
    if(size == 1) {
        showUsage(argvObj);
        return 1;
    }
    for(unsigned int i = 1; i < size; i++) {
        const char *env = getenv(str(argvObj.getValues()[i]));
        if(env == nullptr || strlen(env) == 0) {
            return 1;
        }
    }
    return 0;
}

static int builtin_exit(DSState &state, Array_Object &argvObj) {
    const unsigned int size = argvObj.getValues().size();

    int ret = 0;
    if(size > 1) {
        const char *num = str(argvObj.getValues()[1]);
        int status;
        long value = convertToInt64(num, status);
        if(status == 0) {
            ret = value;
        }
    }
    exitShell(state, ret);
}

static int builtin_echo(DSState &, Array_Object &argvObj) {
    bool newline = true;
    bool interpEscape = false;

    GetOptState optState;
    for(int opt; (opt = optState(argvObj, "neE")) != -1;) {
        switch(opt) {
        case 'n':
            newline = false;
            break;
        case 'e':
            interpEscape = true;
            break;
        case 'E':
            interpEscape = false;
            break;
        default:
            goto END;
        }
    }

    END:
    // print argument
    if(optState.index > 1 && strcmp(str(argvObj.getValues()[optState.index - 1]), "--") == 0) {
        optState.index--;
    }

    unsigned int index = optState.index;
    const unsigned int argc = argvObj.getValues().size();
    bool firstArg = true;
    for(; index < argc; index++) {
        if(firstArg) {
            firstArg = false;
        } else {
            fputc(' ', stdout);
        }
        if(!interpEscape) {
            fputs(str(argvObj.getValues()[index]), stdout);
            continue;
        }
        const char *arg = str(argvObj.getValues()[index]);
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
                case 'E':
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
                    ch = static_cast<char>(v);
                    break;
                }
                case 'x': {
                    if(isHex(arg[i + 1])) {
                        int v = toHex(arg[++i]);
                        if(isHex(arg[i + 1])) {
                            v *= 16;
                            v += toHex(arg[++i]);
                        }
                        ch = static_cast<char>(v);
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
            fputc(ch, stdout);
        }
    }

    if(newline) {
        fputc('\n', stdout);
    }
    return 0;
}

static int builtin_true(DSState &, Array_Object &) {
    return 0;
}

static int builtin_false(DSState &, Array_Object &) {
    return 1;
}

/**
 * for stdin redirection test
 */
static int builtin___gets(DSState &, Array_Object &) {
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
static int builtin___puts(DSState &, Array_Object &argvObj) {
    GetOptState optState;
    for(int opt; (opt = optState(argvObj, "1:2:")) != -1;) {
        switch(opt) {
        case '1':
            fputs(optState.optArg, stdout);
            fputc('\n', stdout);
            fflush(stdout);
            break;
        case '2':
            fputs(optState.optArg, stderr);
            fputc('\n', stderr);
            fflush(stderr);
            break;
        default:
            return 1;
        }
    }
    return 0;
}

/**
 * for prompt string debugging
 */
static int builtin_ps_intrp(DSState &state, Array_Object &argvObj) {
    if(argvObj.getValues().size() != 2) {
        showUsage(argvObj);
        return 1;
    }
    std::string v;
    interpretPromptString(state, str(argvObj.getValues()[1]), v);
    fputs(v.c_str(), stdout);
    fputc('\n', stdout);
    return 0;
}

static int builtin_exec(DSState &state, Array_Object &argvObj) {
    bool clearEnv = false;
    const char *progName = nullptr;
    GetOptState optState;

    for(int opt; (opt = optState(argvObj, "ca:")) != -1;) {
        switch(opt) {
        case 'c':
            clearEnv = true;
            break;
        case 'a':
            progName = optState.optArg;
            break;
        default:
            showUsage(argvObj);
            return 1;
        }
    }

    unsigned int index = optState.index;
    const unsigned int argc = argvObj.getValues().size();
    if(index < argc) { // exec
        char *argv2[argc - index + 1];
        for(unsigned int i = index; i < argc; i++) {
            argv2[i - index] = const_cast<char *>(str(argvObj.getValues()[i]));
        }
        argv2[argc - index] = nullptr;

        const char *filePath = getPathCache(state).searchPath(argv2[0], FilePathCache::DIRECT_SEARCH);
        if(progName != nullptr) {
            argv2[0] = const_cast<char *>(progName);
        }

        char *envp[] = {nullptr};
        xexecve(filePath, argv2, clearEnv ? envp : nullptr);
        PERROR(argvObj, "%s", str(argvObj.getValues()[index]));
        exit(1);
    }
    return 0;
}

static int builtin_eval(DSState &state, Array_Object &argvObj) {
    unsigned int argc = argvObj.getValues().size();
    if(argc <= 1) {
        return 0;
    }

    const char *cmdName = str(argvObj.getValues()[1]);
    // user-defined command
    auto *udcNode = lookupUserDefinedCommand(state, cmdName);
    if(udcNode != nullptr) {
        pid_t pid = xfork(state);
        if(pid == -1) {
            perror("child process error");
            exit(1);
        } else if(pid == 0) {   // child
            eraseFirst(argvObj);
            callUserDefinedCommand(state, udcNode, DSValue(&argvObj), DSValue());
        } else {    // parent process
            int status;
            xwaitpid(state, pid, status, 0);
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
    builtin_command_t builtinCmd = lookupBuiltinCommand(cmdName);
    eraseFirst(argvObj);
    if(builtinCmd != nullptr) {
        return builtinCmd(state, argvObj);
    }

    // external command
    int status = forkAndExec(state, argvObj);
    if(WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if(WIFSIGNALED(status)) {
        return WTERMSIG(status);
    }
    return 0;
}

static int builtin_pwd(DSState &state, Array_Object &argvObj) {
    bool useLogical = true;

    GetOptState optState;
    for(int opt; (opt = optState(argvObj, "LP")) != -1;) {
        switch(opt) {
        case 'L':
            useLogical = true;
            break;
        case 'P':
            useLogical = false;
            break;
        default:
            return invalidOptionError(argvObj, optState);
        }
    }

    if(useLogical) {
        const char *dir = getLogicalWorkingDir(state);
        if(!S_ISDIR(getStMode(dir))) {
            PERROR(argvObj, ".");
            return 1;
        }
        fputs(dir, stdout);
    } else {
        size_t size = PATH_MAX;
        char buf[size];
        if(getcwd(buf, size) == nullptr) {
            PERROR(argvObj, ".");
            return 1;
        }
        fputs(buf, stdout);
    }
    fputc('\n', stdout);
    return 0;
}

static int builtin_command(DSState &state, Array_Object &argvObj) {
    bool useDefaultPath = false;

    /**
     * if 0, ignore
     * if 1, show description
     * if 2, show detailed description
     */
    unsigned char showDesc = 0;

    GetOptState optState;
    for(int opt; (opt = optState(argvObj, "pvV")) != -1;) {
        switch(opt) {
        case 'p':
            useDefaultPath = true;
            break;
        case 'v':
            showDesc = 1;
            break;
        case 'V':
            showDesc = 2;
            break;
        default:
            return invalidOptionError(argvObj, optState);
        }
    }

    unsigned int index = optState.index;
    const unsigned int argc = argvObj.getValues().size();
    if(index < argc) {
        if(showDesc == 0) { // execute command
            auto *cmd = lookupBuiltinCommand(str(argvObj.getValues()[index]));
            auto &values = argvObj.refValues();
            values.erase(values.begin(), values.begin() + index);
            if(cmd != nullptr) {
                return cmd(state, argvObj);
            } else {
                int status = forkAndExec(state, argvObj, useDefaultPath);
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
                const char *commandName = str(argvObj.getValues()[index]);
                // check user defined command
                if(lookupUserDefinedCommand(state, commandName) != nullptr) {
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
                const char *path = getPathCache(state).searchPath(commandName, FilePathCache::DIRECT_SEARCH);
                if(path != nullptr) {
                    successCount++;
                    if(showDesc == 1) {
                        printf("%s\n", path);
                    } else if(getPathCache(state).isCached(commandName)) {
                        printf("%s is hashed (%s)\n", commandName, path);
                    } else {
                        printf("%s is %s\n", commandName, path);
                    }
                    continue;
                }
                if(showDesc == 2) {
                    PERROR(argvObj, "%s", commandName);
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

static int builtin_test(DSState &, Array_Object &argvObj) {
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
    unsigned int argc = argvObj.getValues().size();
    const unsigned int argSize = argc - 1;

    switch(argSize) {
    case 0: {
        result = false;
        break;
    }
    case 1: {
        result = strlen(str(argvObj.getValues()[1])) != 0;  // check if string is not empty
        break;
    }
    case 2: {   // unary op
        const char *op = str(argvObj.getValues()[1]);
        const char *value = str(argvObj.getValues()[2]);
        if(strlen(op) != 2 || op[0] != '-') {
            ERROR(argvObj, "%s: invalid unary operator", op);
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
            result = S_ISDIR(getStMode(value)); // check if file is directory
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
            ERROR(argvObj, "%s: invalid unary operator", op);
            return 2;
        }
        }
        break;
    }
    case 3: {   // binary op
        const char *left = str(argvObj.getValues()[1]);
        const char *op = str(argvObj.getValues()[2]);
        const char *right = str(argvObj.getValues()[3]);

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
                ERROR(argvObj, "%s: must be integer", left);
                return 2;
            }

            long n2 = convertToInt64(right, s);
            if(s != 0) {
                ERROR(argvObj, "%s: must be integer", right);
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
            ERROR(argvObj, "%s: invalid binary operator", op);
            return 2;
        }
        }
        break;
    }
    default: {
        ERROR(argvObj, "too many arguments");
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

static int xfgetc(int fd, int timeout) {
    char ch;
    do {
        errno = 0;

        if(timeout > -2) {
            struct pollfd pollfd[1];
            pollfd[0].fd = fd;
            pollfd[0].events = POLLIN;
            if(poll(pollfd, 1, timeout) != 1) {
                return EOF;
            }
        }

        if(read(fd, &ch, 1) <= 0) {
            ch = EOF;
        }
    } while(ch == EOF && (errno == EAGAIN || errno == EINTR));
    return ch;
}

static int builtin_read(DSState &state, Array_Object &argvObj) {  //FIXME: timeout, UTF-8
    const char *prompt = "";
    const char *ifs = nullptr;
    bool backslash = true;
    bool noecho = false;
    int fd = STDIN_FILENO;
    int timeout = -1;

    GetOptState optState;
    for(int opt; (opt = optState(argvObj, "rp:f:su:t:")) != -1;) {
        switch(opt) {
        case 'p':
            prompt = optState.optArg;
            break;
        case 'f':
            ifs = optState.optArg;
            break;
        case 'r':
            backslash = false;
            break;
        case 's':
            noecho = true;
            break;
        case 'u': {
            int s;
            long t = convertToInt64(optState.optArg, s);
            if(s != 0 || t < 0 || t > INT32_MAX) {
                ERROR(argvObj, "%s: invalid file descriptor", optState.optArg);
                return 1;
            }
            fd = static_cast<int>(t);
            break;
        }
        case 't': {
            int s;
            long t = convertToInt64(optState.optArg, s);
            if(s == 0) {
                if(t > -1 && t <= INT32_MAX) {
                    t *= 1000;
                    if(t > -1 && t <= INT32_MAX) {
                        timeout = static_cast<int>(t);
                        break;
                    }
                }
            }
            ERROR(argvObj, "%s: invalid timeout specification", optState.optArg);
            return 1;
        }
        case ':':
            ERROR(argvObj, "-%c: option require argument", optState.optOpt);
            return 2;
        default:
            return invalidOptionError(argvObj, optState);
        }
    }

    const unsigned int argc = argvObj.getValues().size();
    unsigned int index = optState.index;
    const bool isTTY = isatty(fd) != 0;

    // check ifs
    if(ifs == nullptr) {
        ifs = typeAs<String_Object>(getGlobal(state, toIndex(BuiltinVarOffset::IFS)))->getValue();
    }

    // clear old variable before read
    setGlobal(state, toIndex(BuiltinVarOffset::REPLY), getEmptyStrObj(state));    // clear REPLY
    typeAs<Map_Object>(getGlobal(state, toIndex(BuiltinVarOffset::REPLY_VAR)))->clear();      // clear reply


    const int varSize = argc - index;  // if zero, store line to REPLY
    const unsigned int varIndex = toIndex(varSize == 0 ? BuiltinVarOffset::REPLY : BuiltinVarOffset::REPLY_VAR);
    std::string strBuf;

    // show prompt
    if(isTTY) {
        fputs(prompt, stderr);
        fflush(stderr);
    }

    // change tty state
    struct termios tty;
    struct termios oldtty;
    if(noecho && isTTY) {
        tcgetattr(fd, &tty);
        oldtty = tty;
        tty.c_lflag &= ~(ECHO | ECHOK | ECHONL);
        tcsetattr(fd, TCSANOW, &tty);
    }

    // read line
    if(!isTTY) {
        timeout = -2;
    }
    unsigned int skipCount = 1;
    int ch;
    for(bool prevIsBackslash = false; (ch = xfgetc(fd, timeout)) != EOF;
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
            auto obj = typeAs<Map_Object>(getGlobal(state, varIndex));
            auto varObj = argvObj.getValues()[index];
            auto valueObj = DSValue::create<String_Object>(getPool(state).getStringType(), std::move(strBuf));
            obj->set(std::move(varObj), std::move(valueObj));
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
        setGlobal(state, varIndex,
                  DSValue::create<String_Object>(getPool(state).getStringType(), std::move(strBuf)));
    }

    // set rest variable
    for(; index < argc; index++) {
        auto obj = typeAs<Map_Object>(getGlobal(state, varIndex));
        auto varObj = argvObj.getValues()[index];
        auto valueObj = DSValue::create<String_Object>(getPool(state).getStringType(), std::move(strBuf));
        obj->set(std::move(varObj), std::move(valueObj));
        strBuf = "";
    }

    // restore tty setting
    if(noecho && isTTY) {
        tcsetattr(fd, TCSANOW, &oldtty);
    }

    // report error
    int ret = ch == EOF ? 1 : 0;
    if(ret != 0 && errno != 0) {
        PERROR(argvObj, "%d", fd);
    }
    return ret;
}

static int builtin_hash(DSState &state, Array_Object &argvObj) {
    bool remove = false;

    // check option
    const unsigned int argc = argvObj.getValues().size();
    unsigned int index = 1;
    for(; index < argc; index++) {
        const char *arg = str(argvObj.getValues()[index]);
        if(arg[0] != '-') {
            break;
        }
        if(strcmp(arg, "-r") == 0) {
            remove = true;
        } else {
            return invalidOptionError(argvObj, arg);
        }
    }

    const bool hasNames = index < argc;
    if(hasNames) {
        for(; index < argc; index++) {
            const char *name = str(argvObj.getValues()[index]);
            if(remove) {
                getPathCache(state).removePath(name);
            } else {
                if(getPathCache(state).searchPath(name) == nullptr) {
                    ERROR(argvObj, "%s: not found", name);
                    return 1;
                }
            }
        }
    } else {
        if(remove) {    // remove all cache
            getPathCache(state).clear();
        } else {    // show all cache
            const auto cend = getPathCache(state).cend();
            if(getPathCache(state).cbegin() == cend) {
                fputs("hash: file path cache is empty\n", stdout);
                return 0;
            }
            for(auto iter = getPathCache(state).cbegin(); iter != cend; ++iter) {
                printf("%s=%s\n", iter->first, iter->second.c_str());
            }
        }
    }
    return 0;
}

// for completor debugging
static int builtin_complete(DSState &state, Array_Object &argvObj) {
    const unsigned int argc = argvObj.getValues().size();
    if(argc != 2) {
        showUsage(argvObj);
        return 1;
    }

    std::string line = str(argvObj.getValues()[1]);
    line += '\n';
    auto c = completeLine(state, line);
    for(const auto &e : c) {
        fputs(e, stdout);
        fputc('\n', stdout);

        free(e);
    }
    return 0;
}

static int showHistory(DSState &state, const Array_Object &obj) {
    auto *history = DSState_history(&state);
    unsigned int printOffset = history->size;
    const unsigned int histSize = history->size;
    const unsigned int argc = obj.getValues().size();
    if(argc > 1) {
        if(argc > 2) {
            ERROR(obj, "too many arguments");
            return 1;
        }

        int s;
        const char *arg = str(obj.getValues()[1]);
        printOffset = convertToUint64(arg, s);
        if(s != 0) {
            ERROR(obj, "%s: numeric argument required", arg);
            return 1;
        }

        if(printOffset > histSize) {
            printOffset = histSize;
        }
    }

    const unsigned int histCmd = typeAs<Int_Object>(getGlobal(state, toIndex(BuiltinVarOffset::HIST_CMD)))->getValue();
    const unsigned int base = histCmd - histSize;
    for(unsigned int i = histSize - printOffset; i < histSize; i++) {
        fprintf(stdout, "%5d  %s\n", i + base, history->data[i]);
    }
    return 0;
}

static int builtin_history(DSState &state, Array_Object &argvObj) {
    DSState_syncHistorySize(&state);

    const unsigned int argc = argvObj.getValues().size();
    if(argc == 1 || str(argvObj.getValues()[1])[0] != '-') {
        return showHistory(state, argvObj);
    }

    char op = '\0';
    const char *fileName = nullptr;
    const char *deleteTarget = nullptr;

    for(unsigned int i = 1; i < argc; i++) {
        const char *arg = str(argvObj.getValues()[i]);
        if(arg[0] == '-' && strlen(arg) == 2) {
            char ch = arg[1];
            switch(ch) {
            case 'c':
                DSState_clearHistory(&state);
                return 0;
            case 'd': {
                if(i + 1 < argc) {
                    deleteTarget = str(argvObj.getValues()[++i]);
                    continue;
                } else {
                    ERROR(argvObj, "%s: option requires argument", arg);
                    return 2;
                }
            }
            case 'r':
            case 'w': {
                if(op != '\0') {
                    ERROR(argvObj, "cannot use more than one of -rw");
                    return 1;
                }
                op = ch;
                fileName = i + 1 < argc
                           && str(argvObj.getValues()[i + 1])[0] != '-' ? str(argvObj.getValues()[++i]) : nullptr;
                continue;
            }
            default:
                break;
            }
        }
        return invalidOptionError(argvObj, arg);
    }

    auto *history = DSState_history(&state);
    if(deleteTarget != nullptr) {
        int s;
        int offset = convertToInt64(deleteTarget, s) - 1;
        if(s != 0 || offset < 0 || static_cast<unsigned int>(offset) > history->size) {
            ERROR(argvObj, "%s: history offset out of range", deleteTarget);
            return 1;
        }
        DSState_deleteHistoryAt(&state, offset);
        return 0;
    }

    switch(op) {
    case 'r':
        DSState_loadHistory(&state, fileName);
        break;
    case 'w':
        DSState_saveHistory(&state, fileName);
        break;
    }
    return 0;
}


} // namespace ydsh
