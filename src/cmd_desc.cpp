/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include <sys/resource.h> // for ulimit macros (do not remove it)

#include <string>

#include "cmd_desc.h"

namespace arsh {

static BuiltinCmdDesc table[] = {
    {":", "", "    Null command.  Always success (exit status is 0)."},
    {"__gets", "", "    Read standard input and write to standard output."},
    {"__puts", "[-1 arg1] [-2 arg2]",
     "    Print specified argument to standard output/error and print new line.\n"
     "    Options:\n"
     "      -1    print to standard output\n"
     "      -2    print to standard error"},
    {"_exit", "[n]",
     "    Exit the shell with a status of N.  If N is omitted, the exit\n"
     "    status is $?. Unlike exit, it causes normal program termination\n"
     "    without cleaning the resources."},
    {"bg", "[job_spec ...]",
     "    Move jobs to the background.\n"
     "    JOB_SPEC is represented as [%]? [1-9][0-9].\n"
     "    If JOB_SPEC is not present, latest job is used."},
    {"call", "[cmd [arg ...]]", "    Call CMD with ARGs."},
    {"cd", "[-LP] [dir]",
     "    Changing the current directory to DIR.  The Environment variable\n"
     "    HOME is the default DIR.  A null directory name is the same as\n"
     "    the current directory.  If -L is specified, use logical directory \n"
     "    (with symbolic link).  If -P is specified, use physical directory \n"
     "    (without symbolic link).  Default is -L."},
    {"checkenv", "variable ...",
     "    Check existence of specified environmental variables.\n"
     "    If all of variables are exist and not empty string, exit with 0."},
    {"command", "[-pVv] command [arg ...]",
     "    Execute COMMAND with ARGs excepting user defined command.\n"
     "    If -p option is specified, search command from default PATH.\n"
     "    If -V or -v option are specified, print description of COMMAND.\n"
     "    -V option shows more detailed information."},
    {"complete", "[-A action] [-m descriptor] [-dqs] line",
     "    Show completion candidates.\n"
     "    Options:\n"
     "      -d    may put description of completion candidate\n"
     "      -q    does not show completion candidates\n"
     "      -s    may put space to completion candidate suffix\n"
     "      -m    complete in specified module context\n"
     "      -A    show completion candidates via ACTION\n"
     "\n"
     "    Actions:\n"
     "      file       complete file names\n"
     "      dir        complete directory names\n"
     "      module     complete module names\n"
     "      exec       complete executable file names\n"
     "      tilde      expand tilde before completion. only available in \n"
     "                 combination of file, module exec actions\n"
     "      command    complete command names including external, user-defined, builtin ones\n"
     "      cmd        equivalent to 'command'\n"
     "      external   complete external commands\n"
     "      builtin    complete builtin commands\n"
     "      udc        complete user-defined commands\n"
     "      variable   complete variable names\n"
     "      var        equivalent to var\n"
     "      type       complete type names\n"
     "      env        complete environmental variable names\n"
     "      valid_env  complete environmental variable names (valid name only)\n"
     "      signal     complete signal names\n"
     "      user       complete user names\n"
     "      group      complete group names\n"
     "      stmt_kw    complete statement keywords\n"
     "      expr_kw    complete expression keywords"},
    {"dirs", "[-clpv]",
     "    Display directory stack.\n"
     "    Options:\n"
     "      -c    clear all entries of directory stack\n"
     "      -l    print directory entry without prefix ~\n"
     "      -p    print directory entry one per line\n"
     "      -v    print directory with verbose information"},
    {"disown", "[jobspec ...]", "    Remove specified jobs from job table."},
    {"echo", "[-neE] [arg ...]",
     "    Print argument to standard output and print new line.\n"
     "    Options:\n"
     "      -n    not print new line\n"
     "      -e    interpret some escape sequence\n"
     "              \\\\    backslash\n"
     "              \\a    bell\n"
     "              \\b    backspace\n"
     "              \\c    ignore subsequent string\n"
     "              \\e    escape sequence\n"
     "              \\E    escape sequence\n"
     "              \\f    form feed\n"
     "              \\n    newline\n"
     "              \\r    carriage return\n"
     "              \\t    horizontal tab\n"
     "              \\v    vertical tab\n"
     "              \\0nnn N is octal number.  NNN can be 0 to 3 number\n"
     "              \\xnn  N is hex number.  NN can be 1 to 2 number\n"
     "              \\unnnn\n"
     "                    N is hex number. NNNN can be 1 to 4 number\n"
     "              \\Unnnnnnnn\n"
     "                    N is hex number. NNNNNNNN can be 1 to 8 number\n"
     "      -E    disable escape sequence interpretation"},
    {"eval", "[arg ...]", "    Evaluate ARGs as command."},
    {"exec", "[-c] [-a name] file [args ...]",
     "    Execute FILE and replace this shell with specified program.\n"
     "    If FILE is not specified, the redirections take effect in this shell.\n"
     "    IF FILE execution fail, terminate this shell immediately\n"
     "    Options:\n"
     "      -c    cleaner environmental variable\n"
     "      -a    specify set program name(default is FILE)"},
    {"exit", "[n]",
     "    Exit the shell with a status of N.  If N is omitted, the exit\n"
     "    status is $?."},
    {"false", "", "    Always failure (exit status is 1)."},
    {"fg", "[job_spec]",
     "    Move job to the foreground.\n"
     "    JOB_SPEC is represented as [%]? [1-9][0-9].\n"
     "    If JOB_SPEC is not present, latest job is used."},
    {"getenv", "name", "    Get environmental variable and store to REPLY."},
    {"hash", "[-r] [command ...]",
     "    Cache file path of specified commands.  If -r option is supplied,\n"
     "    removes specified command path (if not specified, remove all cache).\n"
     "    If option is not supplied, display all cached path."},
    {"help", "[-s] [pattern ...]", "    Display helpful information about builtin commands."},
    {"jobs", "[-lprs] [jobspec ...]",
     "    Display status of jobs specified by JOBSPEC\n"
     "    If JOBSPEC is not specified, display all status of all jobs.\n"
     "    Options:\n"
     "      -l    show verbose information\n"
     "      -p    show process id only\n"
     "      -r    only allow running jobs\n"
     "      -s    only allow stopped jobs"},
    {"kill", "[-s signal] pid | jobspec ... or kill -l [signal...]",
     "    Send a signal to a process or job.\n"
     "    If signal is not specified, then SIGTERM is assumed.\n"
     "    Options:\n"
     "      -s sig    send a signal.  SIG is a signal name or signal number\n"
     "      -l        list the signal names\n"
     "      -L        equivalent to `-l'"},
    {"popd", "[+N | -N]",
     "    Remove an entry from directory stack and change current directory to\n"
     "    the stack top directory.\n"
     "    Arguments:\n"
     "      +N    remove the Nth entry from the left\n"
     "      -N    remove the Nth entry from the right"},
    {"printf", "[-v var] format [arguments]",
     "    Print formatted string similar to printf(1).\n"
     "    In addition to printf(1) format, support the following formats\n"
     "      %b       interpret backslash escape sequences\n"
     "      %q       quote as shell argument\n"
     "      %(fmt)T  interpret FMT as time format (like strftime)\n"
     "    Options:\n"
     "      -v var    store formatted output to reply variable (get by VAR)\n"
     "                rather than print to stdout"},
    {"pushd", "[+N | -N | dir]",
     "    Change the current directory and push the old current directory onto the stack.\n"
     "    Arguments:\n"
     "      +N     rotate the directory stack so that the Nth entry from left is stack top\n"
     "      -N     rotate the directory stack so that the Nth entry from right is stack top\n"
     "      dir    push DIR onto the stack"},
    {"pwd", "[-LP]",
     "    Print the current working directory(absolute path).\n"
     "    If -L specified, print logical working directory.\n"
     "    If -P specified, print physical working directory\n"
     "    (without symbolic link).  Default is -L."},
    {"read", "[-r] [-p prompt] [-f field separator] [-u fd] [-t timeout] [name ...]",
     "    Read from standard input.\n"
     "    Options:\n"
     "      -r         disable backslash escape\n"
     "      -p         specify prompt string\n"
     "      -f         specify field separator (if not, use IFS)\n"
     "      -s         disable echo back\n"
     "      -u         specify file descriptor\n"
     "      -t timeout set timeout second (only available if input fd is a tty)"},
    {"setenv", "[name=env ...]", "    Set environmental variables."},
    {"shctl", "[subcommand]",
     "    Query and set runtime information\n"
     "    Subcommands:\n"
     "      is-interactive      return 0 if shell is interactive mode.\n"
     "      is-sourced          return 0 if current script is sourced.\n"
     "      backtrace           print stack trace.\n"
     "      function            print current function/command name.\n"
     "      module [MOD ...]    print full path of loaded modules or scripts.\n"
     "                          if specified MODs, find and print full path of theme\n"
     "      set [OPTION ...]    set runtime option. if options is not specified, show option "
     "setting\n"
     "          -d              dump current runtime options to REPLY.\n"
     "          -r <dump>       restore runtime option from <dump>.\n"
     "      unset OPTION ...    unset/disable/off runtime option.\n"
     "      info                show configuration (also get via 'reply' variable).\n"
     "      winsize             get and update LINES/COLUMNS."},
    {"test", "[expr]",
     "    Unary or Binary expressions.\n"
     "    If expression is true, return 0\n"
     "    If expression is false, return 1\n"
     "    If operand or operator is invalid, return 2\n"
     "\n"
     "    String operators:\n"
     "      -z STRING      check if string is empty\n"
     "      -n STRING\n"
     "      STRING         check if string is not empty\n"
     "      STRING1 = STRING2\n"
     "      STRING1 == STRING2\n"
     "                     check if strings are equal\n"
     "      STRING1 != STRING2\n"
     "                     check if strings are not equal\n"
     "      STRING1 < STRING2\n"
     "                     check if STRING1 is less than STRING2 with dictionary order\n"
     "      STRING1 > STRING2\n"
     "                     check if STRING2 is greater than STRING2 with dictionary order\n"
     "    Integer operators:\n"
     "      INT1 -eq INT2  check if integers are equal\n"
     "      INT1 -ne INT2  check if integers are not equal\n"
     "      INT1 -lt INT2  check if INT1 is less than INT2\n"
     "      INT1 -gt INT2  check if INT1 is greater than INT2\n"
     "      INT1 -le INT2  check if INT1 is less than or equal to INT2\n"
     "      INT1 -ge INT2  check if INT1 is greater than or equal to INT2\n"
     "\n"
     "    Integer value is signed int 64.\n"
     "\n"
     "    File operators:\n"
     "      -a FILE\n"
     "      -e FILE        check if file exists\n"
     "      -b FILE        check if file is block device\n"
     "      -c FILE        check if file is character device\n"
     "      -d FILE        check if file is a directory\n"
     "      -f FILE        check if file is a regular file\n"
     "      -g FILE        check if file has set-group-id bit\n"
     "      -h FILE\n"
     "      -L FILE        check if file is a symbolic link\n"
     "      -k FILE        check if file has sticky bit\n"
     "      -p FILE        check if file is a named pipe\n"
     "      -r FILE        check if file is readable\n"
     "      -s FILE        check if file is not empty\n"
     "      -S FILE        check if file is a socket\n"
     "      -t FD          check if file descriptor is a terminal\n"
     "      -u FILE        check if file has set-user-id bit\n"
     "      -w FILE        check if file is writable\n"
     "      -x FILE        check if file is executable\n"
     "      -O FILE        check if file is effectively owned by user\n"
     "      -G FILE        check if file is effectively owned by group\n"
     "\n"
     "      FILE1 -nt FILE2  check if file1 is newer than file2\n"
     "      FILE1 -ot FILE2  check if file1 is older than file2\n"
     "      FILE1 -ef FILE2  check if file1 and file2 refer to the same file"},
    {"true", "", "    Always success (exit status is 0)."},
    {"ulimit",
     "[-H | -S] [-a | -"
#define DEF(O, R, S, N, D) O
#include "ulimit-def.in"
#undef DEF
     " [value]]",
     "    Set or show resource limits of the shell and processes started by the shell.\n"
     "    If VALUE is `soft', `hard' and `unlimited', represent current soft limit\n"
     "    and hard limit and no limit. If no option specified, assume `-f'.\n"
     "    Options.\n"
     "      -H    use `hard' resource limit\n"
     "      -S    use `soft' resource limit (default)\n"
     "      -a    show all resource limits"
#define DEF(O, R, S, N, D) "\n      -" O "    " D
#include "ulimit-def.in"
#undef DEF
    },
    {"umask", "[-p] [-S] [mode]",
     "    Display or set file mode creation mask.\n"
     "    Set the calling process's file mode creation mask to MODE.\n"
     "    If MODE is omitted, prints current value of mask.\n"
     "    Options.\n"
     "      -p    if mode is omitted, print current mask in a form that may be reused as input\n"
     "      -S    print current mask in a symbolic form"},
    {"unsetenv", "[name ...]", "    Unset environmental variables."},
    {"wait", "[-n] [id ...]",
     "    Wait for termination of processes or jobs.\n"
     "    If ID is not specified, wait for termination of all managed jobs.\n"
     "    Return the exit status of last ID. If ID is not found or not managed,\n"
     "    the exit status is 127."},
};

unsigned int getBuiltinCmdSize() { return std::size(table); }

const BuiltinCmdDesc *getBuiltinCmdDescList() { return table; }

} // namespace arsh